import hashlib
import re
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

work_dir = Path(__file__).resolve().parent
repo_dir = work_dir.parents[1]
build_pal_sh = work_dir / 'build-pal-startup.sh'
log_file = work_dir / 'pal-startup-host.log'
shards_dir = work_dir / 'host_shards'
shards_dir.mkdir(exist_ok=True)

NUM_SHARDS = 24

# 1. Index all functions
func_to_shard = {}
for tier in ('base_common', 'base_portable_sensitive', 'retro_mod', 'retro_portable_sensitive', 'base_extra'):
    tier_dir = repo_dir / 'generated/build_shards' / tier
    if not tier_dir.exists():
        continue
    for p in tier_dir.glob('*.cpp'):
        rel = p.relative_to(repo_dir)
        txt = p.read_text()
        for addr in re.findall(r'^#line 1 "func_([0-9A-F]{8})\.cpp"', txt, re.M):
            func_to_shard[addr] = rel

print(f"Indexed {len(func_to_shard)} functions.")

def read_linked_from_script():
    if not build_pal_sh.exists():
        return []
    text = build_pal_sh.read_text()
    m = re.search(r'linked = \((.*?)\)\n', text, re.DOTALL)
    if not m:
        return []
    return list(eval('(' + m.group(1) + ')'))

def read_linked_seed():
    """Committed link list (one address per line).

    `host_shards/*.cpp` and `pal_sections.S` are generated artifacts derived from the game
    binaries, so they are NOT tracked in git. This seed file is the tracked bootstrap so a fresh
    clone can regenerate everything from the user's own DOL/REL. It wins over the generated
    build-pal-startup.sh so regeneration is reproducible.
    """
    seed = work_dir / 'linked-functions.txt'
    if not seed.exists():
        return []
    out = []
    for line in seed.read_text().split():
        line = line.strip().upper()
        if line and not line.startswith('#'):
            out.append(line)
    return out

def write_linked_seed(linked):
    seed = work_dir / 'linked-functions.txt'
    seed.write_text('\n'.join(linked) + '\n')

shards_cache = {}
def get_shard(path):
    if path not in shards_cache:
        shards_cache[path] = (repo_dir / path).read_text()
    return shards_cache[path]

def extract_direct_callees(addr):
    shard_path = func_to_shard.get(addr)
    if not shard_path:
        return set()
    shard = get_shard(shard_path)
    try:
        block = shard.split(f'#line 1 "func_{addr}.cpp"\n', 1)[1].split('// RECOMP_GUEST_ABI', 1)[0]
    except IndexError:
        return set()
    callees = set(re.findall(r'InvokeDirectCpu<0x([0-9A-Fa-f]{8})u>', block))
    callees.update(re.findall(r'MKW_STATIC_TRANSLATED_CALL\(0x([0-9A-Fa-f]{8})u', block))
    return {c.upper() for c in callees if c.upper() in func_to_shard}

def expand_transitive_callees(roots, linked_list, linked_set):
    frontier = set(roots)
    added = []
    while frontier:
        next_frontier = set()
        for addr in frontier:
            for callee in extract_direct_callees(addr):
                if callee not in linked_set:
                    linked_set.add(callee)
                    linked_list.append(callee)
                    added.append(callee)
                    next_frontier.add(callee)
        frontier = next_frontier
    return added

def format_func_body(addr):
    shard_path = func_to_shard.get(addr)
    if not shard_path:
        return None
    shard = get_shard(shard_path)
    try:
        block = shard.split(f'#line 1 "func_{addr}.cpp"\n', 1)[1].split('// RECOMP_GUEST_ABI', 1)[0]
    except IndexError:
        return None
    body = re.search(rf'(?ms)^extern "C" void func_{addr}\(.*?\n\}}\n\n\}}', block)
    if not body:
        return None
    body_source = re.sub(
        r'(?m)^    if \((?:MkwStateFreeAbiEnabled[^\n]+|false)\)\s*\{\n(?:        [^\n]*\n)+?    \} else \{',
        '    {', body[0])
    traced = re.sub(r'(?m)^loc_([0-9A-F]{8}):\n\{', r'loc_\1:\n{\n    ctx->pc = 0x\1u;', body_source)
    return traced

