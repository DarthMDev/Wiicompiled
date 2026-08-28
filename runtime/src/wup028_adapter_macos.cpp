#include "wup028_adapter.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <dolphin/pad.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace Wup028Adapter {
namespace {

// WUP-028 uses this report protocol on macOS too, where it is exposed as HID.
constexpr uint16_t kNintendoVendor = 0x057e;
constexpr uint16_t kAdapterProduct = 0x0337;
constexpr size_t kReportSize = 37;

std::mutex g_mutex;
std::array<PADStatus, PAD_CHANMAX> g_statuses{};
std::array<uint8_t, PAD_CHANMAX> g_rumble{};
std::array<int8_t, PAD_CHANMAX> g_portAssignments{{-1, -1, -1, -1}};
std::thread g_worker;
std::atomic_bool g_stop{false};
std::atomic_bool g_running{false};
std::atomic_bool g_connected{false};
AdapterInfo g_info;

struct HidState {
    IOHIDManagerRef manager = nullptr;
    IOHIDDeviceRef device = nullptr;
    std::array<uint8_t, PAD_CHANMAX> sentRumble{};
    std::array<bool, PAD_CHANMAX> reportedPorts{};
    std::chrono::steady_clock::time_point rateStart{};
    uint32_t rateReports = 0;
};

int8_t Axis(uint8_t raw)
{
    constexpr int kCenter = 128;
    constexpr int kTolerance = 10;
    if (raw >= kCenter - kTolerance && raw <= kCenter + kTolerance) return 0;
    return static_cast<int8_t>(std::clamp(static_cast<int>(raw) - kCenter, -128, 127));
}

PADStatus DecodePort(const uint8_t* p)
{
    PADStatus out{};
    // 1 is wired and 2 is a WaveBird receiver. Do not discard the latter.
    if ((p[0] & 0x30) == 0) {
        out.err = PAD_ERR_NO_CONTROLLER;
        return out;
    }
    if (p[1] & 0x01) out.button |= PAD_BUTTON_A;
    if (p[1] & 0x02) out.button |= PAD_BUTTON_B;
    if (p[1] & 0x04) out.button |= PAD_BUTTON_X;
    if (p[1] & 0x08) out.button |= PAD_BUTTON_Y;
    if (p[1] & 0x10) out.button |= PAD_BUTTON_LEFT;
    if (p[1] & 0x20) out.button |= PAD_BUTTON_RIGHT;
    if (p[1] & 0x40) out.button |= PAD_BUTTON_DOWN;
    if (p[1] & 0x80) out.button |= PAD_BUTTON_UP;
    if (p[2] & 0x01) out.button |= PAD_BUTTON_START;
    if (p[2] & 0x02) out.button |= PAD_TRIGGER_Z;
    if (p[2] & 0x04) out.button |= PAD_TRIGGER_R;
    if (p[2] & 0x08) out.button |= PAD_TRIGGER_L;
    out.stickX = Axis(p[3]);
    out.stickY = Axis(p[4]);
    out.substickX = Axis(p[5]);
    out.substickY = Axis(p[6]);
    out.triggerL = p[7];
    out.triggerR = p[8];
    out.err = PAD_ERR_NONE;
    return out;
}

std::string DeviceName(IOHIDDeviceRef device)
{
    const auto value = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return "WUP-028-compatible adapter";
    char name[256]{};
    return CFStringGetCString(static_cast<CFStringRef>(value), name, sizeof(name), kCFStringEncodingUTF8)
               ? name
               : "WUP-028-compatible adapter";
}

void SendInputEnable(HidState& state)
{
    if (state.device == nullptr) return;
    const uint8_t command = 0x13;
    IOHIDDeviceSetReport(state.device, kIOHIDReportTypeOutput, 0, &command, sizeof(command));
}

bool SendRumble(HidState& state, const std::array<uint8_t, PAD_CHANMAX>& motors)
{
    if (state.device == nullptr) return false;
    const std::array<uint8_t, 5> report{{0x11, motors[0], motors[1], motors[2], motors[3]}};
    return IOHIDDeviceSetReport(state.device, kIOHIDReportTypeOutput, 0, report.data(), report.size()) == kIOReturnSuccess;
}

void ClearPorts(const char* reason)
{
    std::lock_guard lock(g_mutex);
    g_rumble.fill(0);
    for (size_t port = 0; port < g_statuses.size(); ++port) {
        if (g_info.ports[port]) {
            g_info.ports[port] = false;
            ++g_info.portChangeSequence[port];
            RT_LOG(RT_TAG_RUNTIME) << "GameCube adapter port " << (port + 1)
                                   << " controller disconnected (" << reason << ")" << std::endl;
        }
        g_statuses[port] = {};
        g_statuses[port].err = PAD_ERR_NO_CONTROLLER;
        g_info.portStatus[port] = 0;
    }
}

void HandleInputReport(void* context, IOReturn result, void*, IOHIDReportType,
                       uint32_t, uint8_t* report, CFIndex reportLength)
{
    if (result != kIOReturnSuccess || reportLength != static_cast<CFIndex>(kReportSize) || report[0] != 0x21) return;
    auto& state = *static_cast<HidState*>(context);
    std::array<PADStatus, PAD_CHANMAX> decoded{};
    for (size_t port = 0; port < decoded.size(); ++port) decoded[port] = DecodePort(report + 1 + port * 9);

    std::array<int8_t, PAD_CHANMAX> transitions{};
    {
        std::lock_guard lock(g_mutex);
        g_statuses = decoded;
        ++state.rateReports;
        for (size_t port = 0; port < decoded.size(); ++port) {
            g_info.portStatus[port] = report[1 + port * 9];
            const bool present = decoded[port].err == PAD_ERR_NONE;
            if (present != state.reportedPorts[port]) {
                state.reportedPorts[port] = present;
                g_info.ports[port] = present;
                ++g_info.portChangeSequence[port];
                transitions[port] = present ? 1 : -1;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const float seconds = std::chrono::duration<float>(now - state.rateStart).count();
        if (seconds >= 1.0f) {
            g_info.pollRateHz = static_cast<float>(state.rateReports) / seconds;
            state.rateReports = 0;
            state.rateStart = now;
        }
    }
    for (size_t port = 0; port < transitions.size(); ++port) {
        if (transitions[port] != 0) {
            RT_LOG(RT_TAG_RUNTIME) << "GameCube adapter port " << (port + 1) << " controller "
                                   << (transitions[port] > 0 ? "connected" : "disconnected") << std::endl;
        }
    }
}

void DeviceMatched(void* context, IOReturn result, void*, IOHIDDeviceRef device)
{
    if (result != kIOReturnSuccess) return;
    auto& state = *static_cast<HidState*>(context);
    if (state.device != nullptr) return;
    if (IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        std::lock_guard lock(g_mutex);
        g_info.state = ConnectionState::DriverError;
        g_info.detail = "macOS could not open the adapter (another app may own it)";
        return;
    }
    state.device = device;
    CFRetain(device);
    state.rateStart = std::chrono::steady_clock::now();
    state.rateReports = 0;
    state.sentRumble.fill(0);
    state.reportedPorts.fill(false);
    g_connected.store(true, std::memory_order_release);
    {
        std::lock_guard lock(g_mutex);
        g_info.state = ConnectionState::Connected;
        g_info.deviceName = DeviceName(device);
        g_info.detail = "Receiving native GameCube reports through macOS HID";
        // HID does not publish USB endpoint addresses as WinUSB does.
        g_info.inputEndpoint = 0;
        g_info.outputEndpoint = 0;
    }
    RT_LOG(RT_TAG_RUNTIME) << DeviceName(device) << " connected through macOS HID" << std::endl;
    SendInputEnable(state);
}

void DeviceRemoved(void* context, IOReturn, void*, IOHIDDeviceRef device)
{
    auto& state = *static_cast<HidState*>(context);
    if (state.device != device) return;
    SendRumble(state, {});
    IOHIDDeviceClose(state.device, kIOHIDOptionsTypeNone);
    CFRelease(state.device);
    state.device = nullptr;
    g_connected.store(false, std::memory_order_release);
    ClearPorts("adapter unavailable");
    std::lock_guard lock(g_mutex);
    g_info.state = ConnectionState::Searching;
    g_info.detail = "Adapter disconnected; waiting for reconnect";
    g_info.pollRateHz = 0.0f;
}

CFMutableDictionaryRef AdapterMatchDictionary()
{
    auto* match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const int vendor = kNintendoVendor;
    const int product = kAdapterProduct;
    auto* vendorNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    auto* productNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), vendorNumber);
    CFDictionarySetValue(match, CFSTR(kIOHIDProductIDKey), productNumber);
    CFRelease(vendorNumber);
    CFRelease(productNumber);
    return match;
}

void Worker()
{
    HidState state;
    state.manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (state.manager == nullptr) {
        std::lock_guard lock(g_mutex);
        g_info.state = ConnectionState::DriverError;
        g_info.detail = "macOS could not create a HID manager";
        return;
    }
    auto* match = AdapterMatchDictionary();
    IOHIDManagerSetDeviceMatching(state.manager, match);
    CFRelease(match);
    IOHIDManagerRegisterDeviceMatchingCallback(state.manager, DeviceMatched, &state);
    IOHIDManagerRegisterDeviceRemovalCallback(state.manager, DeviceRemoved, &state);
    IOHIDManagerRegisterInputReportCallback(state.manager, HandleInputReport, &state);
    IOHIDManagerScheduleWithRunLoop(state.manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    if (IOHIDManagerOpen(state.manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        std::lock_guard lock(g_mutex);
        g_info.state = ConnectionState::DriverError;
        g_info.detail = "macOS could not start HID monitoring";
    } else {
        std::lock_guard lock(g_mutex);
        g_info.state = ConnectionState::Searching;
        g_info.detail = "Waiting for a VID 057E / PID 0337 adapter";
    }

    auto nextEnable = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_acquire)) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        if (state.device == nullptr) continue;
        std::array<uint8_t, PAD_CHANMAX> desired{};
        {
            std::lock_guard lock(g_mutex);
            desired = g_rumble;
        }
        if (desired != state.sentRumble && SendRumble(state, desired)) state.sentRumble = desired;
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextEnable) {
            SendInputEnable(state);
            nextEnable = now + std::chrono::seconds(1);
        }
    }

    if (state.device != nullptr) {
        SendRumble(state, {});
        IOHIDDeviceClose(state.device, kIOHIDOptionsTypeNone);
        CFRelease(state.device);
    }
    IOHIDManagerUnscheduleFromRunLoop(state.manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(state.manager, kIOHIDOptionsTypeNone);
    CFRelease(state.manager);
    g_connected.store(false, std::memory_order_release);
    ClearPorts("adapter stopped");
}

} // namespace

