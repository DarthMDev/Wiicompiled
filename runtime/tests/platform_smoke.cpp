#include "platform/host_platform.h"

#include <chrono>
#include <iostream>

int main() {
    if (!RuntimePlatform::ExecutableDirectory()) {
        std::cerr << "unable to resolve the current executable directory\n";
        return 1;
    }

    const auto userData = RuntimePlatform::ApplicationDataDirectory("WiiCompiledPlatformSmoke");
    if (userData.filename() != "WiiCompiledPlatformSmoke") {
        std::cerr << "application-data directory lost its application name: " << userData << '\n';
        return 1;
    }
    if (RuntimePlatform::LogDirectory("WiiCompiledPlatformSmoke") != userData / "Logs") {
        std::cerr << "log directory is not derived from application data\n";
        return 1;
    }
#if defined(__APPLE__)
    if (userData.parent_path().filename() != "Application Support" ||
        userData.parent_path().parent_path().filename() != "Library") {
        std::cerr << "macOS application-data directory is not under ~/Library/Application Support: "
                  << userData << '\n';
        return 1;
    }
#endif

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    RuntimePlatform::SleepPreciselyUntil(deadline);
    if (std::chrono::steady_clock::now() < deadline) {
        std::cerr << "deadline wait returned early\n";
        return 1;
    }
    return 0;
}
