#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

// Small host-services boundary for functionality that must not leak Win32
// assumptions into runtime or game code. Guest execution, virtual memory, and
// cooperative contexts remain outside this layer until dedicated macOS
// prototypes establish a safe abstraction.
namespace RuntimePlatform {

std::optional<std::filesystem::path> ExecutableDirectory() noexcept;

// Returns the platform's conventional per-user application-data directory.
// It does not create the directory, leaving that policy to the caller.
std::filesystem::path ApplicationDataDirectory(std::string_view applicationName);

// The root for per-run diagnostics. Keeping this here ensures log placement
// follows the same host convention as configuration and other user data.
std::filesystem::path LogDirectory(std::string_view applicationName);

uint64_t CurrentProcessId() noexcept;

// Wait until a steady-clock deadline. Windows uses its high-resolution
// waitable timer when available; other supported hosts use the C++ runtime's
// monotonic wait implementation. The optional final spin is intentionally
// limited to callers with a measured frame-pacing need.
void SleepPreciselyUntil(std::chrono::steady_clock::time_point deadline,
                         bool finishWithSpin = false,
                         std::chrono::microseconds spinWindow = std::chrono::microseconds{750});

} // namespace RuntimePlatform