SHARD_HEADER = '''#include "ppc_runtime.h"
#include "isa/ppc_isa_cr.h"

struct BootBoundary { uint32_t address; };
void DispatchStartup(uint32_t target, CpuContext* cpu);
void InvokeIndirectCpu(uint32_t target, CpuContext* cpu);
inline void InvokeIndirectJump(uint32_t target, CpuContext* cpu) { InvokeIndirectCpu(target, cpu); }
template <uint32_t Target> inline void InvokeDirectCpu(CpuContext* cpu) { DispatchStartup(Target, cpu); }
#define MKW_STATIC_TRANSLATED_CALL(Target, Entry, Context) DispatchStartup(Target, Context)
extern "C" void PPC_Mtfsb1(uint32_t bit);

'''

# Cache of file hashes on disk to prevent redundant recompilations
g_shard_hashes = {}

def sync_shards_and_dispatch(linked):
    # Distribute functions across 8 shards using bit shift for 4-byte instruction alignment
    shard_map = [[] for _ in range(NUM_SHARDS)]
    for addr in linked:
        idx = (int(addr, 16) >> 2) % NUM_SHARDS
        shard_map[idx].append(addr)

    # 1. Write shards
    for i in range(NUM_SHARDS):
        path = shards_dir / f'shard_{i}.cpp'
        parts = [SHARD_HEADER]
        for addr in shard_map[i]:
            body = format_func_body(addr)
            if body:
                parts.append(body + '\n\n')
        content = ''.join(parts)
        h = hashlib.md5(content.encode('utf-8')).hexdigest()
        if g_shard_hashes.get(i) != h or not path.exists():
            path.write_text(content)
            g_shard_hashes[i] = h

    # 2. Write dispatch.cpp
    disp_path = shards_dir / 'dispatch.cpp'
    disp_parts = ['''#include "ppc_runtime.h"
#include "isa/ppc_isa_cr.h"

struct BootBoundary { uint32_t address; };
bool handle_startup_hle(uint32_t target, CpuContext* cpu);

''']
    for addr in linked:
        disp_parts.append(f'extern "C" void func_{addr}(CpuContext*);\n')

    disp_parts.append('''
void DispatchStartup(uint32_t target, CpuContext* cpu) {
    if (handle_startup_hle(target, cpu)) return;
    switch (target) {
''')
    for addr in linked:
        disp_parts.append(f'    case 0x{addr}u: func_{addr}(cpu); return;\n')
    disp_parts.append('''    }
    throw BootBoundary{target};
}

void InvokeIndirectCpu(uint32_t target, CpuContext* cpu) {
    DispatchStartup(target, cpu);
}

uint32_t run_guest_boot(CpuContext& cpu)
{
    CpuContextScope scope(&cpu);
    try {
        func_800060A4(&cpu);
    } catch (const BootBoundary& boundary) {
        return boundary.address;
    }
    return 0;
}
''')
    disp_content = ''.join(disp_parts)
    dh = hashlib.md5(disp_content.encode('utf-8')).hexdigest()
    if g_shard_hashes.get('dispatch') != dh or not disp_path.exists():
        disp_path.write_text(disp_content)
        g_shard_hashes['dispatch'] = dh

def compile_one_source(src):
    obj = src.with_suffix('.o')
    cmd = [
        'clang++', '-std=c++20', '-O0',
        '-Iruntime-overlay', '-I../../generated',
        '-c', str(src), '-o', str(obj)
    ]
    t0 = time.time()
    res = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True)
    dt = time.time() - t0
    if res.returncode != 0:
        return False, src.name, dt, res.stderr
    return True, src.name, dt, ''

