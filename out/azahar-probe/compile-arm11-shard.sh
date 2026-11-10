#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -f runtime-overlay/guest_abi/memory_access.h ] && [ "${1:-}" = "--prepare-only" ]; then
    exit 0
fi
python3 prepare_runtime_probe.py
python3 prepare_paired_probe.py
python3 - <<'PY'
from pathlib import Path
import re

root = Path('runtime-overlay')

def replace(file, old, new):
    path = root / file
    text = path.read_text()
    assert old in text, file
    path.write_text(text.replace(old, new, 1))

replace('isa/ppc_isa_int.h', '#pragma once', '#pragma once\n#include <bit>')
replace('isa/ppc_isa_int.h', '__builtin_rotateleft32(value, shift)', 'std::rotl(value, static_cast<int>(shift & 31))')
replace('system_bridge.h', '#if defined(_WIN32)', '#if defined(_WIN32) || defined(__3DS__)')
replace('isa/ppc_isa_config.h', 'using MkwStateFreeResult2 = uint64_t __attribute__((vector_size(16)));', '''struct MkwStateFreeResult2 {
    uint64_t words[2];
    uint64_t operator[](unsigned index) const { return words[index]; }
};''')

# Fail explicitly if a probe reaches an unported quantized operation.
path = root / 'isa/ppc_isa_quantized.h'
source = path.read_text()
for name in ('PpcLoadPairPsqFloatBitsPackedInline', 'PpcStorePairPsqFloatBitsPackedInline',
             'PpcLoadPairPsqFloatFromHostInline', 'PpcQuantizePairPsqU8Scale61PackedInline'):
    pattern = rf'(?ms)^inline [^\n]*{name}\([^\n]*\n\{{.*?^\}}'
    match = re.search(pattern, source)
    assert match, name
    before, after = match[0].rsplit('#endif', 1)
    fallback = '__builtin_trap();'
    if name == 'PpcStorePairPsqFloatBitsPackedInline':
        fallback = ('return (static_cast<uint64_t>(PpcStorePsqFloatBitsInline(static_cast<uint32_t>(value >> 32))) << 32) '
                    '| PpcStorePsqFloatBitsInline(static_cast<uint32_t>(value));')
    source = source[:match.start()] + before + '#else\n    ' + fallback + '\n#endif' + after + source[match.end():]
path.write_text(source)

# These emitted RAM stores bypass RequiresCheckedAccess on desktop.
path = root / 'guest_abi/memory_access.h'
text = path.read_text()
for kind in ('8', '16', '32', 'Float32', 'Float64'):
    start = text.index('void FlatWriteRam' + kind + '(')
    start = text.index('{', start) + 1
    text = text[:start] + '\n    if constexpr (GuestFlat::RequiresCheckedAccess()) {\n        Memory::Write' + kind + '(address, value);\n        return;\n    }' + text[start:]
# Only inject the checked branch in the ARM11 overlay.
text = text.replace('if constexpr (GuestFlat::RequiresCheckedAccess())', 'if constexpr (true)')
path.write_text(text)
PY
if [ "${1:-}" = "--prepare-only" ]; then exit 0; fi
sdk=${DEVKITPRO:-/opt/devkitpro}
shard=shard_00110bb88c72c5589ccd8cd8
"$sdk/devkitARM/bin/arm-none-eabi-g++" -D__3DS__ -std=c++20 -O0 \
    -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -Iruntime-overlay -I../../generated -I../../runtime/include \
    -Werror=return-type -c "../../generated/build_shards/base_common/$shard.cpp" \
    -o "$shard.arm11.o" > shard-arm11.log 2>&1