void Initialize()
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    for (auto& status : g_statuses) status.err = PAD_ERR_NO_CONTROLLER;
    for (size_t gamePort = 0; gamePort < g_portAssignments.size(); ++gamePort) {
        g_portAssignments[gamePort] = static_cast<int8_t>(RuntimeConfigFile::GameCubeAdapterPort(gamePort));
    }
    g_stop.store(false, std::memory_order_release);
    g_worker = std::thread(Worker);
}

void Shutdown()
{
    if (!g_running.exchange(false)) return;
    g_stop.store(true, std::memory_order_release);
    if (g_worker.joinable()) g_worker.join();
    g_connected.store(false, std::memory_order_release);
}

bool Read(std::array<PADStatus, 4>& statuses)
{
    if (!g_connected.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(g_mutex);
    for (auto& status : statuses) status.err = PAD_ERR_NO_CONTROLLER;
    for (size_t gamePort = 0; gamePort < statuses.size(); ++gamePort) {
        const int physicalPort = g_portAssignments[gamePort];
        if (physicalPort >= 0) statuses[gamePort] = g_statuses[static_cast<size_t>(physicalPort)];
    }
    return true;
}

void SetPortAssignment(uint32_t gamePort, int physicalPort)
{
    if (gamePort >= g_portAssignments.size() || physicalPort < -1 || physicalPort >= PAD_CHANMAX) return;
    std::lock_guard lock(g_mutex);
    const int oldPhysicalPort = g_portAssignments[gamePort];
    if (oldPhysicalPort >= 0) g_rumble[static_cast<size_t>(oldPhysicalPort)] = 0;
    if (physicalPort >= 0) {
        for (auto& assignment : g_portAssignments) {
            if (assignment == physicalPort) {
                assignment = -1;
                g_rumble[static_cast<size_t>(physicalPort)] = 0;
            }
        }
    }
    g_portAssignments[gamePort] = static_cast<int8_t>(physicalPort);
}

int GetPortAssignment(uint32_t gamePort)
{
    if (gamePort >= g_portAssignments.size()) return -1;
    std::lock_guard lock(g_mutex);
    return g_portAssignments[gamePort];
}

bool SetRumble(uint32_t port, bool enabled)
{
    if (port >= g_rumble.size() || !g_connected.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(g_mutex);
    const int physicalPort = g_portAssignments[port];
    if (physicalPort < 0) return false;
    const size_t adapterPort = static_cast<size_t>(physicalPort);
    if (g_statuses[adapterPort].err != PAD_ERR_NONE) {
        g_rumble[adapterPort] = 0;
        return false;
    }
    g_rumble[adapterPort] = enabled ? 1 : 0;
    return true;
}

AdapterInfo GetInfo()
{
    std::lock_guard lock(g_mutex);
    return g_info;
}

} // namespace Wup028Adapter