def save_arm11_build_script(linked):
    func_shards_lines = []
    for addr in linked:
        if addr in func_to_shard:
            func_shards_lines.append(f"        {repr(addr)}: {repr(str(func_to_shard[addr]))},")
    func_shards_block = "func_shards = {\n" + "\n".join(func_shards_lines) + "\n}\n"

    linked_lines = []
    chunk_size = 8
    for i in range(0, len(linked), chunk_size):
        chunk = linked[i:i + chunk_size]
        linked_lines.append("    " + ", ".join(repr(x) for x in chunk) + ",")
    linked_block = "linked = (\n" + "\n".join(linked_lines) + "\n)\n"

    py_script = """from pathlib import Path
import re

shards_cache = {}
def get_shard(path):
    if path not in shards_cache:
        shards_cache[path] = Path('../../' + path).read_text()
    return shards_cache[path]

""" + linked_block + func_shards_block + """with Path('pal_guest.cpp').open('w') as out:
    out.write('''#include "ppc_runtime.h"
#include "isa/ppc_isa_cr.h"

struct BootBoundary { uint32_t address; };
bool handle_startup_hle(uint32_t target, CpuContext* cpu);
extern "C" void PPC_Mtfsb1(uint32_t bit);
template <uint32_t Target> void DispatchStartup(CpuContext* cpu);
template <uint32_t Target> void InvokeDirectCpu(CpuContext* cpu) {
    DispatchStartup<Target>(cpu);
}
void InvokeIndirectCpu(uint32_t target, CpuContext* cpu);
void InvokeIndirectJump(uint32_t target, CpuContext* cpu) { InvokeIndirectCpu(target, cpu); }

''')
    for address in linked:
        out.write(f'extern "C" void func_{address}(CpuContext*);\\n')
    out.write('#define MKW_STATIC_TRANSLATED_CALL(Target, Entry, Context) DispatchStartup<Target>(Context)\\n')
    out.write('template <uint32_t Target> void DispatchStartup(CpuContext* cpu) {\\n')
    out.write('    if (handle_startup_hle(Target, cpu)) return;\\n')
    for address in linked:
        out.write(f'    if constexpr (Target == 0x{address}u) {{ func_{address}(cpu); return; }}\\n')
    out.write('    throw BootBoundary{Target};\\n}\\n')
    out.write('void InvokeIndirectCpu(uint32_t target, CpuContext* cpu) {\\n')
    out.write('    if (handle_startup_hle(target, cpu)) return;\\n    switch (target) {\\n')
    for address in linked:
        out.write(f'    case 0x{address}u: func_{address}(cpu); return;\\n')
    out.write('    }\\n    throw BootBoundary{target};\\n}\\n')
    for address in linked:
        shard_path = func_shards.get(address)
        if not shard_path:
            continue
        shard = get_shard(shard_path)
        block = shard.split(f'#line 1 "func_{address}.cpp"\\n', 1)[1].split('// RECOMP_GUEST_ABI', 1)[0]
        body = re.search(rf'(?ms)^extern "C" void func_{address}\\(.*?^\\}}\\n\\n\\}}', block)
        assert body, address
        body_source = re.sub(
            r'(?m)^    if \\((?:MkwStateFreeAbiEnabled[^\\n]+|false)\\)\\s*\\{\\n(?:        [^\\n]*\\n)+?    \\} else \\{',
            '    {', body[0])
        traced = re.sub(r'(?m)^loc_([0-9A-F]{8}):\\n\\{', r'loc_\\1:\\n{\\n    ctx->pc = 0x\\1u;', body_source)
        out.write(traced + '\\n\\n')
    out.write('''uint32_t run_guest_boot(CpuContext& cpu)
{
    CpuContextScope scope(&cpu);
    try {
        func_800060A4(&cpu);
    } catch (const BootBoundary& boundary) {
        return boundary.address;
    }
    return 0;
}
''')
assembly = Path('../../generated/data_sections_init_blobs.S').read_text()
assembly = assembly.replace('.section __TEXT,__const', '.section .rodata')
assembly = re.sub(r'(?m)^(\\.globl )?_kData_', r'\\1kData_', assembly)
fst = Path('../../Assets/extracted-rmcp01/DATA/sys/fst.bin').resolve()
assembly += f'\\n.p2align 4\\n.globl kStartupFst\\nkStartupFst:\\n.incbin "{fst}"\\n.globl kStartupFstEnd\\nkStartupFstEnd:\\n'
Path('pal_sections.S').write_text(assembly)
"""
    content = f'''#!/bin/sh
set -eu
cd "$(dirname "$0")"
sh compile-arm11-shard.sh --prepare-only
python3 - <<'PY'
{py_script}PY
if [ "${{1:-}}" = "--prepare-only" ]; then exit 0; fi
sdk=${{DEVKITPRO:-/opt/devkitpro}}
"$sdk/devkitARM/bin/arm-none-eabi-g++" -D__3DS__ -std=c++20 -O2 \\
    -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -specs=3dsx.specs \\
    -Iruntime-overlay -I../../generated -I"$sdk/libctru/include" -L"$sdk/libctru/lib" \\
    pal_startup_main.cpp pal_guest.cpp ../../generated/data_sections_init.cpp pal_sections.S -lctru -lm -o wiicompiled-pal-startup.elf
"$sdk/tools/bin/3dsxtool" wiicompiled-pal-startup.elf wiicompiled-pal-startup.3dsx
'''
    build_pal_sh.write_text(content)
    write_linked_seed(linked)

