#pragma once

#include <atomic>
#include <cstdint>

#define MKW_RESTRICT __restrict
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#else
#error "ppc_isa_config.h has no SIMD intrinsics header for this architecture"
#endif

inline constexpr bool MkwStateFreeAbiEnabled(uint32_t) noexcept
{
    return true;
}

#if defined(_MSC_VER) || defined(__MINGW32__)
#define MKW_PPC_FORCE_INLINE __forceinline
#define MKW_PPC_NO_INLINE __declspec(noinline)
#else
#define MKW_PPC_FORCE_INLINE inline __attribute__((always_inline))
#define MKW_PPC_NO_INLINE __attribute__((noinline))
#endif
#define MKW_PPC_ALWAYS_INLINE_BODY __attribute__((always_inline))
#define MKW_PPC_COLD __attribute__((cold))
#define MKW_PPC_INTERNAL_CALL __regcall


using MkwStateFreeResult2 = uint64_t __attribute__((ext_vector_type(2)));
