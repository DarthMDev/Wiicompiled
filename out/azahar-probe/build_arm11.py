import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

work_dir = Path(__file__).resolve().parent
repo_dir = work_dir.parents[1]
shards_dir = work_dir / 'host_shards'

sdk = os.environ.get('DEVKITPRO', '/opt/devkitpro')
gxx = Path(sdk) / 'devkitARM/bin/arm-none-eabi-g++'
strip = Path(sdk) / 'devkitARM/bin/arm-none-eabi-strip'
tool3dsx = Path(sdk) / 'tools/bin/3dsxtool'

if not gxx.exists():
    print(f"Error: devkitARM compiler not found at {gxx}", file=sys.stderr)
    sys.exit(1)

common_flags = [
    # -O2 instead of -Os: the guest CPU is recompiled C++, so instruction-level performance of the
    # shards dominates menu frame times.  MKW_ARM11_OPT lets us fall back to -Os if code size (and
    # therefore the RAM the binary takes from the app region) becomes a problem.
    '-D__3DS__', '-std=c++20', os.environ.get('MKW_ARM11_OPT', '-Os'), '-g0',
    '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
    '-Iruntime-overlay', '-I../../generated',
    f'-I{sdk}/libctru/include'
]

# Per-shard optimisation.  -O2 for the whole image costs +4.1 MiB of binary, which is heap the
# texture cache and the audio ring need, so instead MKW_HOT_SHARDS lists the shard indices that own
# the hottest guest targets (see "prof=" in [Beat]) and only those are rebuilt at -O2.
#  MKW_HOT_SHARDS=0,4,8 MKW_HOT_OPT=-O2 python3 out/azahar-probe/build_arm11.py
hot_shards = {int(x) for x in os.environ.get('MKW_HOT_SHARDS', '').replace(' ', '').split(',') if x.isdigit()}
hot_opt = os.environ.get('MKW_HOT_OPT', '-O2')

def hot_flags_for(src):
    m = re.fullmatch(r'shard_(\d+)\.cpp', src.name)
    if m and int(m.group(1)) in hot_shards:
        return [hot_opt]
    return []

def compile_src(src, obj):
    cmd = [str(gxx)] + common_flags + hot_flags_for(src) + ['-c', str(src), '-o', str(obj)]
    t0 = time.time()
    res = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True)
    dt = time.time() - t0
    if res.returncode != 0:
        return False, src.name, dt, res.stderr
    return True, src.name, dt, ''

def main():
    print(f"Using devkitARM at {sdk}")
    if hot_shards:
        print(f"Hot shards at {hot_opt}: {sorted(hot_shards)}")
    t_start = time.time()

    # Base sources
    tasks = [
        (work_dir / 'pal_startup_main.cpp', work_dir / 'pal_startup_main.arm11.o'),
        (repo_dir / 'generated/data_sections_init.cpp', work_dir / 'data_sections_init.arm11.o'),
        (repo_dir / 'runtime/src/ppc_quantized.cpp', work_dir / 'ppc_quantized.arm11.o'),
        (work_dir / 'pal_sections.S', work_dir / 'pal_sections.arm11.o'),
        (shards_dir / 'dispatch.cpp', shards_dir / 'dispatch.arm11.o')
    ]

    # Shards
    for i in range(24):
        src = shards_dir / f'shard_{i}.cpp'
        if src.exists():
            tasks.append((src, shards_dir / f'shard_{i}.arm11.o'))

    overlay_dir = work_dir / 'runtime-overlay'
    overlay_mtime = max(
        (p.stat().st_mtime for p in overlay_dir.rglob('*') if p.is_file()),
        default=0
    ) if overlay_dir.exists() else 0

    force_clean = '--clean' in sys.argv or '-f' in sys.argv

    # Shard objects carry whatever hot set was configured last time; rebuild them all when that set
    # changes, otherwise a stale -Os object silently survives a new MKW_HOT_SHARDS.
    hot_stamp = work_dir / '.hot_shards'
    stamp_text = ','.join(str(i) for i in sorted(hot_shards)) + '|' + hot_opt
    previous_stamp = hot_stamp.read_text().strip() if hot_stamp.exists() else ''
    if previous_stamp != stamp_text:
        force_clean = True
        hot_stamp.write_text(stamp_text)

    to_compile = []
    for src, obj in tasks:
        needs_build = force_clean or not obj.exists() or src.stat().st_mtime > obj.stat().st_mtime or overlay_mtime > obj.stat().st_mtime
        if needs_build:
            to_compile.append((src, obj))
        else:
            print(f"  Reusing cached {obj.name}")

    if to_compile:
        workers = min(len(to_compile), max(1, int(os.environ.get('MKW_BUILD_JOBS', '4'))))
        print(f"Compiling {len(to_compile)} compilation units in parallel across {workers} workers...")
        with ThreadPoolExecutor(max_workers=workers) as ex:
            futures = [ex.submit(compile_src, src, obj) for src, obj in to_compile]
            results = [f.result() for f in futures]

        failed = False
        for ok, name, dt, err in results:
            if not ok:
                print(f"FAILED: {name} in {dt:.2f}s:\n{err}", file=sys.stderr)
                failed = True
            else:
                print(f"  Compiled {name} in {dt:.2f}s")

        if failed:
            sys.exit(1)

    print(f"All compilation units compiled in {time.time() - t_start:.2f}s.")

    # Link elf
    elf = work_dir / 'wiicompiled-pal-startup.elf'
    target_3dsx = work_dir / 'wiicompiled-pal-startup.3dsx'

    objs = [obj for _, obj in tasks]
    link_cmd = [
        str(gxx), '-specs=3dsx.specs',
        # The title-screen model/scene code recurses deeper than the 32 KiB default stack.
        # Give the app a 1 MiB stack so it does not die with an opaque data abort.
        '-Wl,--defsym=__stack_size=0x100000',
        '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
        f'-L{sdk}/libctru/lib',
        *[str(o) for o in objs],
        # citro3d is the PICA200 wrapper used by the 2D UI backend (gx_bridge).
        '-lcitro3d', '-lctru', '-lm',
        '-o', str(elf)
    ]

    print("Linking wiicompiled-pal-startup.elf...")
    t0 = time.time()
    res = subprocess.run(link_cmd, cwd=work_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Link failed:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
    print(f"Linked in {time.time() - t0:.2f}s (size: {elf.stat().st_size / 1024 / 1024:.2f} MiB)")

    # 3dsxtool
    print("Building 3dsx package...")
    tool_args = [str(tool3dsx), str(elf), str(target_3dsx)]
    res = subprocess.run(tool_args, cwd=work_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"3dsxtool failed:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)

    print(f"SUCCESS: {target_3dsx} built! Size: {target_3dsx.stat().st_size / 1024 / 1024:.2f} MiB")
    print(f"Total ARM11 build time: {time.time() - t_start:.2f}s")

if __name__ == '__main__':
    main()