def run_host_cycle():
    # Check pal_startup_main.cpp
    main_src = work_dir / 'pal_startup_main.cpp'
    main_obj = work_dir / 'pal_startup_main_host.o'
    if not main_obj.exists() or main_src.stat().st_mtime > main_obj.stat().st_mtime:
        res = subprocess.run([
            'clang++', '-std=c++20', '-O0',
            '-Iruntime-overlay', '-I../../generated',
            '-c', str(main_src), '-o', str(main_obj)
        ], cwd=work_dir, capture_output=True, text=True)
        if res.returncode != 0:
            print("Compile pal_startup_main_host.o failed:\n", res.stderr, flush=True)
            return False, "compile_main_failed"

    # Identify which shards need compilation
    to_compile = []
    for i in range(NUM_SHARDS):
        src = shards_dir / f'shard_{i}.cpp'
        obj = src.with_suffix('.o')
        if not obj.exists() or src.stat().st_mtime > obj.stat().st_mtime:
            to_compile.append(src)
    
    disp_src = shards_dir / 'dispatch.cpp'
    disp_obj = disp_src.with_suffix('.o')
    if not disp_obj.exists() or disp_src.stat().st_mtime > disp_obj.stat().st_mtime:
        to_compile.append(disp_src)

    if to_compile:
        with ThreadPoolExecutor(max_workers=min(len(to_compile), NUM_SHARDS)) as ex:
            results = list(ex.map(compile_one_source, to_compile))
        for ok, name, dt, err in results:
            if not ok:
                print(f"Compile {name} failed ({dt:.2f}s):\n{err}", flush=True)
                return False, f"compile_{name}_failed"

    # Link pal-startup-host
    cmd = [
        'clang++', '-std=c++20', '-O0',
        str(main_obj), str(disp_obj),
        *[str(shards_dir / f'shard_{i}.o') for i in range(NUM_SHARDS)],
        str(work_dir / 'data_sections_init_host.o'),
        str(work_dir / 'data_sections_init_blobs_host.o'),
        str(work_dir / 'pal_fst_host.o'),
        str(work_dir / 'ppc_quantized_host.o'),
        '-o', str(work_dir / 'pal-startup-host')
    ]
    res = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print("Link failed:\n", res.stderr, flush=True)
        return False, "link_failed"

    # Run host with timeout
    try:
        res = subprocess.run(['./pal-startup-host'], cwd=work_dir, capture_output=True, timeout=60)
        log_text = res.stdout.decode('utf-8', errors='replace') + res.stderr.decode('utf-8', errors='replace')
    except subprocess.TimeoutExpired as e:
        out_str = e.stdout.decode('utf-8', errors='replace') if isinstance(e.stdout, bytes) else (e.stdout or "")
        err_str = e.stderr.decode('utf-8', errors='replace') if isinstance(e.stderr, bytes) else (e.stderr or "")
        log_text = out_str + err_str + "\n[TimeoutExpired in pal-startup-host]\n"
    log_file.write_text(log_text)

    # Check outcome
    boundaries = re.findall(r'(?m)^Boot boundary: ([0-9A-Fa-f]{8})', log_text)
    if boundaries:
        return True, ('boundary', boundaries[-1])
    if 'Guest entry returned' in log_text:
        return True, ('returned', None)
    if '[TimeoutExpired' in log_text:
        return False, 'timeout'
    return True, ('log', log_text[-500:])

