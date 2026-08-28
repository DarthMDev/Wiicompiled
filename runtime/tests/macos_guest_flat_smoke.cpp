#include "guest_flat_memory.h"

#include <cstdint>
#include <iostream>

int main() {
    GuestFlat::Initialize({
        {0x00000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x80000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x10000000u, 0x4000u, GuestFlat::Backing::Mem2},
        {0x90000000u, 0x4000u, GuestFlat::Backing::Mem2},
    });
    auto* mem1Physical = GuestFlat::HostPointer(0x00000000u);
    auto* mem1Cached = GuestFlat::HostPointer(0x80000000u);
    auto* mem2Physical = GuestFlat::HostPointer(0x10000000u);
    auto* mem2Cached = GuestFlat::HostPointer(0x90000000u);
    if (!mem1Physical || !mem1Cached || !mem2Physical || !mem2Cached) {
        std::cerr << "missing host alias\n";
        return 1;
    }
    mem1Physical[7] = 0x5a;
    mem2Cached[9] = 0xa5;
    const auto* guest = reinterpret_cast<const uint8_t*>(GuestFlat::kFixedFlatGuestBase);
    if (mem1Cached[7] != 0x5a || guest[0x80000007u] != 0x5a ||
        mem2Physical[9] != 0xa5 || guest[0x10000009u] != 0xa5) {
        std::cerr << "guest aliases are not coherent\n";
        return 1;
    }
    return 0;
}
