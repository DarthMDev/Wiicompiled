#!/usr/bin/env python3
"""Converge the linked-function set against the host harness.

Runs the macOS host build, and whenever it throws `Boot boundary: XXXXXXXX`, adds that function
(plus its transitive static callees) to linked-functions.txt, regenerates the shards/dispatch, and
repeats. Only stale shards are recompiled, so each iteration is fast.

Usage: python3 converge.py [max_iterations] [run_seconds]
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import advance  # noqa: E402

work = Path(__file__).resolve().parent
shards = work / 'host_shards'
log_path = work / 'pal-startup-host.log'
CFLAGS = ['-std=c++20', '-O0', '-Iruntime-overlay', '-I../../generated']


def stale_compile():
    changed = 0
    for unit in sorted(shards.glob('shard_*.cpp')) + [shards / 'dispatch.cpp']:
        obj = unit.with_suffix('.o')
        if not obj.exists() or unit.stat().st_mtime > obj.stat().st_mtime:
            res = subprocess.run(['clang++', *CFLAGS, '-c', str(unit), '-o', str(obj)],
                                 cwd=work, capture_output=True, text=True)
            if res.returncode != 0:
                print(f'compile {unit.name} failed:\n{res.stderr[:2000]}')
                raise SystemExit(1)
            changed += 1
    return changed


def link():
    objs = [str(shards / f'shard_{i}.o') for i in range(advance.NUM_SHARDS)]
    res = subprocess.run(
        ['clang++', '-std=c++20', '-O0', 'pal_startup_main_host.o',
         str(shards / 'dispatch.o'), *objs,
         'data_sections_init_host.o', 'data_sections_init_blobs_host.o',
         'pal_fst_host.o', 'ppc_quantized_host.o', '-o', 'pal-startup-host'],
        cwd=work, capture_output=True, text=True)
    if res.returncode != 0:
        print('link failed:\n', res.stderr[:2000])
        raise SystemExit(1)


def run_host(seconds):
    try:
        res = subprocess.run(['./pal-startup-host'], cwd=work, capture_output=True, timeout=seconds)
        text = (res.stdout + res.stderr).decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as exc:
        text = ((exc.stdout or b'') + (exc.stderr or b'')).decode('utf-8', 'replace')
    log_path.write_text(text)
    return text


def main():
    max_iter = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    run_secs = int(sys.argv[2]) if len(sys.argv) > 2 else 20

    linked = advance.read_linked_seed()
    linked_set = set(linked)
    print(f'seed: {len(linked)} functions', flush=True)

    for iteration in range(max_iter):
        changed = stale_compile()
        link()
        text = run_host(run_secs)
        frames = re.findall(r'Game Frame (\d+)', text)
        boundary = re.findall(r'Boot boundary: ([0-9A-Fa-f]{8})', text)
        models = len(re.findall(r'\[Model Ok\]', text))
        last_frame = frames[-1] if frames else '?'
        print(f'[{iteration}] recompiled={changed} frames={last_frame} modelOk={models} '
              f'boundary={boundary[-1] if boundary else "none"}', flush=True)
        if not boundary:
            print('converged: no boundary thrown', flush=True)
            break
        target = boundary[-1].upper()
        if target in linked_set:
            print(f'boundary {target} is already linked - stopping (real fault, not a missing link)',
                  flush=True)
            break
        if target not in advance.func_to_shard:
            print(f'boundary {target} has no recompiled source - stopping', flush=True)
            break
        linked.append(target)
        linked_set.add(target)
        added = advance.expand_transitive_callees([target], linked, linked_set)
        print(f'   + {target} (+{len(added)} callees) -> {len(linked)} linked', flush=True)
        advance.save_arm11_build_script(linked)
        advance.sync_shards_and_dispatch(linked)


if __name__ == '__main__':
    main()
