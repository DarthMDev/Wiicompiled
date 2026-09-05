#pragma once

#include <cstdint>

namespace RuntimePlatform::Web {

// Bump only when a browser shell and the public runtime no longer agree on
// their import/export contract. This bootstrap deliberately contains no game
// code or game-derived data.
inline constexpr uint32_t kPublicRuntimeAbiVersion = 1;

} // namespace RuntimePlatform::Web

extern "C" uint32_t MkwWebPublicRuntimeAbiVersion() noexcept;
