#include "touch_pad.h"
#include "runtime_config.h"
#include "touch_art.h"
#include "settings_overlay.h"

#include <imgui.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_touch.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << " (" #condition ")" << std::endl; \
            return 1; \
        } \
    } while (false)

// Stubs for headless execution
namespace settings_overlay {
void ToggleTopBar() noexcept {}
bool TopBarVisible() noexcept { return false; }
bool FpsOverlayBounds(float*, float*, float*, float*) noexcept { return false; }
} // namespace settings_overlay

ImTextureID TouchArt::Get(const char*) { return ImTextureID{}; }

ImVec2 ImGui::CalcTextSize(const char*, const char*, bool, float) { return ImVec2(0, 0); }
ImDrawList* ImGui::GetBackgroundDrawList(ImGuiViewport*) { return nullptr; }
ImGuiIO::ImGuiIO() {}
void ImGui::MemFree(void* p) { std::free(p); }
ImGuiIO& ImGui::GetIO() { static ImGuiIO io{}; return io; }
void ImDrawList::AddCircleFilled(const ImVec2&, float, ImU32, int) {}
void ImDrawList::AddCircle(const ImVec2&, float, ImU32, int, float) {}
void ImDrawList::AddText(const ImVec2&, ImU32, const char*, const char*) {}
void ImDrawList::AddImage(ImTextureID, const ImVec2&, const ImVec2&, const ImVec2&, const ImVec2&, ImU32) {}

extern "C" {
SDL_JoystickID* SDL_GetGamepads(int* count) { if (count) *count = 0; return nullptr; }
SDL_TouchID* SDL_GetTouchDevices(int* count) { if (count) *count = 0; return nullptr; }
SDL_Finger** SDL_GetTouchFingers(SDL_TouchID, int* count) { if (count) *count = 0; return nullptr; }
void SDL_free(void*) {}
}

int main() {
    std::cout << "[test] Starting touch auto-accelerate tests..." << std::endl;

    // Isolate configuration path to a temporary directory so tests never touch real user config
    std::error_code ec;
    const auto tempDir = std::filesystem::temp_directory_path() / "mkw_touch_test_isolation";
    std::filesystem::create_directories(tempDir, ec);
    const auto tempConfig = tempDir / "Config.toml";
    std::filesystem::remove(tempConfig, ec);
    RuntimeConfigFile::SetConfigPathOverride(tempConfig);

    struct CleanupGuard {
        std::filesystem::path dir;
        ~CleanupGuard() {
            RuntimeConfigFile::SetConfigPathOverride({});
            std::error_code err;
            std::filesystem::remove_all(dir, err);
        }
    } cleanup{tempDir};

    // 1. Initial / default state
    TouchPad::Reset();
    TouchPad::SetAutoAccelerate(true);
    CHECK(TouchPad::AutoAccelerateEnabled());
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Default state verified." << std::endl;

    // 2. Normal tap (< 1000ms) does not lock
    TouchPad::UpdateAutoAccelerate(true, 100, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(true, 500, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 600, true);
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Short tap does not latch verified." << std::endl;

    // 3. Hold for 1000ms latches acceleration
    TouchPad::UpdateAutoAccelerate(true, 1000, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(true, 1500, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(true, 1999, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(true, 2000, true); // 1000ms elapsed
    CHECK(TouchPad::IsGasLocked());
    std::cout << "[test] 1-second hold locks acceleration verified." << std::endl;

    // 4. Releasing finger after latch retains lock
    TouchPad::UpdateAutoAccelerate(false, 2500, true);
    CHECK(TouchPad::IsGasLocked());
    std::cout << "[test] Finger lift retains lock verified." << std::endl;

    // 5. Tapping while locked unlocks acceleration
    TouchPad::UpdateAutoAccelerate(true, 3000, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 3100, true);
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Tap while locked unlocks acceleration verified." << std::endl;

    // 6. Holding the unlocking tap for > 1000ms does NOT immediately re-lock in same touch contact
    TouchPad::UpdateAutoAccelerate(true, 4000, true);
    TouchPad::UpdateAutoAccelerate(true, 5000, true);
    CHECK(TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 5100, true);
    CHECK(TouchPad::IsGasLocked());

    // Unlocking touch begins:
    TouchPad::UpdateAutoAccelerate(true, 6000, true);
    CHECK(!TouchPad::IsGasLocked());
    // Continuous hold past 1000ms:
    TouchPad::UpdateAutoAccelerate(true, 7500, true);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 7600, true);
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Unlocking tap cannot immediately re-lock in same gesture verified." << std::endl;

    // 7. Disabling auto-accelerate immediately clears active lock
    TouchPad::UpdateAutoAccelerate(true, 8000, true);
    TouchPad::UpdateAutoAccelerate(true, 9000, true);
    CHECK(TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 9100, true);
    CHECK(TouchPad::IsGasLocked());

    TouchPad::SetAutoAccelerate(false);
    CHECK(!TouchPad::AutoAccelerateEnabled());
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Disabling auto-accelerate cancels active lock verified." << std::endl;

    // 8. Holding A when disabled never locks
    TouchPad::UpdateAutoAccelerate(true, 10000, false);
    TouchPad::UpdateAutoAccelerate(true, 12000, false);
    CHECK(!TouchPad::IsGasLocked());
    TouchPad::UpdateAutoAccelerate(false, 12100, false);
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Holding A while disabled does not latch verified." << std::endl;

    // 9. Reset clears lock
    TouchPad::SetAutoAccelerate(true);
    TouchPad::UpdateAutoAccelerate(true, 13000, true);
    TouchPad::UpdateAutoAccelerate(true, 14000, true);
    CHECK(TouchPad::IsGasLocked());
    TouchPad::Reset();
    CHECK(!TouchPad::IsGasLocked());
    std::cout << "[test] Reset clears lock verified." << std::endl;

    // 10. Config persistence round-trip & stream parsing
    CHECK(RuntimeConfigFile::SetTouchAutoAccelerate(false));
    CHECK(!RuntimeConfigFile::TouchAutoAccelerate());
    {
        std::ifstream in(tempConfig);
        const auto parsed = RuntimeConfigFile::ParseConfig(in);
        CHECK(parsed.touchAutoAccelerate.has_value());
        CHECK(*parsed.touchAutoAccelerate == false);
    }

    CHECK(RuntimeConfigFile::SetTouchAutoAccelerate(true));
    CHECK(RuntimeConfigFile::TouchAutoAccelerate());
    {
        std::ifstream in(tempConfig);
        const auto parsed = RuntimeConfigFile::ParseConfig(in);
        CHECK(parsed.touchAutoAccelerate.has_value());
        CHECK(*parsed.touchAutoAccelerate == true);
    }

    // Direct in-memory stream validation
    {
        std::istringstream streamOn("[controller]\ntouch_auto_accelerate = true\n");
        const auto parsedOn = RuntimeConfigFile::ParseConfig(streamOn);
        CHECK(parsedOn.touchAutoAccelerate.has_value());
        CHECK(*parsedOn.touchAutoAccelerate == true);

        std::istringstream streamOff("[controller]\ntouch_auto_accelerate = false\n");
        const auto parsedOff = RuntimeConfigFile::ParseConfig(streamOff);
        CHECK(parsedOff.touchAutoAccelerate.has_value());
        CHECK(*parsedOff.touchAutoAccelerate == false);
    }
    std::cout << "[test] Config persistence round-trip and ParseConfig verified." << std::endl;

    std::cout << "[test] All touch auto-accelerate tests passed successfully!" << std::endl;
    return 0;
}