if __name__ == '__main__':
    base_linked = [
        '800060A4', '80006210', '800062A0', '80005F34', '8000636C', '80006038', '80005F84', '8019FC68',
        '801A31F0', '801A9548', '801A05B8', '801A8238', '801A245C', '8015D3AC', '801A8BD4', '801A8DD4',
        '801A961C', '801A95A8', '8019F9BC', '8019F5C0', '8019F79C', '801B0180', '801AC668', '801A0504',
        '801949B8', '80193040', '801B033C', '801B0450', '801B0694', '8019F2D0', '80006068', '801EBD30',
        '801DCCC4', '801E8B80', '801B1D00', '801ABB40', '801E5B54', '801B0FC0', '801E5754', '801A7EAC',
        '8001C478', '801E63E4', '801E6590', '801E8AEC', '801B1E90', '801B1484', '800216F0', '801E89A4',
        '8015DF1C', '8019F980', '801AD07C', '8016765C', '801AC220', '801ABD70', '801AE524', '801AE544',
        '800211E4', '800099CC', '80027AB0', '80021338', '80035930', '80036580', '800380B0', '8003E220',
        '800648C0', '80077930', '80060F20', '8016FF40', '8001B590', '8001913C', '8001AED0', '801700C8',
        '80060E40', '80020FF4', '80061A70', '80061FB0', '80199D04', '80085B40', '80079900', '8007B310',
        '8007CC20', '8007F230'
    ]

    ctors_raw = (repo_dir / 'generated/data_sections_init_blobs/_ctors.bin').read_bytes()
    all_ctors = [f'{x:08X}' for (x,) in struct.iter_unpack('>I', ctors_raw) if x != 0]

    known_traversed = [
        '80215D68', '80215DC8', '8022FAC4', '80215E90', '8023D2B4', '8000B6B0', '80008EF0', '80009194',
        '80242504', '801A0FC8', '802296A8', '80226A1C', '80198CA8', '8019832C', '80199C08', '80229ADC',
        '80198658', '802296E8', '8021A0F0', '800AEF80', '80226AC8', '80226C04', '80198D88', '8019899C',
        '80198798', '80229D74', '80207D84', '802090E8', '801EF500', '8020C194', '801F449C', '80207BDC',
        '801FEE74', '80205B6C', '8020B3E0', '80229540', '80229DE0', '80229814', '802434D8', '80229640'
    ]

    linked = read_linked_seed() or read_linked_from_script()
    if not linked:
        linked = list(base_linked)
    linked_set = set(linked)

    for item in all_ctors + known_traversed:
        if item not in linked_set:
            if item in func_to_shard:
                linked.append(item)
                linked_set.add(item)

    harvested = expand_transitive_callees(list(linked), linked, linked_set)
    print(f"Statically harvested {len(harvested)} direct callees. Total linked: {len(linked)}", flush=True)

    sync_shards_and_dispatch(linked)
    ok, res = run_host_cycle()
    print("Run cycle result:", ok, res, flush=True)

    steps = 0
    max_steps = 1000
    for a in sys.argv[1:]:
        if a.isdigit():
            max_steps = int(a)
        elif a.startswith('--max='):
            max_steps = int(a.split('=')[1])

    while ok and isinstance(res, tuple) and res[0] == 'boundary' and steps < max_steps:
        t_step_start = time.time()
        addr = res[1]
        if addr in linked_set:
            print(f"Loop detected on boundary {addr} already linked!", flush=True)
            break
        if addr not in func_to_shard:
            print(f"Boundary {addr} NOT in translated shards! Check MAP.txt / HLE.", flush=True)
            break
        print(f"[{steps}] Auto-linking {addr} from {func_to_shard[addr].name}", flush=True)
        linked.append(addr)
        linked_set.add(addr)
        new_callees = expand_transitive_callees([addr], linked, linked_set)
        if new_callees:
            print(f"[{steps}] + Harvested {len(new_callees)} static callees", flush=True)
        sync_shards_and_dispatch(linked)
        ok, res = run_host_cycle()
        dt_step = time.time() - t_step_start
        print(f"[{steps}] Result in {dt_step:.2f}s:", ok, res, flush=True)
        steps += 1
        if steps % 10 == 0:
            save_arm11_build_script(linked)

    save_arm11_build_script(linked)
