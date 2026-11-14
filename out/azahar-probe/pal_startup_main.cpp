#ifdef __3DS__
#include <3ds.h>
#include <citro3d.h>
// Pass-through vertex shader for the 2D layout pass, compiled from
// out/azahar-probe/runtime-overlay/gpu2d.v.pica. Its four attributes follow the native renderer's
// packed Vertex layout: float3 position, float3 normal, float4 UVs, byte4 color. Positions are
// pre-projected on the CPU; byte colors are normalized by 1/255 in the shader.
alignas(4) static const uint8_t kGpu2dShader[] = {
    68, 86, 76, 66, 1, 0, 0, 0, 104, 0, 0, 0,
    68, 86, 76, 80, 0, 0, 0, 0, 40, 0, 0, 0,
    5, 0, 0, 0, 60, 0, 0, 0, 4, 0, 0, 0,
    92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 76, 1, 240, 7, 76,
    2, 32, 32, 76, 131, 225, 71, 32, 0, 0, 0, 136,
    78, 3, 0, 0, 0, 0, 0, 0, 161, 10, 0, 0,
    0, 0, 0, 0, 172, 2, 0, 0, 0, 0, 0, 0,
    15, 192, 6, 0, 0, 0, 0, 0, 68, 86, 76, 69,
    2, 16, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0,
    0, 0, 7, 0, 0, 0, 0, 0, 64, 0, 0, 0,
    2, 0, 0, 0, 104, 0, 0, 0, 0, 0, 0, 0,
    104, 0, 0, 0, 3, 0, 0, 0, 128, 0, 0, 0,
    0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0,
    2, 0, 95, 0, 0, 0, 0, 0, 0, 0, 63, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 94, 0,
    1, 1, 55, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0,
    3, 0, 1, 0, 15, 0, 0, 0, 2, 0, 2, 0,
    15, 0, 0, 0,
};
static_assert(sizeof(kGpu2dShader) % 4 == 0, "shader binary must be word aligned");
extern "C" {
    // The PICA200 backend keeps the decoded UI textures in VRAM (the linear heap).  The menu set
    // is ~1.8 MiB once packed as RGB565/RGBA4 with power-of-two padding, so give it headroom over
    // the old 2 MiB; the framebuffers and the command buffer come out of the same heap.
    u32 __ctru_linear_heap_size = 4 * 1024 * 1024;
}
#endif
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <set>

static uint32_t* g_padCallCounts = nullptr;
#include <strings.h>
#include <bit>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "RuntimeConfig.h"
#include "ppc_runtime.h"
#include "timebase_contract.h"

struct BootBoundary { uint32_t address; };
uint32_t run_guest_boot(CpuContext& cpu);
void InvokeIndirectCpu(uint32_t address, CpuContext* cpu);
extern "C" void InitializeDataSections();
extern "C" const uint8_t kStartupFst[], kStartupFstEnd[];

// lwbrx / stwbrx helpers. Declared in isa/ppc_isa_int.h but never defined in this build; they were
// not needed until the UI/layout cluster pulled in code that uses them.
static inline uint32_t ByteSwap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
extern "C" uint32_t PPC_LoadWordByteReverse(uint32_t addr) {
    return ByteSwap32(Memory::Read32(addr));
}
extern "C" void PPC_StoreWordByteReverse(uint32_t addr, uint32_t value) {
    Memory::Write32(addr, ByteSwap32(value));
}
extern "C" uint32_t PPC_LoadHalfwordByteReverse(uint32_t addr) {
    const uint32_t v = Memory::Read16(addr);
    return ((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu);
}
extern "C" void PPC_StoreHalfwordByteReverse(uint32_t addr, uint32_t value) {
    Memory::Write16(addr, static_cast<uint16_t>(((value & 0xFFu) << 8) | ((value >> 8) & 0xFFu)));
}

extern "C" void func_800841B0(CpuContext*);
extern "C" void func_805411FC(CpuContext*);
extern "C" void func_805A8FBC(CpuContext*);
extern "C" void func_805A900C(CpuContext*);
extern "C" void func_8052A954(CpuContext*);
extern "C" void func_80540038(CpuContext*);
extern "C" void func_805B9344(CpuContext*);
extern "C" void func_805E889C(CpuContext*);
extern "C" void func_80631588(CpuContext*);
extern "C" void func_8051BC78(CpuContext*);
extern "C" void func_80602530(CpuContext*);
extern "C" void func_80013040(CpuContext*);
extern "C" void func_800135F0(CpuContext*);
extern "C" void func_80631C10(CpuContext*);
extern "C" void func_805A08F0(CpuContext*);
extern "C" void func_805E78F0(CpuContext*);
extern "C" void func_80577064(CpuContext*);
extern "C" void func_805A0A1C(CpuContext*);
extern "C" void func_805A8468(CpuContext*);
extern "C" void func_80124894(CpuContext*);
extern "C" void func_8020FA80(CpuContext*);
extern "C" void func_8004C240(CpuContext*);
extern "C" void func_8004C2B0(CpuContext*);
extern "C" void func_8004C350(CpuContext*);
extern "C" void func_80070D20(CpuContext*);
extern "C" void func_80073470(CpuContext*);
extern "C" void func_8006EB60(CpuContext*);
extern "C" void func_8004C5D0(CpuContext*);
extern "C" void func_8004C780(CpuContext*);
extern "C" void func_8004C7F0(CpuContext*);
extern "C" void func_8004C900(CpuContext*);

static constexpr uint32_t guestBase = 0x80000000u;
static constexpr size_t guestSize = 0x01800000u;
static uint8_t* guestRam;
static constexpr uint32_t mem2Base = 0x90000000u;
static size_t mem2Size = 48u * 1024 * 1024; // Wii MEM2 is 64 MiB; the title UI archive needs
                                             // ~12 MiB of free MEM2 on top of the menu heaps.
                                             // ARM11 falls back to 28/24/20 MiB.
static uint8_t* mem2Ram;
static constexpr uint32_t kRaceSzsBase = 0x95000000u;
static constexpr size_t kRaceSzsSize = 6u * 1024 * 1024;
static uint8_t* g_raceSzsRam = nullptr;
static constexpr uint32_t kLockedCacheBase = 0xE0000000u;
alignas(32) static uint8_t g_lockedCache[16 * 1024];
static uint32_t g_lockedCacheDmaUpper = 0;

#ifdef __3DS__
#include <malloc.h>
// Host heap headroom.  The port maps guest MEM1+MEM2 with calloc from this same heap, so on an Old
// 3DS there is very little left; anything that allocates at runtime (texture cache, NAND buffers)
// silently aborts the app when this reaches zero.  Logging it makes that visible.
static size_t HostHeapFreeKb() {
    struct mallinfo mi = mallinfo();
    return static_cast<size_t>(mi.fordblks) / 1024;
}
#else
static size_t HostHeapFreeKb() { return 0; }
#endif

#ifdef __3DS__
// Logging on hardware used to do two fflush()es per line (one per stream), i.e. two SD-card round
// trips per line at ~13 lines per frame.  That alone cost hundreds of milliseconds per frame.
// Buffer in RAM and push to the log file when the buffer fills or once a second.
static char g_appLogBuf[48 * 1024];
static size_t g_appLogLen = 0;
static void AppLogFlush() {
    if (g_appLogLen == 0) return;
    std::fwrite(g_appLogBuf, 1, g_appLogLen, stderr);
    std::fflush(stderr);
    g_appLogLen = 0;
}
#endif

static void AppLog(const char* fmt, ...) {
#ifdef __3DS__
    va_list args;
    va_start(args, fmt);
    const size_t space = sizeof(g_appLogBuf) - g_appLogLen;
    const int n = std::vsnprintf(g_appLogBuf + g_appLogLen, space, fmt, args);
    va_end(args);
    if (n > 0) {
        const size_t wrote = static_cast<size_t>(n) < space ? static_cast<size_t>(n) : space - 1;
        g_appLogLen += wrote;
        if (g_appLogLen > sizeof(g_appLogBuf) - 512) AppLogFlush();
    }
#else
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    std::fflush(stdout);
    va_end(args);

    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fflush(stderr);
    va_end(args);
#endif
}

static uint8_t* resolve_region(uint8_t* ram, uint32_t base, size_t size,
                               uint32_t address, size_t length)
{
    if (!ram || address < base || address - base > size || length > size - (address - base))
        return nullptr;
    return ram + (address - base);
}

// Lazily back the race SZS staging buffer: only the force-race shortcut touches this 6 MiB region,
// so the menu path never pays for it.
static bool ensure_race_szs_ram()
{
    if (g_raceSzsRam) return true;
    g_raceSzsRam = static_cast<uint8_t*>(std::calloc(1, kRaceSzsSize));
    if (!g_raceSzsRam) {
        AppLog("[Mem] race SZS buffer (%u MiB) allocation failed, host heap free %zuKB\n",
            (unsigned)(kRaceSzsSize / (1024 * 1024)), HostHeapFreeKb());
        return false;
    }
    const uintptr_t raceBias = reinterpret_cast<uintptr_t>(g_raceSzsRam) - kRaceSzsBase + 1;
    for (size_t offset = 0; offset < kRaceSzsSize; offset += MemoryInline::kPageSize) {
        const auto page = (kRaceSzsBase + offset) >> MemoryInline::kPageShift;
        MemoryInline::g_fullPageBias[page] = raceBias;
        MemoryInline::g_fullReadablePageBias[page] = raceBias;
        MemoryInline::g_fullWritablePageBias[page] = raceBias;
    }
    AppLog("[Mem] race SZS buffer committed on first use (%u MiB, host heap free %zuM)\n",
        (unsigned)(kRaceSzsSize / (1024 * 1024)), HostHeapFreeKb() / 1024);
    return true;
}

static uint8_t* resolve_guest(uint32_t address, size_t length)
{
    // Physical MEM1 (0x00000000 - 0x01800000)
    if (address < 0x01800000u)
        return resolve_region(guestRam, 0x00000000u, 0x01800000u, address, length);
    // Cached MEM1 (0x80000000 - 0x81800000)
    if (address >= 0x80000000u && address < 0x81800000u)
        return resolve_region(guestRam, 0x80000000u, 0x01800000u, address, length);
    // Uncached MEM1 (0xC0000000 - 0xC1800000)
    if (address >= 0xC0000000u && address < 0xC1800000u)
        return resolve_region(guestRam, 0xC0000000u, 0x01800000u, address, length);

    // Physical MEM2 (0x10000000)
    if (address >= 0x10000000u && address < 0x10000000u + mem2Size)
        return resolve_region(mem2Ram, 0x10000000u, mem2Size, address, length);
    // Cached MEM2 (0x90000000)
    if (address >= 0x90000000u && address < 0x90000000u + mem2Size)
        return resolve_region(mem2Ram, 0x90000000u, mem2Size, address, length);
    // Extended/Alias MEM2 (0xA0000000)
    if (address >= 0xA0000000u && address < 0xA0000000u + mem2Size)
        return resolve_region(mem2Ram, 0xA0000000u, mem2Size, address, length);
    // Uncached MEM2 (0xD0000000)
    if (address >= 0xD0000000u && address < 0xD0000000u + mem2Size)
        return resolve_region(mem2Ram, 0xD0000000u, mem2Size, address, length);

    if (address >= kLockedCacheBase && address < kLockedCacheBase + sizeof(g_lockedCache))
        return resolve_region(g_lockedCache, kLockedCacheBase, sizeof(g_lockedCache), address, length);

    // Race SZS buffer (0x95000000 - 0x95600000) and physical alias (0x15000000 - 0x15600000).
    // The 6 MiB backing store is only needed by the force-race shortcut; the menu path never touches
    // it, so it is allocated on first access instead of at boot (an Old 3DS has no host heap to
    // spare once MEM1 + MEM2 are mapped).
    if (address >= kRaceSzsBase && address < kRaceSzsBase + kRaceSzsSize) {
        if (!ensure_race_szs_ram()) return nullptr;
        return resolve_region(g_raceSzsRam, kRaceSzsBase, kRaceSzsSize, address, length);
    }
    if (address >= 0x15000000u && address < 0x15000000u + kRaceSzsSize) {
        if (!ensure_race_szs_ram()) return nullptr;
        return resolve_region(g_raceSzsRam, 0x15000000u, kRaceSzsSize, address, length);
    }

    return nullptr;
}

bool Memory::Contains(uint32_t address, size_t length) {
    return resolve_guest(address, length) != nullptr;
}

uint8_t* Memory::GetPointer(uint32_t address, size_t length) {
    if (auto* pointer = resolve_guest(address, length))
        return pointer;
    std::fprintf(stderr, "Memory::GetPointer FAILED for address 0x%08X (size %zu) at PC %08X, LR %08X\n",
        address, length, g_currentCpuContext ? g_currentCpuContext->pc : 0, g_currentCpuContext ? g_currentCpuContext->lr : 0);
    std::fflush(stderr);
    std::abort();
}

#ifndef __3DS__
namespace GuestFlat { bool g_requiresCheckedAccess = true; }
#endif

namespace RecompMod {
std::atomic<uint8_t> g_executableWriteGuardPages[1u << (32 - kExecutableWriteGuardPageShift)]{};
}

namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};

uintptr_t g_fullPageBias[kPageCount]{};
uintptr_t g_fullReadablePageBias[kPageCount]{};
uintptr_t g_fullWritablePageBias[kPageCount]{};
static inline bool is_mmio_or_null(uint32_t address) {
    return address <= 0x10000u || address >= 0xFFFFFF00u || (address >= 0xCC000000u && address <= 0xCDFFFFFFu);
}

template <typename T>
static inline T swap_bytes(T val) {
    if constexpr (sizeof(T) == 2) return __builtin_bswap16(static_cast<uint16_t>(val));
    else if constexpr (sizeof(T) == 4) return __builtin_bswap32(static_cast<uint32_t>(val));
    else if constexpr (sizeof(T) == 8) return __builtin_bswap64(static_cast<uint64_t>(val));
    return val;
}

uint8_t Read8Slow(uint32_t address) {
    if (is_mmio_or_null(address)) return 0;
    if (auto* ptr = resolve_guest(address, 1)) return *ptr;
    return 0;
}

uint16_t Read16Slow(uint32_t address) {
    if (is_mmio_or_null(address)) return 0;
    if (auto* ptr = resolve_guest(address, 2)) {
        uint16_t val;
        std::memcpy(&val, ptr, 2);
        return swap_bytes(val);
    }
    return 0;
}

uint32_t Read32Slow(uint32_t address) {
    if (is_mmio_or_null(address)) return 0;
    if (auto* ptr = resolve_guest(address, 4)) {
        uint32_t val;
        std::memcpy(&val, ptr, 4);
        return swap_bytes(val);
    }
    return 0;
}

uint64_t Read64Slow(uint32_t address) {
    if (is_mmio_or_null(address)) return 0;
    if (auto* ptr = resolve_guest(address, 8)) {
        // Two 32-bit loads: an 8-byte memcpy becomes ldrd on ARM11, which faults on
        // 4-byte-aligned guest addresses.
        uint32_t first = 0;
        uint32_t second = 0;
        std::memcpy(&first, ptr, 4);
        std::memcpy(&second, ptr + 4, 4);
        return (static_cast<uint64_t>(swap_bytes(first)) << 32) | swap_bytes(second);
    }
    return 0;
}

void Write32Slow(uint32_t address, uint32_t value) {
    if (is_mmio_or_null(address)) return;
    if (auto* ptr = resolve_guest(address, 4)) {
        uint32_t val = swap_bytes(value);
        std::memcpy(ptr, &val, 4);
    }
}

void Write8Slow(uint32_t address, uint8_t value) {
    if (is_mmio_or_null(address)) return;
    if (auto* ptr = resolve_guest(address, 1)) {
        *ptr = value;
    }
}

void Write16Slow(uint32_t address, uint16_t value) {
    if (is_mmio_or_null(address)) return;
    if (auto* ptr = resolve_guest(address, 2)) {
        uint16_t val = swap_bytes(value);
        std::memcpy(ptr, &val, 2);
    }
}

void Write64Slow(uint32_t address, uint64_t value) {
    if (is_mmio_or_null(address)) return;
    if (auto* ptr = resolve_guest(address, 8)) {
        const uint32_t first = swap_bytes(static_cast<uint32_t>(value >> 32));
        const uint32_t second = swap_bytes(static_cast<uint32_t>(value));
        std::memcpy(ptr, &first, 4);
        std::memcpy(ptr + 4, &second, 4);
    }
}
}

#include <csignal>
#include <unistd.h>

#ifndef __3DS__
#include <execinfo.h>
#endif
static uint32_t g_hleRing[8] = {};
static unsigned g_hleRingPos = 0;

#ifndef __3DS__
static void fault_handler(int sig, siginfo_t* info, void* /*ctx*/) {
    char buf[512];
    const unsigned long addr = info ? reinterpret_cast<unsigned long>(info->si_addr) : 0;
    const CpuContext* cpu = g_currentCpuContext;
    int n = std::snprintf(buf, sizeof(buf),
        "\n[HOST FAULT] sig=%d addr=%08lX guestPC=%08lX guestLR=%08lX r3=%08lX r4=%08lX\n",
        sig, addr, cpu ? static_cast<unsigned long>(cpu->pc) : 0,
        cpu ? static_cast<unsigned long>(cpu->lr) : 0,
        cpu ? static_cast<unsigned long>(cpu->gpr[3]) : 0,
        cpu ? static_cast<unsigned long>(cpu->gpr[4]) : 0);
    if (n > 0) { ssize_t ignored = write(2, buf, static_cast<size_t>(n)); (void)ignored; }
    {
        char rbuf[256];
        int m = std::snprintf(rbuf, sizeof(rbuf), "[HOST FAULT] last HLE targets:");
        for (int i = 0; i < 8; ++i) {
            const uint32_t t = g_hleRing[(g_hleRingPos + i) % 8];
            if (t) m += std::snprintf(rbuf + m, sizeof(rbuf) - static_cast<size_t>(m), " %08X", t);
        }
        m += std::snprintf(rbuf + m, sizeof(rbuf) - static_cast<size_t>(m), "\n");
        ssize_t ignored = write(2, rbuf, static_cast<size_t>(m)); (void)ignored;
    }
#ifndef __3DS__
    void* frames[24];
    const int count = backtrace(frames, 24);
    backtrace_symbols_fd(frames, count, 2);
#endif
    std::_Exit(3);
}
#endif

static void alarm_handler(int) {
    if (g_currentCpuContext) {
        std::fprintf(stderr, "\n[HANG TRACE] pc=%08lX lr=%08lX r3=%08lX r4=%08lX r31=%08lX\n",
            static_cast<unsigned long>(g_currentCpuContext->pc),
            static_cast<unsigned long>(g_currentCpuContext->lr),
            static_cast<unsigned long>(g_currentCpuContext->gpr[3]),
            static_cast<unsigned long>(g_currentCpuContext->gpr[4]),
            static_cast<unsigned long>(g_currentCpuContext->gpr[31]));
    }
    std::abort();
}

static bool prepare_memory()
{
    // MEM1 and MEM2 stay single contiguous arenas: guest heap code passes whole sections to
    // memcpy-style accessors (Memory::GetPointer over multi-megabyte ranges), so the backing store
    // cannot be split into separately allocated pages.  What the menu path saves instead is the
    // 6 MiB race staging buffer (force-race shortcut only), the eager NAND save copy, and half of
    // the texture-cache budget.
    constexpr uint32_t base = 0x80000000u;
    const size_t size = guestSize;
    auto* ram = static_cast<uint8_t*>(std::calloc(1, size));
    if (!ram) {
#ifdef __3DS__
        AppLog("calloc MEM1 (%zu MiB) failed! Free app: %lu MiB\n",
            size / 1024 / 1024, (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024 / 1024));
#endif
        return false;
    }

    volatile uint8_t* pages = ram;
    for (size_t offset = 0; offset < size; offset += 4096) {
        pages[offset] = 0xA5;
        if (pages[offset] != 0xA5) {
            std::free(ram);
            return false;
        }
        pages[offset] = 0;
    }
    const uintptr_t bias = reinterpret_cast<uintptr_t>(ram) - base + 1;
    for (size_t offset = 0; offset < size; offset += MemoryInline::kPageSize) {
        const size_t page = (base + offset) >> MemoryInline::kPageShift;
        MemoryInline::g_fullPageBias[page] = bias;
        MemoryInline::g_fullReadablePageBias[page] = bias;
        MemoryInline::g_fullWritablePageBias[page] = bias;
        if (offset < 0x01800000u) {
            constexpr uint32_t aliasBase = 0xC0000000u;
            const auto aliasPage = (aliasBase + offset) >> MemoryInline::kPageShift;
            const auto aliasBias = reinterpret_cast<uintptr_t>(ram) - aliasBase + 1;
            MemoryInline::g_fullPageBias[aliasPage] = aliasBias;
            MemoryInline::g_fullReadablePageBias[aliasPage] = aliasBias;
            MemoryInline::g_fullWritablePageBias[aliasPage] = aliasBias;
        }
    }
    guestRam = ram;

#ifdef __3DS__
    size_t wantMem2 = 36u * 1024 * 1024;
    if (const char* mb = std::getenv("MKW_MEM2_MB")) {
        const long v = std::strtol(mb, nullptr, 10);
        if (v >= 16 && v <= 64) wantMem2 = static_cast<size_t>(v) * 1024 * 1024;
    }
    for (size_t trySize : {wantMem2, 32u * 1024 * 1024, 28u * 1024 * 1024,
                           24u * 1024 * 1024, 20u * 1024 * 1024}) {
        if (trySize > wantMem2) continue;
        mem2Ram = static_cast<uint8_t*>(std::calloc(1, trySize));
        if (mem2Ram) {
            mem2Size = trySize;
            break;
        }
    }
    AppLog("[Mem] guest MEM2 arena = %zu MiB (host heap free %zuM after guest RAM)\n",
        mem2Size / 1024 / 1024, HostHeapFreeKb() / 1024);
#else
    if (const char* mb = std::getenv("MKW_MEM2_MB")) {
        const long v = std::strtol(mb, nullptr, 10);
        if (v >= 16 && v <= 64) mem2Size = static_cast<size_t>(v) * 1024 * 1024;
    }
    mem2Ram = static_cast<uint8_t*>(std::calloc(1, mem2Size));
#endif
    if (!mem2Ram) {
#ifdef __3DS__
        AppLog("calloc MEM2 (%zu MiB) failed! Free app: %lu MiB\n",
            mem2Size / 1024 / 1024, (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024 / 1024));
#endif
        return false;
    }

    for (uint32_t alias : {0x90000000u, 0xD0000000u, 0x10000000u, 0xA0000000u}) {
        const uintptr_t mem2Bias = reinterpret_cast<uintptr_t>(mem2Ram) - alias + 1;
        for (size_t offset = 0; offset < mem2Size; offset += MemoryInline::kPageSize) {
            const auto page = (alias + offset) >> MemoryInline::kPageShift;
            MemoryInline::g_fullPageBias[page] = mem2Bias;
            MemoryInline::g_fullReadablePageBias[page] = mem2Bias;
            MemoryInline::g_fullWritablePageBias[page] = mem2Bias;
        }
    }
    return true;
}

static void ReportWorkingSet(const char* checkpoint) {
    size_t mem1ActivePages = 0;
    const size_t mem1TotalPages = guestSize / 4096;
    for (size_t p = 0; p < mem1TotalPages && guestRam; ++p) {
        const uint64_t* words = reinterpret_cast<const uint64_t*>(guestRam + p * 4096);
        bool active = false;
        for (size_t w = 0; w < 4096 / sizeof(uint64_t); ++w) {
            if (words[w] != 0) { active = true; break; }
        }
        if (active) ++mem1ActivePages;
    }

    size_t mem2ActivePages = 0;
    const size_t mem2TotalPages = mem2Size / 4096;
    for (size_t p = 0; p < mem2TotalPages && mem2Ram; ++p) {
        const uint64_t* words = reinterpret_cast<const uint64_t*>(mem2Ram + p * 4096);
        bool active = false;
        for (size_t w = 0; w < 4096 / sizeof(uint64_t); ++w) {
            if (words[w] != 0) { active = true; break; }
        }
        if (active) ++mem2ActivePages;
    }

    const double mem1Mb = (mem1ActivePages * 4096) / (1024.0 * 1024.0);
    const double mem2Mb = (mem2ActivePages * 4096) / (1024.0 * 1024.0);
    AppLog("[WorkingSet Profile @ %s]\n", checkpoint);
    AppLog("  MEM1: %.2f / %.2f MiB active (%zu / %zu pages)\n", mem1Mb, guestSize / (1024.0 * 1024.0), mem1ActivePages, mem1TotalPages);
    AppLog("  MEM2: %.2f / %.2f MiB active (%zu / %zu pages)\n", mem2Mb, mem2Size / (1024.0 * 1024.0), mem2ActivePages, mem2TotalPages);
    AppLog("  Combined Guest Active RAM: %.2f MiB (host heap free %zuM)\n", mem1Mb + mem2Mb,
        HostHeapFreeKb() / 1024);
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifndef __3DS__
    // Crash diagnostics for the headless harness: report the guest PC/LR and the faulting host
    // address instead of dying silently (the 3DS keeps libctru's own handler).
    {
        struct sigaction sa {};
        sa.sa_sigaction = fault_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGSEGV, &sa, nullptr);
    }
#endif
#ifdef __3DS__
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, nullptr);
    std::freopen("sdmc:/wiicompiled_log.txt", "w", stderr);
    bool new3ds = false;
    const Result model = APT_CheckNew3DS(&new3ds);
    AppLog("WiiCompiled PAL startup\n");
    AppLog("System mode: %s\n", R_FAILED(model) ? "unknown" : new3ds ? "New 3DS" : "Old 3DS");
    AppLog("Memory: App Free %luM, Linear Free %luM\n",
        (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / (1024 * 1024)),
        (unsigned long)(linearSpaceFree() / (1024 * 1024)));
#endif
    bool pass = false;
    if (prepare_memory()) {
        AppLog("Guest RAM: PASS (MEM1=%uM MEM2=%uM, host heap free %zuM)\n",
            (unsigned)(guestSize / 1024 / 1024), (unsigned)(mem2Size / 1024 / 1024),
            HostHeapFreeKb() / 1024);
        // PAL boot information expected by the SDK.
        Memory::Write32(0x80000000u, 0x524D4350u);
        Memory::Write32(0x80000004u, 0x30310100u);
        for (uint32_t address : {0x80000028u, 0x800000F0u, 0x80003100u, 0x80003104u})
            Memory::Write32(address, 0x01800000u);
        for (uint32_t address : {0x80000030u, 0x8000310Cu})
            Memory::Write32(address, 0x80399180u);
        for (uint32_t address : {0x80000034u, 0x80003110u})
            Memory::Write32(address, 0x817F0520u);
        Memory::Write32(0x80003108u, 0x81800000u);
        Memory::Write32(0x800000F8u, TimeBaseContract::kBusClockHz);
        Memory::Write32(0x800000FCu, TimeBaseContract::kBusClockHz * 3);
        Memory::Write32(0x80003140u, 0x00240E18u);
        Memory::Write32(0x80003144u, 0x00030110u);
        const uint32_t mem2End = mem2Base + mem2Size;
        Memory::Write32(0x80003118u, mem2Size);
        Memory::Write32(0x8000311Cu, mem2Size);
        Memory::Write32(0x80003120u, mem2End - 0x20000u);
        Memory::Write32(0x80003124u, mem2Base + 0x800u);
        Memory::Write32(0x80003128u, mem2End - 0x240000u);
        Memory::Write32(0x80003130u, mem2End - 0x40000u);
        Memory::Write32(0x80003134u, mem2End - 0x20000u);
        Memory::Write32(0x80003148u, mem2End - 0x20000u);
        Memory::Write32(0x8000314Cu, mem2End);
        InitializeDataSections();
        uint32_t kclStrAddr = 0;
        for (uint32_t a = 0x80000000u; a < 0x80400000u; a += 4) {
            if (Memory::Contains(a, 11) && std::memcmp(Memory::GetPointer(a, 11), "course.kcl", 11) == 0) {
                kclStrAddr = a;
                break;
            }
        }
        AppLog("[String Finder] 'course.kcl' at 0x%08X\n", kclStrAddr);
        CpuContext cpu{};
        cpu.gpr[1] = 0x81700000u;
        cpu.gpr[2] = RuntimeConfig::SDA2_BASE;
        cpu.gpr[13] = RuntimeConfig::SDA1_BASE;
        const auto boundary = run_guest_boot(cpu);
        AppLog("\nBoot boundary: %08lX (pc=%08X lr=%08X r3=%08X r1=%08X)\n",
            static_cast<unsigned long>(boundary), cpu.pc, cpu.lr, cpu.gpr[3], cpu.gpr[1]);
        ReportWorkingSet("Exit / Boundary");
        pass = boundary == 0;
        std::puts(pass ? "Guest entry returned" : "Guest service missing");
    } else {
        AppLog("Guest RAM (MEM1=%uM MEM2=%uM): FAIL\n",
            (unsigned)(guestSize / 1024 / 1024), (unsigned)(mem2Size / 1024 / 1024));
    }

#ifdef __3DS__
    AppLogFlush();
    std::puts("\nSTART to exit");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gspWaitForVBlank();
        gfxSwapBuffers();
    }
    gfxExit();
#endif
    std::free(mem2Ram);
    std::free(guestRam);
    return pass ? 0 : 1;
}

extern "C" uint32_t OSSystemCall()
{
    return 0;
}

extern "C" void PPC_Ecowx(uint32_t, uint32_t)
{
}

extern "C" void PPC_Mtfsb1(uint32_t bit)
{
    bit &= 31;
    if (bit == 1 || bit == 2)
        return;
    auto& cpu = *g_currentCpuContext;
    cpu.fpscr |= 1u << (31 - bit);
    MkwApplyHostNiMode(cpu.fpscr);
}

extern "C" void PPC_Mtfsb0(uint32_t bit)
{
    if (!g_currentCpuContext) return;
    bit &= 31u;
    g_currentCpuContext->fpscr &= ~(1u << (31u - bit));
    MkwApplyHostNiMode(g_currentCpuContext->fpscr);
}

extern "C" uint32_t PPC_ReadSpr(uint32_t spr)
{
    CpuContext* cpu = g_currentCpuContext;
    if (spr >= 912u && spr <= 919u) return cpu ? cpu->gqr[spr - 912u] : 0u;
    switch (spr) {
        case 8: return cpu ? cpu->lr : 0u;
        case 9: return cpu ? cpu->ctr : 0u;
        case 1: return cpu ? cpu->xer : 0u;
        case 26: return cpu ? cpu->srr0 : 0u;
        case 27: return cpu ? cpu->srr1 : 0u;
        default: return 0u;
    }
}

extern "C" void PPC_WriteSpr(uint32_t spr, uint32_t value)
{
    CpuContext* cpu = g_currentCpuContext;
    if (spr >= 912u && spr <= 919u) {
        if (cpu) cpu->gqr[spr - 912u] = value;
        return;
    }
    switch (spr) {
        case 8: if (cpu) cpu->lr = value; break;
        case 9: if (cpu) cpu->ctr = value; break;
        case 1: if (cpu) cpu->xer = value; break;
        case 26: if (cpu) cpu->srr0 = value; break;
        case 27: if (cpu) cpu->srr1 = value; break;
        case 922: // DMAU
            g_lockedCacheDmaUpper = value;
            break;
        case 923: { // DMAL
            if ((value & 2u) == 0) break;
            const uint32_t blocks = ((g_lockedCacheDmaUpper & 31u) << 2) | ((value >> 2) & 3u);
            const size_t bytes = (blocks ? blocks : 128u) * 32u;
            const uint32_t memoryAddr = g_lockedCacheDmaUpper & 0x1FFFFFE0u;
            const uint32_t cacheAddr = value & 0xFFFFFFE0u;
            uint8_t* memory = resolve_guest(memoryAddr, bytes);
            uint8_t* cache = resolve_region(g_lockedCache, kLockedCacheBase, sizeof(g_lockedCache), cacheAddr, bytes);
            if (!memory || !cache) {
                static bool reportedInvalidDma = false;
                if (!reportedInvalidDma) {
                    reportedInvalidDma = true;
                    AppLog("[Locked cache] unmapped DMA memory=%08X cache=%08X bytes=%zu\n",
                        memoryAddr, cacheAddr, bytes);
                }
                break;
            }
            if (value & 0x10u) std::memcpy(cache, memory, bytes);
            else std::memcpy(memory, cache, bytes);
            break;
        }
        default: break;
    }
}

extern "C" int32_t memset_zero_32(int32_t address)
{
    const uint32_t addr = static_cast<uint32_t>(address) & ~31u;
    if (Memory::Contains(addr, 32)) {
        std::memset(Memory::GetPointer(addr, 32), 0, 32);
    }
    return 0;
}

extern "C" void PPC_Mtfsfi(uint32_t field, uint32_t value)
{
    if (!g_currentCpuContext) return;
    field &= 7u;
    const uint32_t shift = (7u - field) * 4u;
    uint32_t mask = 0xFu << shift;
    g_currentCpuContext->fpscr = (g_currentCpuContext->fpscr & ~mask) | ((value & 0xFu) << shift);
    MkwApplyHostNiMode(g_currentCpuContext->fpscr);
}

extern "C" double PPC_Mffs()
{
    return std::bit_cast<double>(static_cast<uint64_t>(g_currentCpuContext ? g_currentCpuContext->fpscr : 0));
}

extern "C" void PPC_Mtfsf(uint32_t fieldMask, double source)
{
    if (fieldMask == 0 || !g_currentCpuContext) return;
    const uint32_t incoming = static_cast<uint32_t>(std::bit_cast<uint64_t>(source));
    for (int field = 0; field < 8; ++field) {
        if ((fieldMask & (1u << (7 - field))) == 0) continue;
        const int shift = (7 - field) * 4;
        uint32_t mask = 0xFu << shift;
        g_currentCpuContext->fpscr = (g_currentCpuContext->fpscr & ~mask) | (incoming & mask);
    }
    MkwApplyHostNiMode(g_currentCpuContext->fpscr);
}

static uint64_t GetGuestTimeBase()
{
    using namespace std::chrono;
    static const auto epoch = steady_clock::now();
    const auto elapsed = duration_cast<nanoseconds>(steady_clock::now() - epoch);
    return TimeBaseContract::NanosecondsToTicks(elapsed.count());
}

extern "C" uint32_t PPC_Mftb()
{
    return static_cast<uint32_t>(GetGuestTimeBase());
}

extern "C" uint32_t PPC_Mftbu()
{
    return static_cast<uint32_t>(GetGuestTimeBase() >> 32);
}

template <uint32_t W, bool Stack>
double PPC_PsqLStateFallback(uint32_t gqr, uint32_t addr);
template <uint32_t W, bool Stack>
void PPC_PsqStStateFallback(uint32_t gqr, uint32_t addr, double value);

extern "C" double PPC_PsqL(uint32_t addr, uint32_t w, uint32_t i)
{
    CpuContext* cpu = g_currentCpuContext;
    if (!cpu) std::abort();
    const uint32_t gqr = cpu->gqr[i & 7];
    return w == 0 ? PPC_PsqLStateFallback<0u, false>(gqr, addr)
                  : PPC_PsqLStateFallback<1u, false>(gqr, addr);
}

extern "C" void PPC_PsqSt(uint32_t addr, double value, uint32_t w, uint32_t i)
{
    CpuContext* cpu = g_currentCpuContext;
    if (!cpu) std::abort();
    const uint32_t gqr = cpu->gqr[i & 7];
    if (w == 0) {
        PPC_PsqStStateFallback<0u, false>(gqr, addr, value);
    } else {
        PPC_PsqStStateFallback<1u, false>(gqr, addr, value);
    }
}

extern "C" double PPC_PsAdd(double lhs, double rhs) { return PPC_PsAddInline(lhs, rhs); }
extern "C" double PPC_PsSub(double lhs, double rhs) { return PPC_PsSubInline(lhs, rhs); }
extern "C" double PPC_PsDiv(double lhs, double rhs) { return PPC_PsDivInline(lhs, rhs); }
extern "C" double PPC_PsNeg(double value) { return PPC_PsNegInline(value); }
extern "C" double PPC_PsMul(double lhs, double rhs) { return PPC_PsMulInline(lhs, rhs); }
extern "C" double PPC_PsMsub(double multiplicand, double multiplier, double subtractor) { return PPC_PsMsubInline(multiplicand, multiplier, subtractor); }
extern "C" double PPC_PsMadd(double multiplicand, double multiplier, double addend) { return PPC_PsMaddInline(multiplicand, multiplier, addend); }
extern "C" double PPC_PsMadds0(double multiplicand, double multiplier, double addend) { return PPC_PsMadds0Inline(multiplicand, multiplier, addend); }
extern "C" double PPC_PsMadds1(double multiplicand, double multiplier, double addend) { return PPC_PsMadds1Inline(multiplicand, multiplier, addend); }
extern "C" double PPC_PsNmsub(double multiplicand, double multiplier, double subtractor) { return PPC_PsNmsubInline(multiplicand, multiplier, subtractor); }
extern "C" double PPC_PsNmadd(double multiplicand, double multiplier, double addend) { return PPC_PsNmaddInline(multiplicand, multiplier, addend); }
extern "C" double PPC_PsSel(double lhs, double control, double rhs) { return PPC_PsSelInline(lhs, control, rhs); }

#include <algorithm>
#include <cmath>

namespace gx_bridge {

static constexpr int kScreenW = 400;
static constexpr int kScreenH = 240;

static uint32_t g_fbColor[kScreenW * kScreenH]; // 0xRRGGBBAA
static float    g_fbDepth[kScreenW * kScreenH];

struct DecodedTex {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t  format = 0;
    uint32_t hash = 0;
    uint64_t lastUse = 0;       // LRU stamp
    std::vector<uint32_t> rgba; // width * height of 0xRRGGBBAA
    mutable std::vector<std::vector<uint32_t>> mips; // lazily built box-filtered levels
};

static std::map<uint64_t, DecodedTex> g_texCache;
static size_t g_texCacheBytes = 0;
static uint64_t g_texTick = 0;                  // LRU clock
static DecodedTex g_texScratch;                 // decode target for textures that do not fit
static uint32_t g_texStatDecodes = 0;           // textures decoded since the last report
static uint32_t g_texStatHits = 0;
static uint32_t g_texStatEvictions = 0;
static size_t   g_texStatTexels = 0;
static void BuildMipChain(DecodedTex& tex);
#ifdef __3DS__
// The Old 3DS app region is what is left after guest MEM1+MEM2 and our own code, so keep the
// texture cache small: it is a pure cache, and running out of *host* heap here aborts the port
// (that is what killed the section-63 run at frame 84 while RKSYS::Mgr::LoadTask allocated its
// 2.8 MiB save buffer).
// The menu's decoded-texture working set is ~5.4 MiB.  With the guest's DOL _text blob no longer
// embedded there is room for it, and keeping the cache warm removes the clear-and-redecode every
// other frame that made the title screen crawl.  Allocation failures are caught and fall back to
// untextured quads, so an over-large cap degrades instead of crashing.
// The menu's decoded set measures ~5.9 MiB (23 textures) and a full mip chain adds another third,
// so a 5 MiB cap evicted and re-decoded textures every frame (2 s/frame on device).  7 MiB holds
// the set outright now that the guest DOL _text blob is gone.
static constexpr size_t kMaxTexCacheBytes = 8 * 1024 * 1024;
#else
static constexpr size_t kMaxTexCacheBytes = 24 * 1024 * 1024;
#endif

struct TexSlot {
    uint32_t dataAddr = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t  format = 0;
    uint8_t  wrapS = 0;
    uint8_t  wrapT = 0;
    bool     valid = false;
};

struct VtxAttrFmt {
    uint32_t cnt = 1;
    uint32_t type = 4; // 0=U8, 1=S8, 2=U16, 3=S16, 4=F32; for color: 0=RGB565, 1=RGB8, 2=RGBX8, 3=RGBA4, 4=RGBA6, 5=RGBA8
    uint8_t  frac = 0;
};

struct VtxArray {
    uint32_t base = 0;
    uint8_t  stride = 0;
};

struct GxVertex {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float nrm[3] = {0.0f, 0.0f, 1.0f};
    uint8_t color0[4] = {255, 255, 255, 255};
    float uv0[2] = {0.0f, 0.0f};
    bool hasColor0 = false;
    bool hasUv0 = false;
};

struct GxState {
    float proj[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,-1,-1},{0,0,-1,0}};
    uint32_t projType = 0; // 0 = perspective, 1 = ortho
    float xfProj[6] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f};
    uint32_t xfProjType = 0;
    float pnMtx[10][12];
    uint32_t currentPnMtx = 0;

    // Per-frame diagnostics: did the guest actually push a view/model matrix this frame?
    uint32_t mtxLoadCalls = 0;     // GXLoadPosMtxImm / GXLoadPosMtxIndx calls
    uint32_t xfMtxWrites = 0;      // completed 12-float XF position-matrix writes
    uint32_t mtxNonIdentity = 0;   // loads whose row0/row1 were not the sanitized identity
    uint32_t camUpdateCalls = 0;   // RaceCameraMgr::Update calls this frame
    uint32_t drawCallsSkipped = 0; // draws dropped by the raster frame-skip
    uint32_t lastTrianglePixels = 0; // diagnostic result from the most recent software triangle
    bool trackTrianglePixels = false;
    uint32_t pixels3DThisFrame = 0; // pixels written by perspective draws, for menu diagnostics
    uint32_t tri3DEntered = 0;      // perspective triangles reaching the rasteriser
    uint32_t tri3DBounds = 0;       // perspective triangles with on-screen bounds
    uint32_t tri2DAlphaZero = 0;    // ortho triangles whose vertex alpha is fully zero
    uint32_t tri2DWritten = 0;      // ortho triangles that wrote at least one pixel

    float vpLeft = 0.0f, vpTop = 0.0f, vpWidth = 608.0f, vpHeight = 456.0f;
    float vpNear = 0.0f, vpFar = 1.0f;
    uint32_t efbWidth = 608, efbHeight = 456;

    // GX scissor box, in EFB pixels. Menu layouts rely on this to keep full-screen
    // effect quads (checkered ribbons, flares) inside their own strip.
    uint32_t scissorX = 0, scissorY = 0, scissorW = 608, scissorH = 456;
    bool scissorSet = false;
    int clipX0 = 0, clipY0 = 0, clipX1 = 400, clipY1 = 240; // screen-space scissor

    uint32_t vtxDesc[21] = {}; // 0=NONE, 1=DIRECT, 2=INDEX8, 3=INDEX16
    VtxAttrFmt vtxFmt[8][21] = {};
    VtxArray vtxArray[25] = {};

    TexSlot texSlots[8] = {};
    int activeTexSlot = 0;
    bool texStageEnabled[16] = {};
    uint8_t stageTexMap[16] = {};

    uint8_t chanMatColor[2][4] = {{255,255,255,255}, {255,255,255,255}};
    uint8_t chanAmbColor[2][4] = {{128,128,128,255}, {128,128,128,255}};

    // Channel 0 lighting (the subset Mario Kart Wii actually uses: diffuse, one directional light).
    uint32_t chanCtrl0 = 0;                                  // XF 0x100E
    uint8_t  light0Color[4] = {255, 255, 255, 255};          // XF 0x0600
    float    light0Dir[3] = {0.0f, 0.0f, 1.0f};              // XF 0x0607..0x0609
    bool     light0DirWritten = false;
    bool     lightEnabled = false;
    uint8_t tevColor[4][4] = {{255,255,255,255}, {255,255,255,255}, {255,255,255,255}, {255,255,255,255}};
    uint8_t tevKColor[4][4] = {{255,255,255,255}, {255,255,255,255}, {255,255,255,255}, {255,255,255,255}};

    // TEV combiner state. Mario Kart Wii drives almost every material through the TEV, so the old
    // "vertex * texture" shortcut is only correct for the default MODULATE setup. These mirror the
    // BP registers the guest writes via GXSetTev*.
    uint32_t tevNumStages = 1;
    // Per stage: texCoordId, texMapId (0xFF = no texture), colorChan (0/1/0xFF = no colour).
    uint8_t  tevOrderCoord[16] = {};
    uint8_t  tevOrderMap[16] = {};
    uint8_t  tevOrderChan[16] = {};
    bool     tevOrderEnabled[16] = {};
    // Colour/alpha input selectors (a,b,c,d) and output ops.
    uint8_t  tevColorIn[16][4] = {};
    uint8_t  tevAlphaIn[16][4] = {};
    uint8_t  tevColorOp[16] = {};     // 0=ADD 1=SUB 2=COMP_RGB8 3=COMP_RGBA8
    uint8_t  tevColorBias[16] = {};   // 0=none 1=+0.5 2=-0.5
    uint8_t  tevColorScale[16] = {};  // 0=1 1=2 2=4 3=0.5
    bool     tevColorClamp[16] = {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
    uint8_t  tevColorOut[16] = {};    // output register id
    uint8_t  tevAlphaOp[16] = {};
    uint8_t  tevAlphaBias[16] = {};
    uint8_t  tevAlphaScale[16] = {};
    bool     tevAlphaClamp[16] = {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
    uint8_t  tevAlphaOut[16] = {};
    uint8_t  tevKColorSel[16] = {};
    uint8_t  tevKAlphaSel[16] = {};
    uint8_t  tevSwapRas[16] = {};
    uint8_t  tevSwapTex[16] = {};
    uint8_t  tevSwapTable[4][4] = {{0,1,2,3},{0,1,2,3},{0,1,2,3},{0,1,2,3}};
    // GXSetAlphaCompare: func0/ref0, func1/ref1, op (0=AND 1=OR 2=XOR 3=XNOR).
    uint32_t alphaComp0 = 7;  // GX_ALWAYS
    uint32_t alphaRef0 = 0;
    uint32_t alphaComp1 = 7;
    uint32_t alphaRef1 = 0;
    uint32_t alphaCompOp = 0;
    uint8_t clearColor[4] = {105, 165, 235, 255}; // Default sky blue

    bool depthCompare = true;
    uint32_t depthFunc = 3; // GX_LEQUAL
    bool depthUpdate = true;
    uint32_t cullMode = 0; // 0=NONE, 1=FRONT, 2=BACK, 3=ALL

    uint32_t blendMode = 0; // 0=NONE, 1=BLEND, 2=LOGIC, 3=SUBTRACT
    uint32_t blendSrc = 4;  // GX_BL_SRCALPHA
    uint32_t blendDst = 5;  // GX_BL_INVSRCALPHA

    // Display list recording state
    bool inDisplayList = false;
    uint32_t dlBaseAddr = 0;
    uint32_t dlMaxSize = 0;
    uint32_t dlWriteOffset = 0;

    // Frame statistics
    uint32_t drawCallsThisFrame = 0;
    uint32_t trisThisFrame = 0;
    uint32_t quads2DThisFrame = 0;
    uint32_t dlCallsThisFrame = 0;
};

static GxState g_gx;
// nw4r layout quads are drawn through the 3DS DrawLytQuad2D shortcut, which bypasses the guest
// display list (and therefore the TEV setup). Their contract is the fixed "vertex * texture"
// modulate that nw4r uses for layout panes, so they must not consume the last TEV state.
static bool g_layoutQuad = false;
static bool g_gxInitialized = false;

static inline uint16_t ReadBE16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

static inline uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

static inline float ReadBEFloat(const uint8_t* p) {
    uint32_t u = ReadBE32(p);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

static inline float ReadGuestFloat(uint32_t addr) {
    uint32_t u = Memory::Read32(addr);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

static inline void WriteGuestFloat(uint32_t addr, float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    Memory::Write32(addr, u);
}

static void SanitizePerspectiveProjection() {
    if (g_gx.projType != 0) return;
    const float defaultCotY = 1.9626105f; // ~54 deg vertical FOV
    // 3DS Top Screen is 400x240 (aspect ratio 5:3 = 1.666667)
    const float aspect = 400.0f / 240.0f;
    const float defaultCotX = defaultCotY / aspect;

    if (!std::isfinite(g_gx.proj[0][0]) || std::fabs(g_gx.proj[0][0]) < 1e-4f || std::fabs(g_gx.proj[0][0]) > 1e4f) {
        g_gx.proj[0][0] = defaultCotX;
    }
    if (!std::isfinite(g_gx.proj[0][1])) g_gx.proj[0][1] = 0.0f;
    if (!std::isfinite(g_gx.proj[0][2])) g_gx.proj[0][2] = 0.0f;
    if (!std::isfinite(g_gx.proj[0][3])) g_gx.proj[0][3] = 0.0f;

    if (!std::isfinite(g_gx.proj[1][0])) g_gx.proj[1][0] = 0.0f;
    if (!std::isfinite(g_gx.proj[1][1]) || std::fabs(g_gx.proj[1][1]) < 1e-4f || std::fabs(g_gx.proj[1][1]) > 1e4f) {
        g_gx.proj[1][1] = defaultCotY;
    }
    if (!std::isfinite(g_gx.proj[1][2])) g_gx.proj[1][2] = 0.0f;
    if (!std::isfinite(g_gx.proj[1][3])) g_gx.proj[1][3] = 0.0f;

    if (!std::isfinite(g_gx.proj[2][0])) g_gx.proj[2][0] = 0.0f;
    if (!std::isfinite(g_gx.proj[2][1])) g_gx.proj[2][1] = 0.0f;
    if (!std::isfinite(g_gx.proj[2][2]) || std::fabs(g_gx.proj[2][2]) < 1e-8f) {
        g_gx.proj[2][2] = -2.60001e-06f;
    }
    if (!std::isfinite(g_gx.proj[2][3]) || std::fabs(g_gx.proj[2][3]) < 1e-4f) {
        g_gx.proj[2][3] = -1.3f;
    }
    if (!std::isfinite(g_gx.proj[3][2]) || std::fabs(g_gx.proj[3][2]) < 1e-4f) {
        g_gx.proj[3][2] = -1.0f;
    }
}

static void SanitizeModelViewMatrix(float* m) {
    if (!m) return;
    bool r0Zero = (m[0] == 0.0f && m[1] == 0.0f && m[2] == 0.0f);
    bool r1Zero = (m[4] == 0.0f && m[5] == 0.0f && m[6] == 0.0f);
    if (r0Zero && r1Zero) {
        // Enforce standard Mario Kart Wii 3D Camera / ModelView Basis:
        // Row 0: +X Right
        m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
        // Row 1: +Y Up (elevation)
        m[4] = 0.0f; m[5] = 1.0f; m[6] = 0.0f;
        // Row 2: +Z mapping world Z (< 0 in front of camera) to eye Z (< 0)
        m[8] = 0.0f; m[9] = 0.0f;
        if (std::fabs(m[10]) < 0.1f) {
            m[10] = 1.0f;
        }
    }
}

// PresentFrameTo3DS numbers frames from 1.
// Per-draw tracing is only useful for offline debugging. Enabling it costs ~6800 console
// lines in a SINGLE frame (3385 draws x 2 lines) which stalls the device for minutes, so it is
// switched off by default. Set to a real frame number to debug a specific frame.
static const uint32_t kGxTraceFrame = [](){ const char* e = std::getenv("MKW_TRACE_FRAME"); return e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 0)) : 0xFFFFFFFFu; }();
static uint32_t g_gxFrame = 1;
// --- Debug chase camera -------------------------------------------------------------
// The guest never registers a race camera in this boot path (the camera manager stays empty),
// so the view is stuck in the opening flyover. Build a look-at camera ourselves and use it for
// perspective draws. Coordinates come from Luigi Circuit KTPT 0: (-20115, 470.4, 25750).
// Set kDebugCamera = false to go back to the guest's own matrices.
static constexpr bool kDebugCamera = true;

// out = a * b for two GX-style row-major 3x4 affine matrices (a applied after b).
static void MulAffine3x4(const float* a, const float* b, float* out) {
    for (int row = 0; row < 3; ++row) {
        const float a0 = a[row * 4 + 0], a1 = a[row * 4 + 1];
        const float a2 = a[row * 4 + 2], a3 = a[row * 4 + 3];
        out[row * 4 + 0] = a0 * b[0] + a1 * b[4] + a2 * b[8];
        out[row * 4 + 1] = a0 * b[1] + a1 * b[5] + a2 * b[9];
        out[row * 4 + 2] = a0 * b[2] + a1 * b[6] + a2 * b[10];
        out[row * 4 + 3] = a0 * b[3] + a1 * b[7] + a2 * b[11] + a3;
    }
}
static void BuildDebugCamera(float* out /*12 floats, GX row-major 3x4*/) {
    const float tx = -20115.0f, ty = 470.4f, tz = 25750.0f;
    // Chase framing: one kart-length behind and above the first grid slot, looking down the track.
    const float ex = tx, ey = ty + 115.0f, ez = tz - 330.0f;
    const float lookY = ty + 55.0f, lookZ = tz + 260.0f;
    float f[3] = {tx - ex, lookY - ey, lookZ - ez};
    float fl = std::sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (fl <= 1e-6f) { fl = 1.0f; }
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    // right = f x up(world Y)
    float r[3] = { f[1]*0.0f - f[2]*1.0f, f[2]*0.0f - f[0]*0.0f, f[0]*1.0f - f[1]*0.0f };
    float rl = std::sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (rl <= 1e-6f) { r[0] = 1.0f; r[1] = 0.0f; r[2] = 0.0f; rl = 1.0f; }
    r[0] /= rl; r[1] /= rl; r[2] /= rl;
    // up = r x f
    float u[3] = { r[1]*f[2] - r[2]*f[1], r[2]*f[0] - r[0]*f[2], r[0]*f[1] - r[1]*f[0] };
    out[0] = r[0]; out[1]  = r[1]; out[2]  = r[2]; out[3]  = -(r[0]*ex + r[1]*ey + r[2]*ez);
    out[4] = u[0]; out[5]  = u[1]; out[6]  = u[2]; out[7]  = -(u[0]*ex + u[1]*ey + u[2]*ez);
    out[8] = -f[0]; out[9] = -f[1]; out[10] = -f[2]; out[11] = (f[0]*ex + f[1]*ey + f[2]*ez);
}


// ArchiveMgr instance captured on the first GetFile call; used by the per-frame [ArcState] dump.
static uint32_t g_tracedArchiveMgr = 0;
// Panic-site histogram. nw4r panics are downgraded to warnings, so a failing lookup turns into a
// panic-retry loop (measured: 134M calls). Count per caller so the loop is visible.
static std::map<uint32_t, uint32_t> g_panicSites;
static uint32_t g_panicTotal = 0;
// Dispatch accounting for one address: how many times handle_startup_hle is entered with it vs how
// many times its case body actually runs. If these differ, something between the two intercepts it.

// Watch window for the ArchiveMgr entry table (set by the [ArcState] dump) so the HLE trace can
// report every function that is handed an archive entry pointer during the load window.
static uint32_t g_watchArcEntries = 0;
static uint32_t g_watchArcCount = 0;
static uint32_t g_watchArcs[8] = {0,0,0,0,0,0,0,0};
static uint32_t g_watchArcN = 0;
// True when every registered ArchiveMgr group has a loaded entry (state 4 or 5), i.e. every
// archive is usable. func_805411FC refuses entries in any other state.
static bool ArchivesReady() {
    if (!g_tracedArchiveMgr || !Memory::Contains(g_tracedArchiveMgr, 64)) return false;
    const auto* mgr = reinterpret_cast<const uint8_t*>(Memory::GetPointer(g_tracedArchiveMgr, 64));
    (void)mgr;
    const uint32_t arrayBase = Memory::Read32(g_tracedArchiveMgr + 4);
    if (!arrayBase || !Memory::Contains(arrayBase, 32)) return false;
    // Only the first four groups are loaded eagerly; higher groups load lazily on demand, so
    // requiring them would never be satisfied.
    bool any = false;
    for (uint32_t ty = 0; ty < 4; ++ty) {
        const uint32_t arc = Memory::Read32(arrayBase + ty * 4);
        if (!arc || !Memory::Contains(arc, 12)) break;
        const uint32_t count = Memory::Read16(arc + 8);
        const uint32_t entries = Memory::Read32(arc + 4);
        if (!count || !entries) continue;
        bool haveLoaded = false;
        for (uint32_t e = 0; e < count && e < 8; ++e) {
            if (!Memory::Contains(entries + e * 36u, 36)) break;
            const uint32_t state = Memory::Read32(entries + e * 36u + 32);
            if (state == 4 || state == 5) { haveLoaded = true; any = true; }
        }
        if (!haveLoaded) return false;
    }
    return any;
}
static uint32_t g_traceDrawOrder = 0;
struct PerspectiveTrace {
    uint32_t nonFinite = 0;
    uint32_t nearPlane = 0;
    uint32_t area = 0;
    uint32_t offscreen = 0;
    uint32_t rasterized = 0;
    uint32_t wrotePixels = 0;
};
static PerspectiveTrace g_perspectiveTrace;

struct FrameSamples {
    uint32_t center, topLeft, topRight, bottomLeft, bottomRight;
};

static FrameSamples SampleFramebuffer() {
    return {
        g_fbColor[(kScreenH / 2) * kScreenW + kScreenW / 2],
        g_fbColor[kScreenW + 1],
        g_fbColor[kScreenW + kScreenW - 2],
        g_fbColor[(kScreenH - 2) * kScreenW + 1],
        g_fbColor[(kScreenH - 2) * kScreenW + kScreenW - 2],
    };
}

static void LogDrawSamples(uint32_t order, FrameSamples before) {
    const FrameSamples after = SampleFramebuffer();
    AppLog("[GX Trace %u] RGBA %08X %08X %08X %08X %08X -> %08X %08X %08X %08X %08X\n",
        order, before.center, before.topLeft, before.topRight, before.bottomLeft, before.bottomRight,
        after.center, after.topLeft, after.topRight, after.bottomLeft, after.bottomRight);
}


// Rasterise only every kRasterStride-th frame. The guest keeps running at full speed, so the race
// simulation advances ~4x faster while the display shows the most recent fully-rendered frame.
// Skipped frames must NOT clear the framebuffer, otherwise the screen strobes black.
static constexpr uint32_t kRasterStride = 8;
static bool RasterThisFrame() {
    return g_gxFrame < 40u || (g_gxFrame % kRasterStride) == 0u;
}

static void ClearFramebuffer() {
    if (!RasterThisFrame()) return;
    const uint32_t r = g_gx.clearColor[0];
    const uint32_t g = g_gx.clearColor[1];
    const uint32_t b = g_gx.clearColor[2];
    const uint32_t a = g_gx.clearColor[3];
    const uint32_t clearRgba = (r << 24) | (g << 16) | (b << 8) | a;
    for (int i = 0; i < kScreenW * kScreenH; ++i) {
        g_fbColor[i] = clearRgba;
        g_fbDepth[i] = 1.0e9f;
    }
}

static void UpdateScissorScreenRect() {
    const uint32_t efbW = g_gx.efbWidth ? g_gx.efbWidth : 608u;
    const uint32_t efbH = g_gx.efbHeight ? g_gx.efbHeight : 456u;
    auto scaleClamp = [](uint32_t value, uint32_t efb, int target) {
        const int64_t scaled = static_cast<int64_t>(value) * target / static_cast<int64_t>(efb);
        return static_cast<int>(scaled < 0 ? 0 : (scaled > target ? target : scaled));
    };
    g_gx.clipX0 = scaleClamp(g_gx.scissorX, efbW, kScreenW);
    g_gx.clipY0 = scaleClamp(g_gx.scissorY, efbH, kScreenH);
    g_gx.clipX1 = scaleClamp(g_gx.scissorX + g_gx.scissorW, efbW, kScreenW);
    g_gx.clipY1 = scaleClamp(g_gx.scissorY + g_gx.scissorH, efbH, kScreenH);
}

// Frame-time accounting.  The menu sat at ~55 ms per frame with the 2D layout already on the GPU,
// so the remaining cost has to be attributed before optimising anything: software-raster pixels,
// the GPU pass (including the base-layer upload) or the guest CPU in between.
#ifdef __3DS__
static uint64_t g_profRasterTicks = 0;   // software rasteriser
static uint64_t g_profGpuTicks = 0;      // citro3d frame (upload + submit + sync)
static uint32_t g_profRasterTris = 0;
static inline uint64_t ProfNow() { return osGetTime(); }
static inline uint64_t ProfTicksToUs(uint64_t ticks) { return ticks * 1000ull / 268123ull; }
#else
static uint64_t g_profRasterTicks = 0;
static uint64_t g_profGpuTicks = 0;
static uint32_t g_profRasterTris = 0;
static inline uint64_t ProfNow() { return 0; }
static inline uint64_t ProfTicksToUs(uint64_t ticks) { return 0; }
#endif

// PICA200 2D backend.  Implemented further down for the 3DS build only; the host harness keeps
// the software rasteriser so frames can still be dumped to PPM.
static bool g_gpu2d = false;
static uint32_t g_gpuQuadsRecorded = 0;
static uint32_t g_gpuQuadsDrawn = 0;
static size_t   g_gpuTexBytes = 0;
static uint32_t g_gpuTexUploads = 0;
static uint32_t g_gpuTexEvictions = 0;
static uint32_t g_gpuBaseUploads = 0;   // frames that composited the software framebuffer (CPU cost)
static inline bool Gpu2dActive() { return g_gpu2d; }
#ifdef __3DS__
static void Gpu2dConfigure();
static bool Gpu2dInit();
static void Gpu2dRecordQuad(const GxVertex v[4]);
static void Gpu2dRender();
#else
static void Gpu2dConfigure() {}
static bool Gpu2dInit() { return false; }
static void Gpu2dRecordQuad(const GxVertex[4]) {}
static void Gpu2dRender() {}
#endif

// Audio: one BRSTM played by the DSP (see the NDSP block below).  Off with audio=0 in gpu.cfg.
static char g_audioPathBuf[160] = "sdmc:/mkwii/sound/strm/o_Option_32.brstm";
static bool g_audioEnabled = true;
#ifdef __3DS__
static bool AudioStart(const char* path);
#else
static bool AudioStart(const char*) { return false; }
#endif

static void EnsureInitialized() {
    if (g_gxInitialized) return;
    g_gxInitialized = true;
#ifdef __3DS__
    Gpu2dConfigure();
#endif
    for (int m = 0; m < 10; ++m) {
        std::memset(g_gx.pnMtx[m], 0, sizeof(g_gx.pnMtx[m]));
        g_gx.pnMtx[m][0] = 1.0f;
        g_gx.pnMtx[m][5] = 1.0f;
        g_gx.pnMtx[m][10] = 1.0f;
    }
    g_gx.vtxDesc[9]  = 1; // POS DIRECT
    g_gx.vtxDesc[11] = 1; // CLR0 DIRECT
    g_gx.vtxDesc[13] = 1; // TEX0 DIRECT
    g_gx.vtxFmt[0][9]  = {0, 4, 0}; // POS_XY, F32
    g_gx.vtxFmt[0][11] = {1, 5, 0}; // CLR_RGBA, RGBA8
    g_gx.vtxFmt[0][13] = {1, 4, 0}; // TEX_ST, F32
    // Default TEV: one stage doing MODULATE (TEXC * RASC) for colour and TEXA * RASA for alpha,
    // which is exactly the old "vertex * texture" behaviour when the guest never configures TEV.
    g_gx.tevNumStages = 1;
    g_gx.tevColorIn[0][0] = 15; g_gx.tevColorIn[0][1] = 8;  g_gx.tevColorIn[0][2] = 10; g_gx.tevColorIn[0][3] = 15;
    g_gx.tevAlphaIn[0][0] = 7;  g_gx.tevAlphaIn[0][1] = 4;  g_gx.tevAlphaIn[0][2] = 5;  g_gx.tevAlphaIn[0][3] = 7;
    g_gx.tevColorOp[0] = 0;
    g_gx.tevAlphaOp[0] = 0;
    g_gx.tevColorClamp[0] = true;
    g_gx.tevAlphaClamp[0] = true;
    for (int i = 0; i < 4; ++i) {
        g_gx.tevSwapTable[i][0] = 0; g_gx.tevSwapTable[i][1] = 1;
        g_gx.tevSwapTable[i][2] = 2; g_gx.tevSwapTable[i][3] = 3;
    }
    ClearFramebuffer();
}

static void TraceProjection(const char* source, uint32_t input, const CpuContext* cpu) {
    if (g_gxFrame != kGxTraceFrame || g_gx.projType != 0) return;
    const auto& p = g_gx.proj;
    AppLog("[GX Projection] %s input=%08X pc=%08X lr=%08X afterDraw=%u rows=(%g,%g,%g,%g) (%g,%g,%g,%g) (%g,%g,%g,%g) (%g,%g,%g,%g)\n",
        source, input, cpu ? cpu->pc : 0, cpu ? cpu->lr : 0, g_traceDrawOrder,
        p[0][0], p[0][1], p[0][2], p[0][3], p[1][0], p[1][1], p[1][2], p[1][3],
        p[2][0], p[2][1], p[2][2], p[2][3], p[3][0], p[3][1], p[3][2], p[3][3]);
}

static void RebuildProjFromXf() {
    std::memset(g_gx.proj, 0, sizeof(g_gx.proj));
    g_gx.projType = (g_gx.xfProjType != 0) ? 1 : 0;
    const float* p = g_gx.xfProj;
    if (g_gx.projType == 0) {
        g_gx.proj[0][0] = p[0]; g_gx.proj[0][2] = p[1];
        g_gx.proj[1][1] = p[2]; g_gx.proj[1][2] = p[3];
        g_gx.proj[2][2] = p[4]; g_gx.proj[2][3] = p[5];
        g_gx.proj[3][2] = -1.0f;
        SanitizePerspectiveProjection();
    } else {
        g_gx.proj[0][0] = p[0]; g_gx.proj[0][3] = p[1];
        g_gx.proj[1][1] = p[2]; g_gx.proj[1][3] = p[3];
        g_gx.proj[2][2] = p[4]; g_gx.proj[2][3] = p[5];
        g_gx.proj[3][3] = 1.0f;
    }
}

static inline uint32_t DecodeRGB5A3(uint16_t c) {
    if (c & 0x8000u) {
        uint32_t r = ((c >> 10) & 0x1Fu) * 255u / 31u;
        uint32_t g = ((c >> 5)  & 0x1Fu) * 255u / 31u;
        uint32_t b = (c         & 0x1Fu) * 255u / 31u;
        return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
    } else {
        uint32_t a = ((c >> 12) & 0x7u) * 255u / 7u;
        uint32_t r = ((c >> 8)  & 0xFu) * 17u;
        uint32_t g = ((c >> 4)  & 0xFu) * 17u;
        uint32_t b = (c         & 0xFu) * 17u;
        return (r << 24) | (g << 16) | (b << 8) | a;
    }
}

static inline uint32_t DecodeRGB565(uint16_t c) {
    uint32_t r = ((c >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g = ((c >> 5)  & 0x3Fu) * 255u / 63u;
    uint32_t b = (c         & 0x1Fu) * 255u / 31u;
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

static const DecodedTex* GetOrDecodeTexture(const TexSlot& slot) {
    if (!slot.valid || slot.dataAddr == 0 || slot.width == 0 || slot.height == 0 || slot.width > 1024 || slot.height > 1024) {
        return nullptr;
    }
    uint32_t guestAddr = slot.dataAddr;
    const uint8_t* src = resolve_guest(guestAddr, 64);
    if (!src && guestAddr < 0x20000000u) {
        src = resolve_guest(guestAddr | 0x80000000u, 64);
    }
    if (!src) return nullptr;

    uint32_t sampleHash = 0;
    for (int i = 0; i < 64; i += 4) {
        sampleHash = sampleHash * 31u + ReadBE32(src + i);
    }
    uint64_t key = (static_cast<uint64_t>(guestAddr) << 32) ^
                   (static_cast<uint64_t>(slot.width) << 20) ^
                   (static_cast<uint64_t>(slot.height) << 8) ^
                   static_cast<uint64_t>(slot.format);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end() && it->second.hash == sampleHash) {
        it->second.lastUse = ++g_texTick;
        ++g_texStatHits;
        return &it->second;
    }

    // RGBA8888 per texel; the old estimate added 33% on top, which made the cache hit its cap a
    // third too early and then cleared *everything*.  That total wipe is what cost 2 seconds per
    // frame: every other frame re-decoded the whole menu texture set.
    const size_t neededBytes = static_cast<size_t>(slot.width) * static_cast<size_t>(slot.height) * sizeof(uint32_t);
    const bool fitsCache = neededBytes <= kMaxTexCacheBytes;
    if (fitsCache) {
        // Evict least-recently-used entries instead of dropping the whole cache.
        while (g_texCacheBytes + neededBytes > kMaxTexCacheBytes && !g_texCache.empty()) {
            auto victim = g_texCache.end();
            for (auto scan = g_texCache.begin(); scan != g_texCache.end(); ++scan) {
                if (victim == g_texCache.end() || scan->second.lastUse < victim->second.lastUse)
                    victim = scan;
            }
            if (victim == g_texCache.end()) break;
            g_texCacheBytes -= static_cast<size_t>(victim->second.width) *
                static_cast<size_t>(victim->second.height) * sizeof(uint32_t);
            g_texCache.erase(victim);
            ++g_texStatEvictions;
        }
    }

    // Decode target: the cache when the texture fits, otherwise a reusable scratch buffer, so an
    // oversized texture is never cached and never wipes the cache.  Allocation failure (a very
    // tight Old 3DS heap) frees the cache once and retries, then gives up and draws untextured.
    const size_t pixels = static_cast<size_t>(slot.width) * static_cast<size_t>(slot.height);
    DecodedTex* target = nullptr;
    for (int attempt = 0; attempt < 2 && target == nullptr; ++attempt) {
        try {
            if (fitsCache) {
                auto stale = g_texCache.find(key);
                if (stale != g_texCache.end()) {
                    g_texCacheBytes -= static_cast<size_t>(stale->second.width) *
                        static_cast<size_t>(stale->second.height) * sizeof(uint32_t);
                    g_texCache.erase(stale);
                }
                DecodedTex& tex = g_texCache[key];
                tex.width = slot.width;
                tex.height = slot.height;
                tex.format = slot.format;
                tex.hash = sampleHash;
                tex.lastUse = ++g_texTick;
                tex.rgba.assign(pixels, 0xFFFFFFFFu);
                g_texCacheBytes += neededBytes;
                target = &tex;
            } else {
                g_texScratch.width = slot.width;
                g_texScratch.height = slot.height;
                g_texScratch.format = slot.format;
                g_texScratch.hash = sampleHash;
                g_texScratch.mips.clear();
                g_texScratch.rgba.assign(pixels, 0xFFFFFFFFu);
                target = &g_texScratch;
            }
        } catch (const std::bad_alloc&) {
            g_texCache.erase(key);
            if (attempt == 0 && !g_texCache.empty()) {
                g_texCache.clear();
                g_texCacheBytes = 0;
                continue;
            }
            static uint32_t s_texFailLogs = 0;
            if (s_texFailLogs < 8) {
                ++s_texFailLogs;
                AppLog("[TexFail] %ux%u fmt=%u needs=%zuKB cache=%zuKB hostHeapFree=%zuKB\n",
                    slot.width, slot.height, slot.format, neededBytes / 1024,
                    g_texCacheBytes / 1024, HostHeapFreeKb());
            }
            return nullptr;
        }
    }
    if (target == nullptr) return nullptr;

    DecodedTex& tex = *target;
    ++g_texStatDecodes;
    g_texStatTexels += pixels;

    const int w = slot.width;
    const int h = slot.height;
    const uint8_t* p = src;

    switch (slot.format) {
    case 0x0: { // I4 (8x8 tiles)
        int bw = (w + 7) / 8;
        int bh = (h + 7) / 8;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; x += 2) {
                        uint8_t byte = *p++;
                        uint8_t i0 = (byte >> 4) * 17u;
                        uint8_t i1 = (byte & 0xFu) * 17u;
                        int px0 = bx * 8 + x, py = by * 8 + y;
                        if (px0 < w && py < h) tex.rgba[py * w + px0] = (i0 << 24) | (i0 << 16) | (i0 << 8) | i0;
                        if (px0 + 1 < w && py < h) tex.rgba[py * w + px0 + 1] = (i1 << 24) | (i1 << 16) | (i1 << 8) | i1;
                    }
                }
            }
        }
        break;
    }
    case 0x1: { // I8 (8x4 tiles)
        int bw = (w + 7) / 8;
        int bh = (h + 3) / 4;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        uint8_t v = *p++;
                        int px = bx * 8 + x, py = by * 4 + y;
                        if (px < w && py < h) tex.rgba[py * w + px] = (v << 24) | (v << 16) | (v << 8) | v;
                    }
                }
            }
        }
        break;
    }
    case 0x2: { // IA4 (8x4 tiles)
        int bw = (w + 7) / 8;
        int bh = (h + 3) / 4;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        uint8_t byte = *p++;
                        // GX IA4: intensity in the high nibble, alpha in the low nibble.
                        // Swapping them made every font glyph sample alpha from its intensity
                        // (and vice versa), so menu text rendered invisible.
                        uint32_t v = (byte >> 4) * 17u;
                        uint32_t a = (byte & 0xFu) * 17u;
                        int px = bx * 8 + x, py = by * 4 + y;
                        if (px < w && py < h) tex.rgba[py * w + px] = (v << 24) | (v << 16) | (v << 8) | a;
                    }
                }
            }
        }
        break;
    }
    case 0x3: { // IA8 (4x4 tiles)
        int bw = (w + 3) / 4;
        int bh = (h + 3) / 4;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        // GX IA8 is stored intensity first, then alpha.
                        uint32_t v = p[0];
                        uint32_t a = p[1];
                        p += 2;
                        int px = bx * 4 + x, py = by * 4 + y;
                        if (px < w && py < h) tex.rgba[py * w + px] = (v << 24) | (v << 16) | (v << 8) | a;
                    }
                }
            }
        }
        break;
    }
    case 0x4:   // RGB565 (4x4 tiles)
    case 0x5: { // RGB5A3 (4x4 tiles)
        int bw = (w + 3) / 4;
        int bh = (h + 3) / 4;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        uint16_t c = ReadBE16(p);
                        p += 2;
                        int px = bx * 4 + x, py = by * 4 + y;
                        if (px < w && py < h) {
                            tex.rgba[py * w + px] = (slot.format == 0x4) ? DecodeRGB565(c) : DecodeRGB5A3(c);
                        }
                    }
                }
            }
        }
        break;
    }
    case 0x6: { // RGBA8 (4x4 tiles, 64 bytes per tile: 32B AR + 32B GB)
        int bw = (w + 3) / 4;
        int bh = (h + 3) / 4;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                const uint8_t* ar = p;
                const uint8_t* gb = p + 32;
                p += 64;
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        int idx = (y * 4 + x) * 2;
                        uint32_t a = ar[idx];
                        uint32_t r = ar[idx + 1];
                        uint32_t g = gb[idx];
                        uint32_t b = gb[idx + 1];
                        int px = bx * 4 + x, py = by * 4 + y;
                        if (px < w && py < h) tex.rgba[py * w + px] = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                }
            }
        }
        break;
    }
    case 0xE: { // CMPR (DXT1 8x8 macro-blocks of 2x2 4x4 sub-blocks)
        int bw = (w + 7) / 8;
        int bh = (h + 7) / 8;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                for (int sby = 0; sby < 2; ++sby) {
                    for (int sbx = 0; sbx < 2; ++sbx) {
                        uint16_t c0 = ReadBE16(p);
                        uint16_t c1 = ReadBE16(p + 2);
                        uint32_t bits = ReadBE32(p + 4);
                        p += 8;
                        uint32_t pal[4];
                        pal[0] = DecodeRGB565(c0);
                        pal[1] = DecodeRGB565(c1);
                        uint32_t r0 = (pal[0] >> 24) & 0xFFu, g0 = (pal[0] >> 16) & 0xFFu, b0 = (pal[0] >> 8) & 0xFFu;
                        uint32_t r1 = (pal[1] >> 24) & 0xFFu, g1 = (pal[1] >> 16) & 0xFFu, b1 = (pal[1] >> 8) & 0xFFu;
                        if (c0 > c1) {
                            pal[2] = (((2 * r0 + r1) / 3) << 24) | (((2 * g0 + g1) / 3) << 16) | (((2 * b0 + b1) / 3) << 8) | 0xFFu;
                            pal[3] = (((r0 + 2 * r1) / 3) << 24) | (((g0 + 2 * g1) / 3) << 16) | (((b0 + 2 * b1) / 3) << 8) | 0xFFu;
                        } else {
                            pal[2] = (((r0 + r1) / 2) << 24) | (((g0 + g1) / 2) << 16) | (((b0 + b1) / 2) << 8) | 0xFFu;
                            pal[3] = 0x00000000u;
                        }
                        for (int y = 0; y < 4; ++y) {
                            for (int x = 0; x < 4; ++x) {
                                int shift = 30 - (y * 4 + x) * 2;
                                uint32_t code = (bits >> shift) & 3u;
                                int px = bx * 8 + sbx * 4 + x;
                                int py = by * 8 + sby * 4 + y;
                                if (px < w && py < h) {
                                    tex.rgba[py * w + px] = pal[code];
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }
    // Textures are authored for the Wii's 608x456 canvas but are never displayed larger than the
    // 400x240 top screen, so halve anything bigger than the screen (once per decode, bilinear-
    // equivalent box filter).  UVs stay 0..1 so nothing else changes; this cuts the texture cache
    // roughly in half and makes every software sample cheaper (two texel fetches instead of four
    // in the common case).  tools/optimize_menu_textures.py in MKW-3DS does the same offline, with
    // a measured per-sprite scale, and reports the same win.
    if (tex.width > 512 || tex.height > 448) {
        const int nw = (tex.width + 1) / 2;
        const int nh = (tex.height + 1) / 2;
        std::vector<uint32_t> small(static_cast<size_t>(nw) * static_cast<size_t>(nh));
        const int sw = tex.width;
        const int sh = tex.height;
        for (int y = 0; y < nh; ++y) {
            const int sy0 = std::min(sh - 1, y * 2);
            const int sy1 = std::min(sh - 1, y * 2 + 1);
            for (int x = 0; x < nw; ++x) {
                const int sx0 = std::min(sw - 1, x * 2);
                const int sx1 = std::min(sw - 1, x * 2 + 1);
                const uint32_t c0 = tex.rgba[static_cast<size_t>(sy0) * sw + sx0];
                const uint32_t c1 = tex.rgba[static_cast<size_t>(sy0) * sw + sx1];
                const uint32_t c2 = tex.rgba[static_cast<size_t>(sy1) * sw + sx0];
                const uint32_t c3 = tex.rgba[static_cast<size_t>(sy1) * sw + sx1];
                uint32_t out = 0;
                for (int shift = 24; shift >= 0; shift -= 8) {
                    const uint32_t sum = ((c0 >> shift) & 0xFFu) + ((c1 >> shift) & 0xFFu) +
                                         ((c2 >> shift) & 0xFFu) + ((c3 >> shift) & 0xFFu);
                    out |= ((sum + 2u) / 4u) << shift;
                }
                small[static_cast<size_t>(y) * nw + x] = out;
            }
        }
        tex.rgba.swap(small);
        const size_t oldBytes = static_cast<size_t>(tex.width) * tex.height * sizeof(uint32_t);
        const size_t newBytes = static_cast<size_t>(nw) * static_cast<size_t>(nh) * sizeof(uint32_t);
        if (target != &g_texScratch && g_texCacheBytes >= oldBytes) g_texCacheBytes -= oldBytes - newBytes;
        tex.width = static_cast<uint16_t>(nw);
        tex.height = static_cast<uint16_t>(nh);
    }
    return &tex;
}

static inline float WrapTexCoord(float t, bool repeat) {
    if (!repeat) return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t - std::floor(t);
}

static void BuildMipChain(DecodedTex& tex) {
    tex.mips.clear();
    int w = tex.width;
    int h = tex.height;
    const std::vector<uint32_t>* src = &tex.rgba;
    while (w > 1 || h > 1) {
        const int nw = w > 1 ? w / 2 : 1;
        const int nh = h > 1 ? h / 2 : 1;
        std::vector<uint32_t> level(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                auto accumulate = [&](int sx, int sy) -> uint32_t {
                    sx = sx < w ? sx : w - 1;
                    sy = sy < h ? sy : h - 1;
                    return (*src)[sy * w + sx];
                };
                const uint32_t c0 = accumulate(x * 2, y * 2);
                const uint32_t c1 = accumulate(x * 2 + 1, y * 2);
                const uint32_t c2 = accumulate(x * 2, y * 2 + 1);
                const uint32_t c3 = accumulate(x * 2 + 1, y * 2 + 1);
                uint32_t out = 0;
                for (int shift = 24; shift >= 0; shift -= 8) {
                    const uint32_t sum = ((c0 >> shift) & 0xFFu) + ((c1 >> shift) & 0xFFu) +
                                         ((c2 >> shift) & 0xFFu) + ((c3 >> shift) & 0xFFu);
                    out |= ((sum + 2u) / 4u) << shift;
                }
                level[static_cast<size_t>(y) * nw + x] = out;
            }
        }
        tex.mips.push_back(std::move(level));
        w = nw;
        h = nh;
        src = &tex.mips.back();
    }
}

static inline uint32_t SampleLevelBilinear(const std::vector<uint32_t>& data, int w, int h,
    float u, float v, bool repeatS, bool repeatT) {
    auto wrapIndex = [](int i, int n, bool repeat) {
        if (repeat) {
            i %= n;
            if (i < 0) i += n;
            return i;
        }
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    };
    const float fu = WrapTexCoord(u, repeatS);
    const float fv = WrapTexCoord(v, repeatT);
    const float fx = fu * static_cast<float>(w) - 0.5f;
    const float fy = fv * static_cast<float>(h) - 0.5f;
    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const float ax = fx - static_cast<float>(ix);
    const float ay = fy - static_cast<float>(iy);
    const int x0 = wrapIndex(ix, w, repeatS);
    const int x1 = wrapIndex(ix + 1, w, repeatS);
    const int y0 = wrapIndex(iy, h, repeatT);
    const int y1 = wrapIndex(iy + 1, h, repeatT);
    const uint32_t c00 = data[static_cast<size_t>(y0) * w + x0];
    const uint32_t c10 = data[static_cast<size_t>(y0) * w + x1];
    const uint32_t c01 = data[static_cast<size_t>(y1) * w + x0];
    const uint32_t c11 = data[static_cast<size_t>(y1) * w + x1];
    auto lerpChannel = [&](int shift) {
        const float t00 = static_cast<float>((c00 >> shift) & 0xFFu);
        const float t10 = static_cast<float>((c10 >> shift) & 0xFFu);
        const float t01 = static_cast<float>((c01 >> shift) & 0xFFu);
        const float t11 = static_cast<float>((c11 >> shift) & 0xFFu);
        const float top = t00 + (t10 - t00) * ax;
        const float bottom = t01 + (t11 - t01) * ax;
        const float value = top + (bottom - top) * ay;
        return static_cast<uint32_t>(value + 0.5f) & 0xFFu;
    };
    return (lerpChannel(24) << 24) | (lerpChannel(16) << 16) | (lerpChannel(8) << 8) | lerpChannel(0);
}

static inline uint32_t SampleTexture(const DecodedTex* tex, const TexSlot& slot, float u, float v, float lod = 0.0f) {
    if (!tex || tex->rgba.empty()) return 0xFFFFFFFFu;
    const bool repeatS = slot.wrapS != 0;
    const bool repeatT = slot.wrapT != 0;
    int level = 0;
    if (lod > 0.0f && g_gx.projType != 1) {
        if (tex->mips.empty()) {
            BuildMipChain(const_cast<DecodedTex&>(*tex));
            // Mips are real memory for the cache budget too (previously uncounted, which let the
            // cache grow a third past its cap and thrash).
            size_t mipBytes = 0;
            for (const auto& levelData : tex->mips) mipBytes += levelData.size() * sizeof(uint32_t);
            g_texCacheBytes += mipBytes;
        }
    }
    if (lod > 0.0f && !tex->mips.empty()) {
        level = static_cast<int>(lod + 0.5f);
        if (level > static_cast<int>(tex->mips.size())) level = static_cast<int>(tex->mips.size());
    }
    if (level <= 0) {
        return SampleLevelBilinear(tex->rgba, tex->width, tex->height, u, v, repeatS, repeatT);
    }
    const std::vector<uint32_t>& data = tex->mips[static_cast<size_t>(level - 1)];
    int w = tex->width, h = tex->height;
    for (int i = 0; i < level; ++i) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return SampleLevelBilinear(data, w, h, u, v, repeatS, repeatT);
}

static inline bool BlendPixel(int x, int y, float z, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, bool ignoreDepth) {
    if (x < 0 || x >= kScreenW || y < 0 || y >= kScreenH) return false;
    if (g_gx.scissorSet && (x < g_gx.clipX0 || x >= g_gx.clipX1 || y < g_gx.clipY0 || y >= g_gx.clipY1)) return false;
    if (sa == 0) return false; // Discard fully transparent pixels
    int idx = y * kScreenW + x;
    if (!ignoreDepth && g_gx.depthCompare) {
        if (g_gx.depthFunc == 0) return false; // GX_NEVER
        if (z > g_fbDepth[idx] + 1e-4f && g_gx.depthFunc != 7) return false;
    }
    if (!ignoreDepth && g_gx.depthUpdate) {
        g_fbDepth[idx] = z;
    }
    if (g_gx.blendMode == 1 || sa < 255) { // Alpha blend whenever enabled or pixel has alpha < 255
        uint32_t dst = g_fbColor[idx];
        uint32_t dr = (dst >> 24) & 0xFFu;
        uint32_t dg = (dst >> 16) & 0xFFu;
        uint32_t db = (dst >> 8)  & 0xFFu;
        uint32_t a = sa;
        uint32_t invA = 255u - a;
        uint32_t r = (sr * a + dr * invA) / 255u;
        uint32_t g = (sg * a + dg * invA) / 255u;
        uint32_t b = (sb * a + db * invA) / 255u;
        g_fbColor[idx] = (r << 24) | (g << 16) | (b << 8) | 0xFFu;
    } else {
        g_fbColor[idx] = (static_cast<uint32_t>(sr) << 24) |
                         (static_cast<uint32_t>(sg) << 16) |
                         (static_cast<uint32_t>(sb) << 8)  |
                         0xFFu;
    }
    return true;
}

// --- TEV combiner -----------------------------------------------------------------
// Mario Kart Wii drives almost every material through the TEV. The original bridge shaded every
// pixel as "vertex * texture", which is only the default MODULATE setup; REPLACE, PASS, ADD, SUB
// and the konst/tev-register inputs all came out wrong. These helpers evaluate the per-stage
// combiner against the sampled texture and the interpolated rasterised colour.

static inline void TevColorArg(uint32_t sel, const float prevColor[4], float prevAlpha,
                               const float texColor[4], const float ras[4],
                               const uint8_t kColor[4], float out[4]) {
    switch (sel) {
    case 0:  std::memcpy(out, prevColor, sizeof(float) * 4); return;                     // CPREV
    case 1:  out[0]=out[1]=out[2]=prevAlpha; out[3]=prevAlpha; return;                   // APREV
    case 2:  for (int i=0;i<4;++i) out[i] = g_gx.tevColor[0][i] / 255.0f; return;        // C0
    case 3:  for (int i=0;i<4;++i) out[i] = g_gx.tevColor[1][i] / 255.0f; return;        // C1
    case 4:  for (int i=0;i<4;++i) out[i] = g_gx.tevColor[2][i] / 255.0f; return;        // C2
    case 5:  out[0]=out[1]=out[2]=g_gx.tevColor[0][3]/255.0f; out[3]=out[2]; return;     // A0
    case 6:  out[0]=out[1]=out[2]=g_gx.tevColor[1][3]/255.0f; out[3]=out[2]; return;     // A1
    case 7:  out[0]=out[1]=out[2]=g_gx.tevColor[2][3]/255.0f; out[3]=out[2]; return;     // A2
    case 8:  std::memcpy(out, texColor, sizeof(float) * 4); return;                      // TEXC
    case 9:  out[0]=out[1]=out[2]=texColor[3]; out[3]=texColor[3]; return;               // TEXA
    case 10: std::memcpy(out, ras, sizeof(float) * 4); return;                           // RASC
    case 11: out[0]=out[1]=out[2]=ras[3]; out[3]=ras[3]; return;                         // RASA
    case 12: out[0]=out[1]=out[2]=out[3]=1.0f; return;                                   // ONE
    case 13: out[0]=out[1]=out[2]=out[3]=0.5f; return;                                   // HALF
    case 14: for (int i=0;i<4;++i) out[i] = kColor[i] / 255.0f; return;                  // KONST
    default: out[0]=out[1]=out[2]=out[3]=0.0f; return;                                   // ZERO
    }
}

static inline float TevAlphaArg(uint32_t sel, const float prevColor[4], float prevAlpha,
                                const float texColor[4], const float ras[4],
                                const uint8_t kColor[4]) {
    switch (sel) {
    case 0: return prevAlpha;                       // APREV
    case 1: return g_gx.tevColor[0][3] / 255.0f;   // A0
    case 2: return g_gx.tevColor[1][3] / 255.0f;   // A1
    case 3: return g_gx.tevColor[2][3] / 255.0f;   // A2
    case 4: return texColor[3];                    // TEXA
    case 5: return ras[3];                         // RASA
    case 6: return kColor[3] / 255.0f;             // KONST
    default: return 0.0f;                          // ZERO
    }
}

static inline float TevApplyOp(float a, float b, float c, float d, uint32_t op,
                               uint32_t bias, uint32_t scale, bool clamp) {
    // Matches aurora's reference: out = (mix(a,b,c) + d + bias) * scale, negated for SUB.
    // mix(a,b,c) is a*(1-c) + b*c, i.e. the TEV "lerp" of the two colour args.
    float mix = a + (b - a) * c;
    float r = mix + d;
    if (bias == 1u) r += 0.5f;
    else if (bias == 2u) r -= 0.5f;
    if (op == 1u) r = -r;                       // GX_TEV_SUB
    static const float kScale[4] = {1.0f, 2.0f, 4.0f, 0.5f};
    r *= kScale[scale & 3u];
    if (clamp) r = std::max(0.0f, std::min(1.0f, r));
    return r;
}

static inline void TevSwapChannel(uint32_t swapSel, const float in[4], float out[4]) {
    const uint8_t* t = g_gx.tevSwapTable[swapSel & 3u];
    out[0] = in[t[0] & 3u];
    out[1] = in[t[1] & 3u];
    out[2] = in[t[2] & 3u];
    out[3] = in[t[3] & 3u];
}

static void EvaluateTevPixel(const float ras[4], const float texColor[4], const uint8_t kColor[4],
                             float out[4]) {
    float prevColor[4] = {ras[0], ras[1], ras[2], ras[3]};
    float prevAlpha = ras[3];
    const uint32_t stages = std::max<uint32_t>(1u, std::min<uint32_t>(g_gx.tevNumStages, 16u));
    for (uint32_t s = 0; s < stages; ++s) {
        float a[4], b[4], c[4], d[4];
        TevColorArg(g_gx.tevColorIn[s][0], prevColor, prevAlpha, texColor, ras, kColor, a);
        TevColorArg(g_gx.tevColorIn[s][1], prevColor, prevAlpha, texColor, ras, kColor, b);
        TevColorArg(g_gx.tevColorIn[s][2], prevColor, prevAlpha, texColor, ras, kColor, c);
        TevColorArg(g_gx.tevColorIn[s][3], prevColor, prevAlpha, texColor, ras, kColor, d);
        float col[4];
        for (int i = 0; i < 3; ++i) {
            col[i] = TevApplyOp(a[i], b[i], c[i], d[i], g_gx.tevColorOp[s],
                                g_gx.tevColorBias[s], g_gx.tevColorScale[s], g_gx.tevColorClamp[s]);
        }
        float aa = TevAlphaArg(g_gx.tevAlphaIn[s][0], prevColor, prevAlpha, texColor, ras, kColor);
        float ab = TevAlphaArg(g_gx.tevAlphaIn[s][1], prevColor, prevAlpha, texColor, ras, kColor);
        float ac = TevAlphaArg(g_gx.tevAlphaIn[s][2], prevColor, prevAlpha, texColor, ras, kColor);
        float ad = TevAlphaArg(g_gx.tevAlphaIn[s][3], prevColor, prevAlpha, texColor, ras, kColor);
        col[3] = TevApplyOp(aa, ab, ac, ad, g_gx.tevAlphaOp[s],
                            g_gx.tevAlphaBias[s], g_gx.tevAlphaScale[s], g_gx.tevAlphaClamp[s]);
        float sw[4];
        TevSwapChannel(g_gx.tevSwapRas[s], col, sw);
        std::memcpy(prevColor, sw, sizeof(prevColor));
        prevAlpha = sw[3];
    }
    std::memcpy(out, prevColor, sizeof(prevColor));
}

static inline bool AlphaTestPass(float alpha) {
    if (g_gx.alphaComp0 == 7u && g_gx.alphaComp1 == 7u) return true; // both ALWAYS
    auto test = [](uint32_t fn, float ref, float a) -> bool {
        const float r = ref / 255.0f;
        switch (fn) {
        case 0: return a <  r; // NEVER? GX_NEVER=0
        case 1: return a <  r; // LESS
        case 2: return a == r; // EQUAL
        case 3: return a <= r; // LEQUAL
        case 4: return a >  r; // GREATER
        case 5: return a != r; // NEQUAL
        case 6: return a >= r; // GEQUAL
        default: return true;  // ALWAYS
        }
    };
    const bool t0 = test(g_gx.alphaComp0, static_cast<float>(g_gx.alphaRef0), alpha);
    const bool t1 = test(g_gx.alphaComp1, static_cast<float>(g_gx.alphaRef1), alpha);
    switch (g_gx.alphaCompOp) {
    case 0: return t0 && t1; // AND
    case 1: return t0 || t1; // OR
    case 2: return t0 != t1; // XOR
    default: return t0 == t1; // XNOR
    }
}

static void RasterizeTriangle(const GxVertex& v0, const GxVertex& v1, const GxVertex& v2, uint32_t pnMtxIdx) {
    EnsureInitialized();
    const uint64_t profStart = ProfNow();
    ++g_profRasterTris;
    struct ProfGuard {
        uint64_t start;
        ~ProfGuard() { g_profRasterTicks += ProfNow() - start; }
    } profGuard{profStart};
    g_gx.lastTrianglePixels = 0;
    if (g_gx.projType == 0) ++g_gx.tri3DEntered;
    const bool tracePerspective = (g_gxFrame == kGxTraceFrame) && g_gx.projType == 0;
    const float* rawMv = g_gx.pnMtx[pnMtxIdx < 10 ? pnMtxIdx : 0];
    float mv[12];
    // The debug chase camera is a race-path aid. Applying it to the title/menu scenes replaces
    // their own camera and points the view at the sky, so only use it in the race section.
    const uint32_t dbgSecMgr = Memory::Read32(0x809C1E38u);
    const bool dbgRaceSection = dbgSecMgr && Memory::Read32(dbgSecMgr + 4) == 0x20u;
    if (pnMtxIdx != 0 && std::getenv("MKW_SKIN_VIEW") && g_gx.projType == 0) {
        // Skinned models load the view matrix into pnMtx[0] and the bone matrices into
        // pnMtx[1..9]. The vertex transform is view * bone, not the bone alone.
        MulAffine3x4(g_gx.pnMtx[0], g_gx.pnMtx[pnMtxIdx < 10 ? pnMtxIdx : 0], mv);
    } else if (kDebugCamera && dbgRaceSection && g_gx.projType == 0) {
        // Compose: debug view * the object's own model matrix. Replacing the matrix outright
        // discards per-object transforms, which puts every kart at the wrong place.
        float view[12];
        BuildDebugCamera(view);
        float model[12];
        std::memcpy(model, rawMv, sizeof(model));
        SanitizeModelViewMatrix(model);
        MulAffine3x4(view, model, mv);
    } else {
        std::memcpy(mv, rawMv, sizeof(mv));
    }
    if (g_gx.projType == 0 && g_gxFrame >= 200u && pnMtxIdx != 0) {
        static uint32_t s_tri3DLogs = 0;
        if (s_tri3DLogs < 8) {
            ++s_tri3DLogs;
            const float* M = g_gx.pnMtx[pnMtxIdx < 10 ? pnMtxIdx : 0];
            AppLog("[Tri3D] n=%u pn=%u pos0=(%.1f,%.1f,%.1f) boneT=(%.1f,%.1f,%.1f) viewT=(%.1f,%.1f,%.1f) composedT=(%.1f,%.1f,%.1f) skin=%d\n",
                s_tri3DLogs, pnMtxIdx, v0.pos[0], v0.pos[1], v0.pos[2], M[3], M[7], M[11],
                g_gx.pnMtx[0][3], g_gx.pnMtx[0][7], g_gx.pnMtx[0][11],
                mv[3], mv[7], mv[11], std::getenv("MKW_SKIN_VIEW") ? 1 : 0);
        }
    }
    if (g_gx.projType == 1) {
        // In 2D ortho mode, enforce clean 2D identity modelview
        mv[0] = 1.0f; mv[1] = 0.0f; mv[2] = 0.0f; mv[3] = 0.0f;
        mv[4] = 0.0f; mv[5] = 1.0f; mv[6] = 0.0f; mv[7] = 0.0f;
        mv[8] = 0.0f; mv[9] = 0.0f; mv[10] = 1.0f; mv[11] = 0.0f;
    } else {
        SanitizeModelViewMatrix(mv);
    }

    struct ClipVtx {
        float cx, cy, cz, cw;
        float sx, sy, sz, invW;
        float u, v;
        uint8_t r, g, b, a;
    } cv[3];

    const GxVertex* vs[3] = {&v0, &v1, &v2};
    const float efbW = (g_gx.efbWidth > 0) ? static_cast<float>(g_gx.efbWidth) : 608.0f;
    const float efbH = (g_gx.efbHeight > 0) ? static_cast<float>(g_gx.efbHeight) : 456.0f;
    const bool fullVp = (g_gx.vpLeft <= 4.0f && g_gx.vpTop <= 4.0f && g_gx.vpWidth >= 500.0f && g_gx.vpHeight >= 400.0f);

    for (int i = 0; i < 3; ++i) {
        const GxVertex& vin = *vs[i];
        float ex = mv[0] * vin.pos[0] + mv[1] * vin.pos[1] + mv[2]  * vin.pos[2] + mv[3];
        float ey = mv[4] * vin.pos[0] + mv[5] * vin.pos[1] + mv[6]  * vin.pos[2] + mv[7];
        float ez = mv[8] * vin.pos[0] + mv[9] * vin.pos[1] + mv[10] * vin.pos[2] + mv[11];

        const auto& P = g_gx.proj;
        float cx = P[0][0] * ex + P[0][1] * ey + P[0][2] * ez + P[0][3];
        float cy = P[1][0] * ex + P[1][1] * ey + P[1][2] * ez + P[1][3];
        float cz = P[2][0] * ex + P[2][1] * ey + P[2][2] * ez + P[2][3];
        float cw = P[3][0] * ex + P[3][1] * ey + P[3][2] * ez + P[3][3];

        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || !std::isfinite(cw)) {
            if (tracePerspective && ++g_perspectiveTrace.nonFinite == 1) {
                AppLog("[GX Perspective] draw=%u non-finite matrix=%u position=(%f,%f,%f) eye=(%f,%f,%f) clip=(%f,%f,%f,%f)\n",
                    g_traceDrawOrder, pnMtxIdx, vin.pos[0], vin.pos[1], vin.pos[2], ex, ey, ez, cx, cy, cz, cw);
            }
            return;
        }
        // Reject vertices behind near plane for perspective projection
        if (g_gx.projType == 0 && cw <= 0.05f) {
            if (tracePerspective && ++g_perspectiveTrace.nearPlane == 1) {
                AppLog("[GX Perspective] draw=%u near plane matrix=%u eyeZ=%f clipW=%f\n",
                    g_traceDrawOrder, pnMtxIdx, ez, cw);
            }
            return;
        }
        if ( std::fabs(cw) < 1e-6f) cw = 1.0f;
        float invW = 1.0f / cw;
        float ndcX = cx * invW;
        float ndcY = cy * invW;
        float ndcZ = cz * invW;

        // Y convention differs between the two projection kinds.  For the 2D layout projection the
        // guest already maps canvas y (grows downward) onto NDC, so flipping here mirrors the whole
        // menu vertically - the title logo ended up at the bottom instead of the top.  The
        // perspective path keeps the flip.
        const float ySign = -1.0f;  // keep the established mapping until the 2D UV convention is fixed
        if (fullVp) {
            cv[i].sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(kScreenW);
            cv[i].sy = (0.5f + ySign * ndcY * 0.5f) * static_cast<float>(kScreenH);
        } else {
            float efbX = g_gx.vpLeft + (ndcX * 0.5f + 0.5f) * g_gx.vpWidth;
            float efbY = g_gx.vpTop  + (0.5f + ySign * ndcY * 0.5f) * g_gx.vpHeight;
            cv[i].sx = efbX * (static_cast<float>(kScreenW) / efbW);
            cv[i].sy = efbY * (static_cast<float>(kScreenH) / efbH);
        }
        cv[i].cx = cx;
        cv[i].cy = cy;
        cv[i].cz = cz;
        cv[i].cw = cw;
        cv[i].sz = ndcZ;
        cv[i].invW = invW;
        cv[i].u = vin.uv0[0];
        cv[i].v = vin.uv0[1];
        if (vin.hasColor0) {
            cv[i].r = vin.color0[0];
            cv[i].g = vin.color0[1];
            cv[i].b = vin.color0[2];
            cv[i].a = vin.color0[3];
        } else {
            cv[i].r = g_gx.chanMatColor[0][0];
            cv[i].g = g_gx.chanMatColor[0][1];
            cv[i].b = g_gx.chanMatColor[0][2];
            cv[i].a = g_gx.chanMatColor[0][3];
        }
        // Diffuse lighting (GX channel 0): lit = ambient + material * lightColour * max(0, N.L).
        // Without this every untextured surface renders as a flat blob.
        if (g_gx.lightEnabled) {
            float nx = vin.nrm[0], ny = vin.nrm[1], nz = vin.nrm[2];
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
            float lx = g_gx.light0Dir[0], ly = g_gx.light0Dir[1], lz = g_gx.light0Dir[2];
            // The guest loads its light object through GX internals we do not model, so light0Dir
            // usually stays at the power-on default (0,0,1). Substitute a plausible key light from
            // above/behind so surfaces shade instead of rendering as flat blobs.
            if (!g_gx.light0DirWritten) {
                lx = 0.35f; ly = -0.88f; lz = 0.32f;
            }
            const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
            if (ll > 1e-6f) { lx /= ll; ly /= ll; lz /= ll; }
            float ndl = nx * lx + ny * ly + nz * lz;
            if (ndl < 0.0f) ndl = 0.0f;
            const float lr = g_gx.light0Color[0] / 255.0f;
            const float lg = g_gx.light0Color[1] / 255.0f;
            const float lb = g_gx.light0Color[2] / 255.0f;
            const float mr = g_gx.chanMatColor[0][0] / 255.0f;
            const float mg = g_gx.chanMatColor[0][1] / 255.0f;
            const float mb = g_gx.chanMatColor[0][2] / 255.0f;
            const float ar = g_gx.chanAmbColor[0][0] / 255.0f;
            const float ag = g_gx.chanAmbColor[0][1] / 255.0f;
            const float ab = g_gx.chanAmbColor[0][2] / 255.0f;
            cv[i].r *= std::min(1.0f, ar + mr * ndl * lr);
            cv[i].g *= std::min(1.0f, ag + mg * ndl * lg);
            cv[i].b *= std::min(1.0f, ab + mb * ndl * lb);
        }
    }


    float area = (cv[1].sx - cv[0].sx) * (cv[2].sy - cv[0].sy) -
                 (cv[1].sy - cv[0].sy) * (cv[2].sx - cv[0].sx);
    if (std::fabs(area) < 1e-4f || std::fabs(area) > 1e7f) {
        if (tracePerspective && ++g_perspectiveTrace.area <= 5) {
            if (g_perspectiveTrace.area == 1) {
                for (int m = 0; m < 10; ++m) {
                    AppLog("[GX MatrixDump %d] r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
                        m,
                        g_gx.pnMtx[m][0], g_gx.pnMtx[m][1], g_gx.pnMtx[m][2], g_gx.pnMtx[m][3],
                        g_gx.pnMtx[m][4], g_gx.pnMtx[m][5], g_gx.pnMtx[m][6], g_gx.pnMtx[m][7],
                        g_gx.pnMtx[m][8], g_gx.pnMtx[m][9], g_gx.pnMtx[m][10], g_gx.pnMtx[m][11]);
                }
            }
            AppLog("[GX Perspective Area] draw=%u mtx=%u area=%g sx=(%g,%g,%g) sy=(%g,%g,%g) ndcZ=(%g,%g,%g) invW=(%g,%g,%g) pos0=(%g,%g,%g) pos1=(%g,%g,%g) pos2=(%g,%g,%g)\n",
                g_traceDrawOrder, pnMtxIdx, area,
                cv[0].sx, cv[1].sx, cv[2].sx,
                cv[0].sy, cv[1].sy, cv[2].sy,
                cv[0].sz, cv[1].sz, cv[2].sz,
                cv[0].invW, cv[1].invW, cv[2].invW,
                v0.pos[0], v0.pos[1], v0.pos[2],
                v1.pos[0], v1.pos[1], v1.pos[2],
                v2.pos[0], v2.pos[1], v2.pos[2]);
            AppLog("[GX Perspective MV] mtx=%u r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
                pnMtxIdx,
                mv[0], mv[1], mv[2], mv[3],
                mv[4], mv[5], mv[6], mv[7],
                mv[8], mv[9], mv[10], mv[11]);
            AppLog("[GX Perspective Vtx0] c=(%g,%g,%g,%g) ndc=(%g,%g,%g) P00=%g P11=%g\n",
                cv[0].cx, cv[0].cy, cv[0].cz, cv[0].cw,
                cv[0].cx * cv[0].invW, cv[0].cy * cv[0].invW, cv[0].sz,
                g_gx.proj[0][0], g_gx.proj[1][1]);
        }
        return;
    }

    int minX = std::max(0, static_cast<int>(std::floor(std::min({cv[0].sx, cv[1].sx, cv[2].sx}))));
    int maxX = std::min(kScreenW - 1, static_cast<int>(std::ceil(std::max({cv[0].sx, cv[1].sx, cv[2].sx}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({cv[0].sy, cv[1].sy, cv[2].sy}))));
    int maxY = std::min(kScreenH - 1, static_cast<int>(std::ceil(std::max({cv[0].sy, cv[1].sy, cv[2].sy}))));
    if (minX > maxX || minY > maxY) {
        if (tracePerspective && ++g_perspectiveTrace.offscreen == 1) {
            AppLog("[GX Perspective] draw=%u offscreen matrix=%u clipped bounds=(%d,%d)-(%d,%d)\n",
                g_traceDrawOrder, pnMtxIdx, minX, minY, maxX, maxY);
        }
        return;
    }
    if (g_gx.projType == 0) ++g_gx.tri3DBounds;
    if (g_gx.projType == 1 && cv[0].a == 0 && cv[1].a == 0 && cv[2].a == 0) ++g_gx.tri2DAlphaZero;

    int texSlotIdx = -1;
    if (g_gx.texStageEnabled[0] && g_gx.stageTexMap[0] < 8 && g_gx.texSlots[g_gx.stageTexMap[0]].valid) {
        texSlotIdx = g_gx.stageTexMap[0];
    } else if (g_gx.activeTexSlot >= 0 && g_gx.activeTexSlot < 8 && g_gx.texSlots[g_gx.activeTexSlot].valid) {
        texSlotIdx = g_gx.activeTexSlot;
    } else if (g_gx.projType == 1) {
        for (int s = 0; s < 8; ++s) {
            if (g_gx.texSlots[s].valid) {
                texSlotIdx = s;
                break;
            }
        }
    }
    const TexSlot& slot = (texSlotIdx >= 0) ? g_gx.texSlots[texSlotIdx] : g_gx.texSlots[0];
    const DecodedTex* tex = (v0.hasUv0 && texSlotIdx >= 0 && slot.valid) ? GetOrDecodeTexture(slot) : nullptr;

    // Pick a mip level from the UV footprint of this triangle. Menu layouts tile tiny textures
    // over huge quads (a 32x32 checker repeated 160x); without this the sampler aliases them
    // into harsh 1px stripes instead of the intended soft gradient.
    float texLod = 0.0f;
    if (tex) {
        const float minU = std::min({cv[0].u, cv[1].u, cv[2].u});
        const float maxU = std::max({cv[0].u, cv[1].u, cv[2].u});
        const float minV = std::min({cv[0].v, cv[1].v, cv[2].v});
        const float maxV = std::max({cv[0].v, cv[1].v, cv[2].v});
        const float spanX = static_cast<float>(maxX - minX + 1);
        const float spanY = static_cast<float>(maxY - minY + 1);
        const float texelsX = (maxU - minU) * static_cast<float>(tex->width) / spanX;
        const float texelsY = (maxV - minV) * static_cast<float>(tex->height) / spanY;
        const float footprint = std::max(texelsX, texelsY);
        texLod = std::log2(std::max(footprint, 1e-6f));
    }
    const bool ignoreDepth = (g_gx.projType == 1); // Ortho 2D pass

    float invArea = 1.0f / area;
    float dw0_dx = (cv[1].sy - cv[2].sy) * invArea;
    float dw0_dy = (cv[2].sx - cv[1].sx) * invArea;
    float dw1_dx = (cv[2].sy - cv[0].sy) * invArea;
    float dw1_dy = (cv[0].sx - cv[2].sx) * invArea;

    float fx0 = static_cast<float>(minX) + 0.5f;
    float fy0 = static_cast<float>(minY) + 0.5f;
    float w0_row = ((cv[1].sx - fx0) * (cv[2].sy - fy0) - (cv[1].sy - fy0) * (cv[2].sx - fx0)) * invArea;
    float w1_row = ((cv[2].sx - fx0) * (cv[0].sy - fy0) - (cv[2].sy - fy0) * (cv[0].sx - fx0)) * invArea;

    ++g_gx.trisThisFrame;
    bool wrotePixel = false;

    for (int py = minY; py <= maxY; ++py) {
        float w0 = w0_row;
        float w1 = w1_row;
        for (int px = minX; px <= maxX; ++px) {
            float w2 = 1.0f - w0 - w1;
            if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                float z = w0 * cv[0].sz + w1 * cv[1].sz + w2 * cv[2].sz;
                float ras[4] = {
                    (w0 * cv[0].r + w1 * cv[1].r + w2 * cv[2].r) / 255.0f,
                    (w0 * cv[0].g + w1 * cv[1].g + w2 * cv[2].g) / 255.0f,
                    (w0 * cv[0].b + w1 * cv[1].b + w2 * cv[2].b) / 255.0f,
                    (w0 * cv[0].a + w1 * cv[1].a + w2 * cv[2].a) / 255.0f,
                };
                float texColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                if (tex) {
                    float pw0 = w0 * cv[0].invW;
                    float pw1 = w1 * cv[1].invW;
                    float pw2 = w2 * cv[2].invW;
                    float sumInvW = pw0 + pw1 + pw2;
                    float u = (std::fabs(sumInvW) > 1e-8f) ? ((pw0 * cv[0].u + pw1 * cv[1].u + pw2 * cv[2].u) / sumInvW)
                                                           : (w0 * cv[0].u + w1 * cv[1].u + w2 * cv[2].u);
                    float v = (std::fabs(sumInvW) > 1e-8f) ? ((pw0 * cv[0].v + pw1 * cv[1].v + pw2 * cv[2].v) / sumInvW)
                                                           : (w0 * cv[0].v + w1 * cv[1].v + w2 * cv[2].v);
                    uint32_t tc = SampleTexture(tex, slot, u, v, texLod);
                    texColor[0] = ((tc >> 24) & 0xFFu) / 255.0f;
                    texColor[1] = ((tc >> 16) & 0xFFu) / 255.0f;
                    texColor[2] = ((tc >> 8)  & 0xFFu) / 255.0f;
                    texColor[3] = (tc         & 0xFFu) / 255.0f;
                }
                // The TEV constant colour is selected per stage by GXSetTevKColorSel.
                const uint8_t* kColor = g_gx.tevKColor[g_gx.tevKColorSel[0] & 3u];
                float tevOut[4];
                if (g_layoutQuad) {
                    // Fixed modulate: layout panes always use vertex * texture.
                    tevOut[0] = ras[0] * texColor[0];
                    tevOut[1] = ras[1] * texColor[1];
                    tevOut[2] = ras[2] * texColor[2];
                    tevOut[3] = ras[3] * texColor[3];
                } else {
                    EvaluateTevPixel(ras, texColor, kColor, tevOut);
                }
                if (!g_layoutQuad && !AlphaTestPass(tevOut[3])) {
                    w0 += dw0_dx;
                    w1 += dw1_dx;
                    continue;
                }
                uint32_t r = static_cast<uint32_t>(std::max(0.0f, std::min(255.0f, tevOut[0] * 255.0f + 0.5f)));
                uint32_t g = static_cast<uint32_t>(std::max(0.0f, std::min(255.0f, tevOut[1] * 255.0f + 0.5f)));
                uint32_t b = static_cast<uint32_t>(std::max(0.0f, std::min(255.0f, tevOut[2] * 255.0f + 0.5f)));
                uint32_t a = static_cast<uint32_t>(std::max(0.0f, std::min(255.0f, tevOut[3] * 255.0f + 0.5f)));
                const bool written = BlendPixel(px, py, z, static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                    static_cast<uint8_t>(b), static_cast<uint8_t>(a), ignoreDepth);
                wrotePixel |= written;
                if (written && g_gx.trackTrianglePixels) ++g_gx.lastTrianglePixels;
                if (written && g_gx.projType == 0) ++g_gx.pixels3DThisFrame;
            }
            w0 += dw0_dx;
            w1 += dw1_dx;
        }
        w0_row += dw0_dy;
        w1_row += dw1_dy;
    }
    if (g_gx.projType == 1 && wrotePixel) ++g_gx.tri2DWritten;
    if (g_gx.projType == 1 && g_gxFrame == 296u && slot.width >= 512) {
        static uint32_t s_tri2DLogs = 0;
        if (s_tri2DLogs < 60) {
            ++s_tri2DLogs;
            AppLog("[Tri2D] n=%u bounds=(%d,%d)-(%d,%d) col0=%02X%02X%02X%02X col1=%02X%02X%02X%02X col2=%02X%02X%02X%02X uv0=(%.3f,%.3f) uv1=(%.3f,%.3f) uv2=(%.3f,%.3f) texSlot=%d tex=%08X %ux%u f%u wrote=%d\n",
                s_tri2DLogs, minX, minY, maxX, maxY,
                cv[0].r, cv[0].g, cv[0].b, cv[0].a,
                cv[1].r, cv[1].g, cv[1].b, cv[1].a,
                cv[2].r, cv[2].g, cv[2].b, cv[2].a,
                cv[0].u, cv[0].v, cv[1].u, cv[1].v, cv[2].u, cv[2].v,
                texSlotIdx, slot.dataAddr, slot.width, slot.height, slot.format, wrotePixel ? 1 : 0);
        }
    }
    if (tracePerspective) {
        ++g_perspectiveTrace.rasterized;
        if (wrotePixel) ++g_perspectiveTrace.wrotePixels;
    }
}

static uint32_t CalcCompByteSize(uint32_t attr, const VtxAttrFmt& fmt) {
    if (attr == 11 || attr == 12) { // Color0 / Color1
        switch (fmt.type) {
        case 0: return 2; // RGB565
        case 1: return 3; // RGB8
        case 2: return 4; // RGBX8
        case 3: return 2; // RGBA4
        case 4: return 3; // RGBA6
        case 5: return 4; // RGBA8
        default: return 4;
        }
    }
    uint32_t compBytes = 4;
    switch (fmt.type) {
    case 0: case 1: compBytes = 1; break; // U8 / S8
    case 2: case 3: compBytes = 2; break; // U16 / S16
    case 4: default: compBytes = 4; break; // F32
    }
    uint32_t numComps = 1;
    if (attr == 9) { // POS
        numComps = (fmt.cnt == 0) ? 2 : 3;
    } else if (attr == 10) { // NRM
        numComps = (fmt.cnt != 0) ? 9 : 3;
    } else if (attr >= 13 && attr <= 20) { // TEX0..7
        numComps = (fmt.cnt == 0) ? 1 : 2;
    }
    return compBytes * numComps;
}

static uint32_t CalcVtxByteSize(uint32_t vatIdx) {
    const auto* fmt = g_gx.vtxFmt[vatIdx & 7];
    uint32_t total = 0;
    for (uint32_t attr = 0; attr < 21; ++attr) {
        uint32_t desc = g_gx.vtxDesc[attr];
        if (desc == 0) continue;
        if (attr < 9) { // PNMTXIDX / TEXMTXIDX
            total += 1;
        } else if (desc == 1) { // DIRECT
            total += CalcCompByteSize(attr, fmt[attr]);
        } else if (desc == 2) { // INDEX8
            total += (attr == 10 && fmt[attr].cnt == 2) ? 3 : 1;
        } else if (desc == 3) { // INDEX16
            total += (attr == 10 && fmt[attr].cnt == 2) ? 6 : 2;
        }
    }
    return total;
}

static float ReadCompAsFloat(const uint8_t*& p, uint32_t type, uint8_t frac) {
    float scale = (frac > 0 && frac < 31) ? (1.0f / static_cast<float>(1u << frac)) : 1.0f;
    switch (type) {
    case 0: { uint8_t v = *p++; return static_cast<float>(v) * scale; }
    case 1: { int8_t v = static_cast<int8_t>(*p++); return static_cast<float>(v) * scale; }
    case 2: { uint16_t v = ReadBE16(p); p += 2; return static_cast<float>(v) * scale; }
    case 3: { int16_t v = static_cast<int16_t>(ReadBE16(p)); p += 2; return static_cast<float>(v) * scale; }
    case 4: default: { float v = ReadBEFloat(p); p += 4; return v; }
    }
}

static void ReadColorComp(const uint8_t*& p, uint32_t type, uint8_t out[4]) {
    switch (type) {
    case 0: { // RGB565
        uint16_t c = ReadBE16(p); p += 2;
        out[0] = ((c >> 11) & 0x1Fu) * 255u / 31u;
        out[1] = ((c >> 5)  & 0x3Fu) * 255u / 63u;
        out[2] = (c         & 0x1Fu) * 255u / 31u;
        out[3] = 255;
        break;
    }
    case 1: { // RGB8
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255;
        p += 3;
        break;
    }
    case 2: { // RGBX8
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255;
        p += 4;
        break;
    }
    case 3: { // RGBA4
        uint16_t c = ReadBE16(p); p += 2;
        out[0] = ((c >> 12) & 0xFu) * 17u;
        out[1] = ((c >> 8)  & 0xFu) * 17u;
        out[2] = ((c >> 4)  & 0xFu) * 17u;
        out[3] = (c         & 0xFu) * 17u;
        break;
    }
    case 4: { // RGBA6
        uint32_t c = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        p += 3;
        out[0] = ((c >> 18) & 0x3Fu) * 255u / 63u;
        out[1] = ((c >> 12) & 0x3Fu) * 255u / 63u;
        out[2] = ((c >> 6)  & 0x3Fu) * 255u / 63u;
        out[3] = (c         & 0x3Fu) * 255u / 63u;
        break;
    }
    case 5: default: { // RGBA8
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        p += 4;
        break;
    }
    }
}

static GxVertex DecodeVertex(const uint8_t*& p, uint32_t vatIdx, uint32_t& outPnMtx) {
    GxVertex v{};
    outPnMtx = g_gx.currentPnMtx;
    const auto* fmt = g_gx.vtxFmt[vatIdx & 7];

    for (uint32_t attr = 0; attr < 21; ++attr) {
        uint32_t desc = g_gx.vtxDesc[attr];
        if (desc == 0) continue;

        if (attr < 9) {
            uint8_t val = *p++;
            if (attr == 0) outPnMtx = (val / 3u) % 10u;
            continue;
        }

        const uint8_t* attrPtr = nullptr;
        if (desc == 1) { // DIRECT
            attrPtr = p;
            p += CalcCompByteSize(attr, fmt[attr]);
        } else { // INDEX8 or INDEX16
            uint32_t idx = 0;
            if (desc == 2) {
                idx = *p++;
                if (attr == 10 && fmt[attr].cnt == 2) p += 2; // Skip B/T indices
            } else {
                idx = ReadBE16(p);
                p += 2;
                if (attr == 10 && fmt[attr].cnt == 2) p += 4; // Skip B/T indices
            }
            uint32_t baseAddr = g_gx.vtxArray[attr].base;
            uint32_t stride = g_gx.vtxArray[attr].stride;
            if (stride == 0) stride = CalcCompByteSize(attr, fmt[attr]);
            if (baseAddr != 0) {
                uint32_t elemAddr = baseAddr + idx * stride;
                attrPtr = resolve_guest(elemAddr, 16);
                if (!attrPtr && elemAddr < 0x20000000u) {
                    attrPtr = resolve_guest(elemAddr | 0x80000000u, 16);
                }
            }
        }

        if (!attrPtr) continue;
        const uint8_t* rd = attrPtr;
        const auto& f = fmt[attr];
        if (attr == 9) { // POS
            v.pos[0] = ReadCompAsFloat(rd, f.type, f.frac);
            v.pos[1] = ReadCompAsFloat(rd, f.type, f.frac);
            v.pos[2] = (f.cnt == 0) ? 0.0f : ReadCompAsFloat(rd, f.type, f.frac);
        } else if (attr == 10) { // NRM
            v.nrm[0] = ReadCompAsFloat(rd, f.type, f.frac);
            v.nrm[1] = ReadCompAsFloat(rd, f.type, f.frac);
            v.nrm[2] = ReadCompAsFloat(rd, f.type, f.frac);
        } else if (attr == 11) { // CLR0
            ReadColorComp(rd, f.type, v.color0);
            v.hasColor0 = true;
        } else if (attr == 13) { // TEX0
            v.uv0[0] = ReadCompAsFloat(rd, f.type, f.frac);
            v.uv0[1] = (f.cnt == 0) ? 0.0f : ReadCompAsFloat(rd, f.type, f.frac);
            v.hasUv0 = true;
        }
    }
    return v;
}

static void HandleLoadBpReg(uint32_t val) {
    uint8_t reg = static_cast<uint8_t>((val >> 24) & 0xFFu);
    uint32_t payload = val & 0x00FFFFFFu;

    if (reg == 0x00) { // GEN_MODE
        g_gx.cullMode = (payload >> 14) & 3u;
    } else if (reg >= 0x28 && reg <= 0x2F) { // bp_tref (TEV order)
        uint32_t pair = reg - 0x28;
        for (uint32_t half = 0; half < 2; ++half) {
            uint32_t s = pair * 2 + half;
            if (s >= 16) continue;
            const uint32_t shift = half * 12;
            const uint32_t map = (payload >> shift) & 7u;
            const uint32_t coord = (payload >> (shift + 3)) & 7u;
            const bool enabled = ((payload >> (shift + 6)) & 1u) != 0;
            const uint32_t chan = (payload >> (shift + 7)) & 7u;
            g_gx.tevOrderMap[s] = static_cast<uint8_t>(map);
            g_gx.tevOrderCoord[s] = static_cast<uint8_t>(coord);
            g_gx.tevOrderEnabled[s] = enabled;
            g_gx.tevOrderChan[s] = static_cast<uint8_t>(chan);
            g_gx.stageTexMap[s] = static_cast<uint8_t>(map);
            g_gx.texStageEnabled[s] = enabled && map < 8;
        }
    } else if (reg >= 0xC0 && reg <= 0xDF) { // TEV colour/alpha combiner stages
        if ((reg & 1u) == 0) { // colour env
            const uint32_t s = (reg - 0xC0) / 2;
            if (s < 16) {
                g_gx.tevColorIn[s][3] = static_cast<uint8_t>(payload & 0xFu);
                g_gx.tevColorIn[s][2] = static_cast<uint8_t>((payload >> 4) & 0xFu);
                g_gx.tevColorIn[s][1] = static_cast<uint8_t>((payload >> 8) & 0xFu);
                g_gx.tevColorIn[s][0] = static_cast<uint8_t>((payload >> 12) & 0xFu);
                if (((payload >> 16) & 3u) == 3u) {
                    g_gx.tevColorOp[s] = 0; g_gx.tevColorBias[s] = 0; g_gx.tevColorScale[s] = 0;
                } else {
                    g_gx.tevColorBias[s] = static_cast<uint8_t>((payload >> 16) & 3u);
                    g_gx.tevColorOp[s] = static_cast<uint8_t>((payload >> 18) & 1u);
                    g_gx.tevColorScale[s] = static_cast<uint8_t>((payload >> 20) & 3u);
                }
                g_gx.tevColorClamp[s] = ((payload >> 19) & 1u) != 0;
                g_gx.tevColorOut[s] = static_cast<uint8_t>((payload >> 22) & 3u);
            }
        } else { // alpha env (+ swap mode)
            const uint32_t s = (reg - 0xC1) / 2;
            if (s < 16) {
                g_gx.tevSwapRas[s] = static_cast<uint8_t>(payload & 3u);
                g_gx.tevSwapTex[s] = static_cast<uint8_t>((payload >> 2) & 3u);
                g_gx.tevAlphaIn[s][3] = static_cast<uint8_t>((payload >> 4) & 7u);
                g_gx.tevAlphaIn[s][2] = static_cast<uint8_t>((payload >> 7) & 7u);
                g_gx.tevAlphaIn[s][1] = static_cast<uint8_t>((payload >> 10) & 7u);
                g_gx.tevAlphaIn[s][0] = static_cast<uint8_t>((payload >> 13) & 7u);
                if (((payload >> 16) & 3u) == 3u) {
                    g_gx.tevAlphaOp[s] = 0; g_gx.tevAlphaBias[s] = 0; g_gx.tevAlphaScale[s] = 0;
                } else {
                    g_gx.tevAlphaBias[s] = static_cast<uint8_t>((payload >> 16) & 3u);
                    g_gx.tevAlphaOp[s] = static_cast<uint8_t>((payload >> 18) & 1u);
                    g_gx.tevAlphaScale[s] = static_cast<uint8_t>((payload >> 20) & 3u);
                }
                g_gx.tevAlphaClamp[s] = ((payload >> 19) & 1u) != 0;
                g_gx.tevAlphaOut[s] = static_cast<uint8_t>((payload >> 22) & 3u);
            }
        }
    } else if (reg >= 0xF6 && reg <= 0xFD) { // TEV K select + swap table
        const uint32_t kselIdx = reg - 0xF6;
        const uint32_t swapIdx = kselIdx / 2;
        if (swapIdx < 4) {
            if (kselIdx & 1u) {
                g_gx.tevSwapTable[swapIdx][2] = static_cast<uint8_t>(payload & 3u);
                g_gx.tevSwapTable[swapIdx][3] = static_cast<uint8_t>((payload >> 2) & 3u);
            } else {
                g_gx.tevSwapTable[swapIdx][0] = static_cast<uint8_t>(payload & 3u);
                g_gx.tevSwapTable[swapIdx][1] = static_cast<uint8_t>((payload >> 2) & 3u);
            }
        }
        const uint32_t s0 = kselIdx * 2;
        const uint32_t s1 = kselIdx * 2 + 1;
        if (s0 < 16) {
            g_gx.tevKColorSel[s0] = static_cast<uint8_t>((payload >> 4) & 0x1Fu);
            g_gx.tevKAlphaSel[s0] = static_cast<uint8_t>((payload >> 9) & 0x1Fu);
        }
        if (s1 < 16) {
            g_gx.tevKColorSel[s1] = static_cast<uint8_t>((payload >> 14) & 0x1Fu);
            g_gx.tevKAlphaSel[s1] = static_cast<uint8_t>((payload >> 19) & 0x1Fu);
        }
    } else if (reg == 0x40) { // Z_MODE
        g_gx.depthCompare = (payload & 1u) != 0;
        g_gx.depthFunc = (payload >> 1) & 7u;
        g_gx.depthUpdate = ((payload >> 4) & 1u) != 0;
    } else if (reg == 0x41) { // CMODE0
        bool blendEn = (payload & 1u) != 0;
        bool subEn = ((payload >> 11) & 1u) != 0;
        g_gx.blendMode = subEn ? 3u : (blendEn ? 1u : 0u);
        g_gx.blendDst = (payload >> 5) & 7u;
        g_gx.blendSrc = (payload >> 8) & 7u;
    } else if (reg == 0x4F) { // CLEAR_AR
        g_gx.clearColor[0] = static_cast<uint8_t>(payload & 0xFFu);
        g_gx.clearColor[3] = static_cast<uint8_t>((payload >> 8) & 0xFFu);
    } else if (reg == 0x50) { // CLEAR_GB
        g_gx.clearColor[2] = static_cast<uint8_t>(payload & 0xFFu);
        g_gx.clearColor[1] = static_cast<uint8_t>((payload >> 8) & 0xFFu);
    } else if ((reg >= 0x80 && reg <= 0x9F) || (reg >= 0xA0 && reg <= 0xBF)) {
        uint32_t idx = reg - 0x80;
        uint32_t kind = (idx & 0x1Fu) / 4u;
        uint32_t mapId = (idx & 3u) + ((idx >= 0x20u) ? 4u : 0u);
        if (mapId < 8) {
            TexSlot& slot = g_gx.texSlots[mapId];
            if (kind == 0) { // Mode0
                slot.wrapS = static_cast<uint8_t>(payload & 3u);
                slot.wrapT = static_cast<uint8_t>((payload >> 2) & 3u);
            } else if (kind == 2) { // Image0
                slot.width = static_cast<uint16_t>((payload & 0x3FFu) + 1u);
                slot.height = static_cast<uint16_t>(((payload >> 10) & 0x3FFu) + 1u);
                slot.format = static_cast<uint8_t>((payload >> 20) & 0xFu);
            } else if (kind == 5) { // Image3
                slot.dataAddr = (payload & 0x00FFFFFFu) << 5;
                slot.valid = (slot.dataAddr != 0 && slot.width > 0 && slot.height > 0);
            }
        }
    } else if (reg >= 0xE0 && reg <= 0xE7) {
        uint32_t regId = (reg - 0xE0) / 2;
        bool isK = ((payload >> 23) & 1u) != 0;
        uint8_t (&dst)[4][4] = isK ? g_gx.tevKColor : g_gx.tevColor;
        if ((reg & 1u) == 0) { // RA
            dst[regId][0] = static_cast<uint8_t>(payload & 0xFFu);
            dst[regId][3] = static_cast<uint8_t>((payload >> 12) & 0xFFu);
        } else { // BG
            dst[regId][2] = static_cast<uint8_t>(payload & 0xFFu);
            dst[regId][1] = static_cast<uint8_t>((payload >> 12) & 0xFFu);
        }
    }
}

static void HandleLoadCpReg(uint8_t reg, uint32_t val) {
    auto bits = [](uint32_t v, uint32_t size, uint32_t shift) -> uint32_t {
        return (v >> shift) & ((1u << size) - 1u);
    };
    if (reg == 0x30) {
        g_gx.currentPnMtx = ( bits(val, 6, 0) / 3u) % 10u;
    } else if (reg == 0x50) { // VCD_LO
        g_gx.vtxDesc[0]  = bits(val, 1, 0);
        for (uint32_t i = 0; i < 8; ++i) g_gx.vtxDesc[1 + i] = bits(val, 1, 1 + i);
        g_gx.vtxDesc[9]  = bits(val, 2, 9);
        g_gx.vtxDesc[10] = bits(val, 2, 11);
        g_gx.vtxDesc[11] = bits(val, 2, 13);
        g_gx.vtxDesc[12] = bits(val, 2, 15);
    } else if (reg == 0x60) { // VCD_HI
        for (uint32_t i = 0; i < 8; ++i) g_gx.vtxDesc[13 + i] = bits(val, 2, i * 2);
    } else if (reg >= 0x70 && reg <= 0x77) { // VAT_A
        auto* vf = g_gx.vtxFmt[reg - 0x70];
        vf[9].cnt   = bits(val, 1, 0);
        vf[9].type  = bits(val, 3, 1);
        vf[9].frac  = static_cast<uint8_t>(bits(val, 5, 4));
        uint32_t nrmCnt = bits(val, 1, 9);
        uint32_t nrmNbt3 = bits(val, 1, 31);
        vf[10].cnt  = nrmNbt3 ? 2u : (nrmCnt ? 1u : 0u);
        vf[10].type = bits(val, 3, 10);
        vf[10].frac = (vf[10].type <= 1) ? 6 : ((vf[10].type <= 3) ? 14 : 0);
        vf[11].cnt  = bits(val, 1, 13);
        vf[11].type = bits(val, 3, 14);
        vf[11].frac = 0;
        vf[12].cnt  = bits(val, 1, 17);
        vf[12].type = bits(val, 3, 18);
        vf[12].frac = 0;
        vf[13].cnt  = bits(val, 1, 21);
        vf[13].type = bits(val, 3, 22);
        vf[13].frac = static_cast<uint8_t>(bits(val, 5, 25));
    } else if (reg >= 0x80 && reg <= 0x87) { // VAT_B
        auto* vf = g_gx.vtxFmt[reg - 0x80];
        vf[14].cnt  = bits(val, 1, 0);
        vf[14].type = bits(val, 3, 1);
        vf[14].frac = static_cast<uint8_t>(bits(val, 5, 4));
        vf[15].cnt  = bits(val, 1, 9);
        vf[15].type = bits(val, 3, 10);
        vf[15].frac = static_cast<uint8_t>(bits(val, 5, 13));
        vf[16].cnt  = bits(val, 1, 18);
        vf[16].type = bits(val, 3, 19);
        vf[16].frac = static_cast<uint8_t>(bits(val, 5, 22));
        vf[17].cnt  = bits(val, 1, 27);
        vf[17].type = bits(val, 3, 28);
    } else if (reg >= 0x90 && reg <= 0x97) { // VAT_C
        auto* vf = g_gx.vtxFmt[reg - 0x90];
        vf[17].frac = static_cast<uint8_t>(bits(val, 5, 0));
        vf[18].cnt  = bits(val, 1, 5);
        vf[18].type = bits(val, 3, 6);
        vf[18].frac = static_cast<uint8_t>(bits(val, 5, 9));
        vf[19].cnt  = bits(val, 1, 14);
        vf[19].type = bits(val, 3, 15);
        vf[19].frac = static_cast<uint8_t>(bits(val, 5, 18));
        vf[20].cnt  = bits(val, 1, 23);
        vf[20].type = bits(val, 3, 24);
        vf[20].frac = static_cast<uint8_t>(bits(val, 5, 27));
    } else if (reg >= 0xA0 && reg <= 0xAF) { // ARRAY_BASE
        uint32_t attr = 9 + (reg - 0xA0);
        if (attr < 25) g_gx.vtxArray[attr].base = val;
    } else if (reg >= 0xB0 && reg <= 0xBF) { // ARRAY_STRIDE
        uint32_t attr = 9 + (reg - 0xB0);
        if (attr < 25) g_gx.vtxArray[attr].stride = static_cast<uint8_t>(val & 0xFFu);
    }
}

static void HandleLoadXfReg(uint16_t baseAddr, uint16_t count, const uint8_t* data) {
    if (g_gxFrame == kGxTraceFrame && baseAddr < 0x0078u) {
        AppLog("[GX LoadXf Matrix] baseAddr=%04X count=%u\n", baseAddr, count);
    }
    bool projUpdated = false;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t addr = baseAddr + i;
        const uint8_t* wordPtr = data + i * 4;
        if (addr < 0x0078u) { // Position matrices (10 x 12 floats)
            uint32_t mtxIdx = addr / 12u;
            uint32_t comp = addr % 12u;
            if (mtxIdx < 10) {
                g_gx.pnMtx[mtxIdx][comp] = ReadBEFloat(wordPtr);
                if (comp == 11) {
                    SanitizeModelViewMatrix(g_gx.pnMtx[mtxIdx]);
                    ++g_gx.xfMtxWrites;
                    const float* m = g_gx.pnMtx[mtxIdx];
                    if (m[0] != 1.0f || m[1] != 0.0f || m[2] != 0.0f ||
                        m[4] != 0.0f || m[5] != 1.0f || m[6] != 0.0f || m[3] != 0.0f || m[7] != 0.0f) {
                        ++g_gx.mtxNonIdentity;
                    }
                }
            }
        } else if (addr == 0x100Au || addr == 0x100Bu) { // AmbColor
            uint32_t c = ReadBE32(wordPtr);
            uint32_t ch = addr - 0x100Au;
            g_gx.chanAmbColor[ch][0] = (c >> 24) & 0xFFu;
            g_gx.chanAmbColor[ch][1] = (c >> 16) & 0xFFu;
            g_gx.chanAmbColor[ch][2] = (c >> 8)  & 0xFFu;
            g_gx.chanAmbColor[ch][3] = c         & 0xFFu;
        } else if (addr == 0x100Cu || addr == 0x100Du) { // MatColor
            uint32_t c = ReadBE32(wordPtr);
            uint32_t ch = addr - 0x100Cu;
            g_gx.chanMatColor[ch][0] = (c >> 24) & 0xFFu;
            g_gx.chanMatColor[ch][1] = (c >> 16) & 0xFFu;
            g_gx.chanMatColor[ch][2] = (c >> 8)  & 0xFFu;
            g_gx.chanMatColor[ch][3] = c         & 0xFFu;
        } else if (addr == 0x100Eu) { // Channel 0 control
            const uint32_t ctrl = ReadBE32(wordPtr);
            g_gx.chanCtrl0 = ctrl;
            const bool enable = ((ctrl >> 1) & 1u) != 0;
            const uint32_t lightMask = (ctrl >> 2) & 0xFu;
            g_gx.lightEnabled = enable && lightMask != 0;
            static uint32_t s_lastCtrl = 0xFFFFFFFFu;
            static uint32_t s_lastLightLogFrame = 0;
            if (ctrl != s_lastCtrl && g_gxFrame >= s_lastLightLogFrame + 60) {
                s_lastCtrl = ctrl;
                s_lastLightLogFrame = g_gxFrame;
                AppLog("[GX Light] chanCtrl0=%08X enable=%u mask=%u lightEnabled=%u dir=(%g,%g,%g) col=%u,%u,%u frame=%u\n",
                    ctrl, enable ? 1u : 0u, lightMask, g_gx.lightEnabled ? 1u : 0u,
                    g_gx.light0Dir[0], g_gx.light0Dir[1], g_gx.light0Dir[2],
                    g_gx.light0Color[0], g_gx.light0Color[1], g_gx.light0Color[2],
                    g_gxFrame);
            }
        } else if (addr == 0x0600u) { // Light0 colour
            const uint32_t c = ReadBE32(wordPtr);
            g_gx.light0Color[0] = static_cast<uint8_t>((c >> 24) & 0xFFu);
            g_gx.light0Color[1] = static_cast<uint8_t>((c >> 16) & 0xFFu);
            g_gx.light0Color[2] = static_cast<uint8_t>((c >> 8) & 0xFFu);
            g_gx.light0Color[3] = static_cast<uint8_t>(c & 0xFFu);
        } else if (addr >= 0x0607u && addr <= 0x0609u) { // Light0 position/direction
            g_gx.light0Dir[addr - 0x0607u] = ReadBEFloat(wordPtr);
            g_gx.light0DirWritten = true;
        } else if (addr == 0x1018u) { // MatrixIndex0
            uint32_t val = ReadBE32(wordPtr);
            g_gx.currentPnMtx = ((val & 0x3Fu) / 3u) % 10u;
        } else if (addr >= 0x1020u && addr <= 0x1025u) {
            g_gx.xfProj[addr - 0x1020u] = ReadBEFloat(wordPtr);
            projUpdated = true;
        } else if (addr == 0x1026u) {
            g_gx.xfProjType = ReadBE32(wordPtr);
            projUpdated = true;
        }
    }
    if (projUpdated) {
        RebuildProjFromXf();
        TraceProjection("XF", baseAddr, TryGetCpuContext());
    }
}

static void HandleLoadIndx(uint8_t cmd, const uint8_t* d) {
    uint16_t arrayIdx = ReadBE16(d);
    uint16_t addrLen = ReadBE16(d + 2);
    uint32_t count = static_cast<uint32_t>((addrLen >> 12) & 0xFu) + 1u;
    uint32_t xfDest = addrLen & 0x0FFFu;
    uint32_t arrayAttr = 21 + ((cmd - 0x20u) / 8u);
    if (g_gxFrame == kGxTraceFrame) {
        AppLog("[GX LoadIndx] cmd=%02X arrayIdx=%u addrLen=%04X count=%u xfDest=%04X attr=%u base=%08X stride=%u\n",
            cmd, arrayIdx, addrLen, count, xfDest, arrayAttr, g_gx.vtxArray[arrayAttr].base, g_gx.vtxArray[arrayAttr].stride);
    }
    if (arrayAttr >= 25) return;
    uint32_t baseAddr = g_gx.vtxArray[arrayAttr].base;
    uint32_t stride = g_gx.vtxArray[arrayAttr].stride;
    if (baseAddr == 0 || stride == 0) return;
    uint32_t srcAddr = baseAddr + static_cast<uint32_t>(arrayIdx) * stride;
    const uint8_t* srcPtr = resolve_guest(srcAddr, count * 4);
    if (!srcPtr && srcAddr < 0x20000000u) {
        srcPtr = resolve_guest(srcAddr | 0x80000000u, count * 4);
    }
    if (srcPtr) {
        HandleLoadXfReg(static_cast<uint16_t>(xfDest), static_cast<uint16_t>(count), srcPtr);
    }
}


static void ExecutePrimitive(uint8_t primCmd, uint16_t vtxCount, const uint8_t*& p) {
    if (!RasterThisFrame()) { ++g_gx.drawCallsSkipped; return; }
    uint32_t vatIdx = primCmd & 7u;
    uint8_t primType = primCmd & 0xF8u;
    if (vtxCount == 0) return;
    ++g_gx.drawCallsThisFrame;

    static std::array<GxVertex, 512> s_verts;
    static std::array<uint32_t, 512> s_mtxs;
    const uint16_t decodeCount = (vtxCount <= 512) ? vtxCount : 512;
    for (uint16_t i = 0; i < vtxCount; ++i) {
        if (i < decodeCount) {
            s_verts[i] = DecodeVertex(p, vatIdx, s_mtxs[i]);
        } else {
            uint32_t dummyMtx;
            DecodeVertex(p, vatIdx, dummyMtx);
        }
    }

    const auto* verts = s_verts.data();
    const auto* mtxs = s_mtxs.data();

    if (primType == 0x80 || primType == 0x88) { // GX_QUADS
        for (uint16_t i = 0; i + 3 < decodeCount; i += 4) {
            RasterizeTriangle(verts[i], verts[i + 1], verts[i + 2], mtxs[i]);
            RasterizeTriangle(verts[i], verts[i + 2], verts[i + 3], mtxs[i]);
        }
    } else if (primType == 0x90) { // GX_TRIANGLES
        for (uint16_t i = 0; i + 2 < decodeCount; i += 3) {
            RasterizeTriangle(verts[i], verts[i + 1], verts[i + 2], mtxs[i]);
        }
    } else if (primType == 0x98) { // GX_TRIANGLESTRIP
        for (uint16_t i = 0; i + 2 < decodeCount; ++i) {
            if ((i & 1u) == 0) {
                RasterizeTriangle(verts[i], verts[i + 1], verts[i + 2], mtxs[i]);
            } else {
                RasterizeTriangle(verts[i + 1], verts[i], verts[i + 2], mtxs[i]);
            }
        }
    } else if (primType == 0xA0) { // GX_TRIANGLEFAN
        for (uint16_t i = 1; i + 1 < decodeCount; ++i) {
            RasterizeTriangle(verts[0], verts[i], verts[i + 1], mtxs[0]);
        }
    }
}

static void ParseDisplayList(uint32_t listAddr, uint32_t byteCount, int depth = 0);

static size_t ConsumeOneGxCommand(const uint8_t* buf, size_t avail, int dlDepth) {
    if (avail == 0) return 0;
    uint8_t cmd = buf[0];
    uint8_t opcode = cmd & 0xF8u;

    if (cmd == 0x00) { // NOP
        return 1;
    }
    if (opcode == 0x08) { // GX_LOAD_CP_REG (6 bytes: opcode, addr, u32 val)
        if (avail < 6) return 0;
        HandleLoadCpReg(buf[1], ReadBE32(buf + 2));
        return 6;
    }
    if (opcode == 0x10) { // GX_LOAD_XF_REG (5 + count*4 bytes)
        if (avail < 5) return 0;
        uint16_t count = ReadBE16(buf + 1) + 1u;
        uint16_t addr = ReadBE16(buf + 3);
        size_t total = 5 + static_cast<size_t>(count) * 4;
        if (avail < total) return 0;
        HandleLoadXfReg(addr, count, buf + 5);
        return total;
    }
    if (opcode >= 0x20 && opcode <= 0x38) { // GX_LOAD_INDX_* (5 bytes)
        if (avail < 5) return 0;
        HandleLoadIndx(cmd, buf + 1);
        return 5;
    }
    if (opcode == 0x40) { // GX_CMD_CALL_DL (9 bytes)
        if (avail < 9) return 0;
        uint32_t dlAddr = ReadBE32(buf + 1);
        uint32_t dlSize = ReadBE32(buf + 5);
        if (dlDepth < 4) {
            ParseDisplayList(dlAddr, dlSize, dlDepth + 1);
        }
        return 9;
    }
    if (opcode == 0x48) { // GX_CMD_INVAL_VTX (1 byte)
        return 1;
    }
    if (opcode == 0x60) { // GX_LOAD_BP_REG (5 bytes: opcode + 4 bytes val)
        if (avail < 5) return 0;
        HandleLoadBpReg(ReadBE32(buf + 1));
        return 5;
    }
    if ((cmd & 0x80u) != 0) { // Primitive draw command (0x80..0xBF)
        if (avail < 3) return 0;
        uint16_t vtxCount = ReadBE16(buf + 1);
        uint32_t vtxSize = CalcVtxByteSize(cmd & 7u);
        size_t total = 3 + static_cast<size_t>(vtxCount) * vtxSize;
        if (vtxSize == 0) {
            return 3;
        }
        if (avail < total) return 0;
        const uint8_t* p = buf + 3;
        if (g_gxFrame == kGxTraceFrame) {
            const uint32_t order = ++g_traceDrawOrder;
            const FrameSamples before = SampleFramebuffer();
            AppLog("[GX Trace %u] FIFO cmd=%02X vertices=%u dlDepth=%d projection=%u\n",
                order, cmd, vtxCount, dlDepth, g_gx.projType);
            ExecutePrimitive(cmd, vtxCount, p);
            LogDrawSamples(order, before);
        } else {
            ExecutePrimitive(cmd, vtxCount, p);
        }
        return total;
    }
    // Unknown byte in stream: skip 1 byte
    return 1;
}

static void ParseDisplayList(uint32_t listAddr, uint32_t byteCount, int depth) {
    const bool trace = g_gxFrame == kGxTraceFrame;
    const CpuContext* cpu = trace ? TryGetCpuContext() : nullptr;
    if (listAddr == 0 || byteCount == 0 || byteCount > 0x100000u) {
        if (trace) AppLog("[GX List] rejected addr=%08X bytes=%u pc=%08X lr=%08X\n",
            listAddr, byteCount, cpu ? cpu->pc : 0, cpu ? cpu->lr : 0);
        return;
    }
    const uint8_t* ptr = resolve_guest(listAddr, byteCount);
    if (!ptr && listAddr < 0x20000000u) {
        ptr = resolve_guest(listAddr | 0x80000000u, byteCount);
    }
    if (!ptr) {
        if (trace) AppLog("[GX List] unmapped addr=%08X bytes=%u pc=%08X lr=%08X\n",
            listAddr, byteCount, cpu ? cpu->pc : 0, cpu ? cpu->lr : 0);
        return;
    }
    ++g_gx.dlCallsThisFrame;
    const uint32_t drawsBefore = g_gx.drawCallsThisFrame;

    size_t offset = 0;
    while (offset < byteCount) {
        size_t consumed = ConsumeOneGxCommand(ptr + offset, byteCount - offset, depth);
        if (consumed == 0) break;
        offset += consumed;
    }
    if (trace) {
        AppLog("[GX List] addr=%08X bytes=%u depth=%d pc=%08X lr=%08X first=%02X consumed=%zu draws=%u\n",
            listAddr, byteCount, depth, cpu ? cpu->pc : 0, cpu ? cpu->lr : 0,
            ptr[0], offset, g_gx.drawCallsThisFrame - drawsBefore);
    }
}

static std::vector<uint8_t> g_fifoStream;

static void AppendFifoBytes(const uint8_t* data, size_t len) {
    EnsureInitialized();
    if (g_gx.inDisplayList) {
        for (size_t i = 0; i < len; ++i) {
            if (g_gx.dlWriteOffset < g_gx.dlMaxSize) {
                Memory::Write8(g_gx.dlBaseAddr + g_gx.dlWriteOffset, data[i]);
                ++g_gx.dlWriteOffset;
            }
        }
        return;
    }
    g_fifoStream.insert(g_fifoStream.end(), data, data + len);
    size_t offset = 0;
    while (offset < g_fifoStream.size()) {
        size_t consumed = ConsumeOneGxCommand(g_fifoStream.data() + offset, g_fifoStream.size() - offset, 0);
        if (consumed == 0) break;
        offset += consumed;
    }
    if (offset > 0) {
        g_fifoStream.erase(g_fifoStream.begin(), g_fifoStream.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    if (g_fifoStream.size() > 262144) {
        g_fifoStream.clear();
    }
}

#ifdef __3DS__
// ===========================================================================
// NDSP audio: play a retail BRSTM stream straight from the disc assets
// ===========================================================================
// Mario Kart Wii's BGM is either an RSEQ sequence inside revo_kart.brsar or a BRSTM stream in
// sound/strm/.  A BRSTM's payload is DSP-ADPCM, i.e. exactly what the 3DS DSP decodes in hardware,
// so no transcoding is needed: parse the header and hand the bytes to NDSP.
//
// The whole stream is loaded into FCRAM and played as one looping wave buffer.  That is simpler
// than a streaming ring and good enough for the ~0.5-1 MB menu tracks; the DSP loops it without
// help from us.  (MKWii's BRSTMs put their loop point mid-file, so a whole-buffer loop repeats the
// intro -- acceptable for now, and the correct fix is to start the buffer at the loop point.)
static bool g_audioReady = false;
static bool g_audioStarted = false;
static bool g_audioFailed = false;
static int  g_audioChannel = 0;
static std::vector<uint8_t> g_audioFile;      // whole BRSTM
static std::vector<uint8_t> g_audioPcm;       // interleaved / mono ADPCM payload
static ndspWaveBuf g_audioWave{};
static uint32_t g_audioRate = 32000;
static uint32_t g_audioSamples = 0;
static uint32_t g_audioBytes = 0;
static uint8_t  g_audioChannels = 2;

static uint32_t RdU32(const uint8_t* d, size_t o) {
    return (static_cast<uint32_t>(d[o]) << 24) | (static_cast<uint32_t>(d[o + 1]) << 16) |
           (static_cast<uint32_t>(d[o + 2]) << 8) | d[o + 3];
}
static uint16_t RdU16(const uint8_t* d, size_t o) {
    return static_cast<uint16_t>((d[o] << 8) | d[o + 1]);
}
// Util::DataRef: {u8 refType, u8 dataType, u16 pad, u32 value}; refType 1 = offset from the section base.
static bool RdDataRef(const uint8_t* d, size_t size, size_t o, size_t base, size_t* out) {
    if (o + 8 > size) return false;
    if (d[o] != 1 || RdU32(d, o + 4) == 0) return false;
    const size_t at = base + RdU32(d, o + 4);
    if (at >= size) return false;
    *out = at;
    return true;
}

static bool AudioParseBrstm(const uint8_t* d, size_t size) {
    if (size < 0x40 || std::memcmp(d, "RSTM", 4) != 0) return false;
    const size_t head = RdU32(d, 0x10);
    if (head + 0x20 > size) return false;
    const size_t base = head + 8;
    size_t info = 0;
    if (!RdDataRef(d, size, head + 8, base, &info)) return false;

    const uint8_t format = d[info];
    const uint8_t channels = d[info + 2];
    g_audioChannels = channels;
    g_audioRate = (static_cast<uint32_t>(d[info + 3]) << 16) | RdU16(d, info + 4);
    const uint32_t loopEnd = RdU32(d, info + 12);
    const uint32_t blocks = RdU32(d, info + 0x14);
    const uint32_t blockSize = RdU32(d, info + 0x18);
    const uint32_t blockSamples = RdU32(d, info + 0x1C);
    const uint32_t lastPadded = RdU32(d, info + 0x28);
    if (format != 2) { // 0/1 = PCM16/PCM8, not what the DSP wants
        AppLog("[Audio] unsupported BRSTM codec %u\n", format);
        return false;
    }
    if (channels != 1 && channels != 2) return false;
    if (blockSize == 0 || blocks == 0) return false;

    // DATA section: 8-byte header, then the per-channel offsets; the first channel's audio starts
    // at data_block+8+its offset.  Channel 1 (if present) follows channel 0's padded block run.
    const size_t dataBlock = RdU32(d, 0x20);
    if (dataBlock + 0x10 > size) return false;
    size_t audio = 0;
    if (!RdDataRef(d, size, dataBlock + 8, dataBlock + 8, &audio)) return false;
    const size_t chanBytes = static_cast<size_t>(blocks) * blockSize + lastPadded;
    const size_t need = (channels == 2 ? chanBytes * 2 : chanBytes);
    if (audio + need > size) {
        AppLog("[Audio] BRSTM data truncated (%zu+%zu > %zu)\n", audio, need, size);
        return false;
    }

    g_audioSamples = loopEnd;
    if (channels == 1) {
        g_audioPcm.assign(d + audio, d + audio + chanBytes);
        g_audioBytes = static_cast<uint32_t>(chanBytes);
    } else {
        // STEREO_DSPADPCM wants the two channels' 8-byte frames adjacent.
        const size_t frames = chanBytes / 8;
        g_audioPcm.resize(frames * 16);
        for (size_t f = 0; f < frames; ++f) {
            std::memcpy(&g_audioPcm[f * 16], d + audio + f * 8, 8);
            std::memcpy(&g_audioPcm[f * 16 + 8], d + audio + chanBytes + f * 8, 8);
        }
        g_audioBytes = static_cast<uint32_t>(frames * 16);
    }
    return true;
}

static bool AudioStart(const char* path) {
    if (g_audioStarted) return true;
    g_audioStarted = true;

    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        AppLog("[Audio] %s missing\n", path);
        g_audioFailed = true;
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) { std::fclose(fp); g_audioFailed = true; return false; }
    g_audioFile.resize(static_cast<size_t>(size));
    const size_t got = std::fread(g_audioFile.data(), 1, g_audioFile.size(), fp);
    std::fclose(fp);
    if (got != g_audioFile.size()) { g_audioFailed = true; return false; }

    if (!AudioParseBrstm(g_audioFile.data(), g_audioFile.size())) {
        AppLog("[Audio] %s is not a playable BRSTM\n", path);
        g_audioFailed = true;
        return false;
    }
    g_audioFile.clear();
    g_audioFile.shrink_to_fit();

    if (!g_audioReady) {
        if (R_FAILED(ndspInit())) {
            AppLog("[Audio] ndspInit failed (no dspfirm.cdc?)\n");
            g_audioFailed = true;
            return false;
        }
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspSetMasterVol(1.0f);
        g_audioReady = true;
    }
    const int ch = g_audioChannel;
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, static_cast<float>(g_audioRate));
    // libctru spells the DSP-ADPCM formats NDSP_FORMAT_ADPCM (stereo, interleaved 8-byte frames)
    // and NDSP_FORMAT_MONO_ADPCM.
    ndspChnSetFormat(ch, g_audioChannels == 2 ? NDSP_FORMAT_ADPCM : NDSP_FORMAT_MONO_ADPCM);
    static const float kMix[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                   1.0f, 1.0f, 1.0f, 0.0f};
    ndspChnSetMix(ch, const_cast<float*>(kMix));

    std::memset(&g_audioWave, 0, sizeof(g_audioWave));
    g_audioWave.data_vaddr = g_audioPcm.data();
    g_audioWave.nsamples = g_audioSamples;
    g_audioWave.looping = true;
    DSP_FlushDataCache(g_audioPcm.data(), g_audioBytes);
    ndspChnWaveBufAdd(ch, &g_audioWave);
    AppLog("[Audio] playing %s rate=%u samples=%u bytes=%u\n", path, g_audioRate,
           g_audioSamples, g_audioBytes);
    return true;
}

// ===========================================================================
// PICA200 (citro3d) 2D path
// ===========================================================================
// The software rasteriser samples every pixel of every layout quad in C++.  The menu issues ~24
// quads covering ~215k pixels per frame, which on a 268 MHz ARM11 costs ~2.3 s per frame, while the
// guest CPU itself only needs ~35 ms.  The 2D layout pass is exactly what the PICA200 is for, so
// these quads are handed to citro3d instead: the guest still produces the same draw calls and the
// same geometry, we just stop touching pixels.  The 3D pass stays on the software rasteriser for
// now (the menus never draw 3D; the title's kart models are a later step).
//
// MKW_NO_GPU2D=1 keeps the old software path; MKW_GPU_YSIGN / MKW_GPU_VFLIP let the 2D orientation
// be A/B tested on hardware without a rebuild (see the mirrored-layout note on Project2DPoint).
static bool  g_gpuReady = false;
static bool  g_gpuTried = false;
static C3D_RenderTarget* g_gpuTop = nullptr;
static C3D_Mtx g_gpuProj;
static C3D_Mtx g_gpuModelView;
static float g_gpuYSpace = -1.0f;   // canvas y -> screen y sign (the software path uses -1)
static bool  g_gpuVFlip = false;    // flip V on textured quads (A/B for the layout mirror)
static bool  g_gpuScissor = false;  // per-quad scissor (MKW_GPU_SCISSOR=1)
static uint32_t g_gpuClearOverride = 0;   // MKW_GPU_CLEAR=RRGGBB, 0 = use the guest clear
static uint32_t g_gpuDebugQuads = 0;      // dumps the first few quads' geometry
static int      g_gpuTestMode = 0;        // 1 = reference pattern, 2 = layout with white texture
static int      g_gpuProjUnif = 0;        // MKW_GPU_UNIF=n picks the projection uniform slot
static bool     g_gpuPnMtx = true;        // apply the pane matrix (GXLoadPosMtxImm) to layout quads

struct GpuQuadVtx { float x, y, u, v, r, g, b, a; };
struct GpuDrawVertex { float position[3], normal[3], texcoord[4]; uint8_t color[4]; };
static_assert(sizeof(GpuDrawVertex) == 44, "native PICA vertex layout");
struct GpuQuad {
    GpuQuadVtx v[4] = {};
    C3D_Tex* tex = nullptr;
    float uScale = 1.0f;
    float vScale = 1.0f;
    int potW = 0;
    int potH = 0;
    bool blend = true;
    GPU_TEXTURE_WRAP_PARAM wrapS = GPU_CLAMP_TO_EDGE;
    GPU_TEXTURE_WRAP_PARAM wrapT = GPU_CLAMP_TO_EDGE;
    int clip[4] = {0, 0, kScreenW, kScreenH};
};
struct GpuTexEntry {
    C3D_Tex tex{};
    bool valid = false;
    size_t bytes = 0;
    uint64_t lastUse = 0;
    int potW = 0;
    int potH = 0;
    float uScale = 1.0f;  // logical width  / padded width
    float vScale = 1.0f;
};

static std::vector<GpuQuad> g_gpuQuads;
static std::map<uint64_t, GpuTexEntry> g_gpuTextures;
static size_t   g_gpuTexCap = 3ull * 1024 * 1024;
static uint64_t g_gpuTexTick = 0;
static uint64_t g_gpuEvictFloor = 0;     // textures used at/after this tick are in flight
static uint64_t g_gpuFrameStartTick = 0; // first tick of the frame being recorded
static C3D_Tex   g_gpuWhite{};
static bool      g_gpuWhiteValid = false;
static DVLB_s*   g_gpuDvlb = nullptr;
static shaderProgram_s g_gpuProgram{};
static bool      g_gpuProgramReady = false;
static C3D_Tex   g_gpuBase{};
static bool      g_gpuBaseValid = false;
static GpuDrawVertex* g_gpuDrawVertices = nullptr;
static size_t g_gpuDrawVertexCapacity = 0;

// PICA200 textures are not row-major: texels are grouped in 8x8 tiles (tiles in row-major
// order), and inside a tile they follow a Morton/Z order.  C3D_TexUpload (C3D_TexLoadImage in
// citro3d 1.7.1) is a plain memcpy into the texture's LINEAR buffer -- it neither swizzles nor
// flushes the data cache -- so the caller must write the tiled layout and C3D_TexFlush it.
// (Same as MKW-3DS, whose tex3ds/.t3x assets are pre-tiled and which flushes after every load.)
// Memory row 0 is sampled at t=0; we store the image top row there so the guest's GX texcoords
// (v=0 = top) are used unchanged.
static inline uint32_t PicaMorton8(uint32_t x, uint32_t y) {
    return (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) | ((y & 2u) << 2) |
           ((x & 4u) << 2) | ((y & 4u) << 3);
}
static inline size_t PicaTiledIndex(int x, int y, int potW) {
    const size_t tile = static_cast<size_t>(y >> 3) * static_cast<size_t>(potW >> 3) +
                        static_cast<size_t>(x >> 3);
    return (tile << 6) + PicaMorton8(static_cast<uint32_t>(x & 7), static_cast<uint32_t>(y & 7));
}

static void Gpu2dReleaseEntry(std::map<uint64_t, GpuTexEntry>::iterator it) {
    if (it->second.valid) {
        C3D_TexDelete(&it->second.tex);
        it->second.valid = false;
    }
    if (g_gpuTexBytes >= it->second.bytes) g_gpuTexBytes -= it->second.bytes;
    else g_gpuTexBytes = 0;
    g_gpuTextures.erase(it);
}

static bool Gpu2dInit() {
    if (g_gpuReady) return true;
    if (g_gpuTried) return g_gpuReady;
    g_gpuTried = true;

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        AppLog("[Gpu2d] C3D_Init failed\n");
        return false;
    }
    // The 3DS framebuffer is stored rotated: citro3d render targets for the top screen are
    // created as (240, 400) and the projection is built with Mtx_OrthoTilt so that layout
    // coordinates stay (x right, y down) in a 400x240 space.  Creating a (400, 240) target
    // and using plain Mtx_Ortho puts every quad outside the target, which renders black.
    // Bind our own vertex shader: citro3d's built-in one multiplies by matrices from float
    // uniforms that start zeroed, which silently discards every vertex.  This one is a
    // pass-through (positions are already clip space) that also forwards colour and texcoords.
    g_gpuDvlb = DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<uint8_t*>(kGpu2dShader)),
                               sizeof(kGpu2dShader));
    if (!g_gpuDvlb) {
        AppLog("[Gpu2d] DVLB_ParseFile failed\n");
        return false;
    }
    shaderProgramInit(&g_gpuProgram);
    if (R_FAILED(shaderProgramSetVsh(&g_gpuProgram, &g_gpuDvlb->DVLE[0]))) {
        AppLog("[Gpu2d] shaderProgramSetVsh failed\n");
        return false;
    }
    C3D_BindProgram(&g_gpuProgram);
    g_gpuProgramReady = true;

    g_gpuTop = C3D_RenderTargetCreate(kScreenH, kScreenW, GPU_RB_RGBA8,
                                      GPU_RB_DEPTH24_STENCIL8);
    if (!g_gpuTop) {
        AppLog("[Gpu2d] C3D_RenderTargetCreate failed\n");
        return false;
    }
    C3D_RenderTargetSetOutput(g_gpuTop, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);

    // A 1x1 white texel stands in for untextured quads so every draw uses one pipeline.
    static const uint16_t kWhite = 0xFFFFu;
    if (C3D_TexInit(&g_gpuWhite, 8, 8, GPU_RGBA5551)) {
        uint16_t* dst = static_cast<uint16_t*>(g_gpuWhite.data);
        for (int i = 0; i < 64; ++i) dst[i] = kWhite;
        C3D_TexFlush(&g_gpuWhite);
        C3D_TexSetFilter(&g_gpuWhite, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&g_gpuWhite, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        g_gpuWhiteValid = true;
    }

    Mtx_OrthoTilt(&g_gpuProj, 0.0f, static_cast<float>(kScreenW),
                  static_cast<float>(kScreenH), 0.0f, -1.0f, 1.0f, false);
    // citro3d's built-in vertex shader computes projectionMtx(uniform 0) * modelViewMtx(uniform 3).
    // Uniforms start zeroed, so an unset modelview collapses every vertex to the origin.
    Mtx_Identity(&g_gpuModelView);

    g_gpuReady = true;
    AppLog("[Gpu2d] citro3d ready %dx%d texBudget=%zuKB\n",
        kScreenW, kScreenH, g_gpuTexCap / 1024);
    return true;
}

// Reads sdmc:/mkwii/gpu.cfg, one "key=value" per line (blank lines and '#' comments ignored):
//   gpu=0              use the software rasteriser
//   debug_overrides=1  enable test/unif/ysign/vflip/pnmtx diagnostic keys
//   test=1             draw the two reference quads (requires debug_overrides=1)
//   unif=3             projection uniform slot (requires debug_overrides=1)
//   ysign=1            canvas y -> screen y sign (requires debug_overrides=1)
//   vflip=1            flip V on textured quads (requires debug_overrides=1)
//   pnmtx=0            ignore the pane matrix (requires debug_overrides=1)
//   scissor=1          per-quad scissor
//   debug=0            silence the first-quad dump
//   clear=FF00FF       forced clear colour
//   texmb=3            VRAM texture budget
static bool Gpu2dReadConfig() {
    FILE* fp = std::fopen("sdmc:/mkwii/gpu.cfg", "rb");
    if (!fp) fp = std::fopen("gpu.cfg", "rb");
    if (!fp) return false;
    char line[128];
    bool any = false;
    bool debugOverrides = false;
    bool hasDebugOverrideKey = false;
    int pendingTestMode = g_gpuTestMode;
    int pendingProjUnif = g_gpuProjUnif;
    float pendingYSpace = g_gpuYSpace;
    bool pendingVFlip = g_gpuVFlip;
    bool pendingPnMtx = g_gpuPnMtx;
    while (std::fgets(line, sizeof(line), fp)) {
        char* hash = std::strchr(line, '#');
        if (hash) *hash = 0;
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        const char* value = eq + 1;
        auto trim = [](char* t) {
            while (*t == ' ' || *t == '\t') ++t;
            char* e = t + std::strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) --e;
            *e = 0;
            return t;
        };
        char keyBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "%s", key);
        const char* k = trim(keyBuf);
        char valBuf[64];
        std::snprintf(valBuf, sizeof(valBuf), "%s", value);
        const char* v = trim(valBuf);
        const long n = std::atol(v);
        const bool on = n != 0;
        any = true;
        if (std::strcmp(k, "gpu") == 0) g_gpu2d = on;
        else if (std::strcmp(k, "debug_overrides") == 0) debugOverrides = on;
        else if (std::strcmp(k, "test") == 0) { pendingTestMode = static_cast<int>(n); hasDebugOverrideKey = true; }
        else if (std::strcmp(k, "unif") == 0) { pendingProjUnif = static_cast<int>(n) & 3; hasDebugOverrideKey = true; }
        else if (std::strcmp(k, "ysign") == 0) { pendingYSpace = (n == 0) ? 0.0f : static_cast<float>(n); hasDebugOverrideKey = true; }
        else if (std::strcmp(k, "vflip") == 0) { pendingVFlip = on; hasDebugOverrideKey = true; }
        else if (std::strcmp(k, "pnmtx") == 0) { pendingPnMtx = on; hasDebugOverrideKey = true; }
        else if (std::strcmp(k, "scissor") == 0) g_gpuScissor = on;
        else if (std::strcmp(k, "debug") == 0) g_gpuDebugQuads = on ? 4u : 0u;
        else if (std::strcmp(k, "clear") == 0)
            g_gpuClearOverride = (static_cast<uint32_t>(std::strtoul(v, nullptr, 16)) << 8) | 0xFFu;
        else if (std::strcmp(k, "audio") == 0) g_audioEnabled = on;
        else if (std::strcmp(k, "audiopath") == 0)
            std::snprintf(g_audioPathBuf, sizeof(g_audioPathBuf), "%s", v);
        else if (std::strcmp(k, "texmb") == 0 && n > 0)
            g_gpuTexCap = static_cast<size_t>(n) * 1024ull * 1024ull;
    }
    std::fclose(fp);
    if (debugOverrides) {
        g_gpuTestMode = pendingTestMode;
        g_gpuProjUnif = pendingProjUnif;
        g_gpuYSpace = pendingYSpace;
        g_gpuVFlip = pendingVFlip;
        g_gpuPnMtx = pendingPnMtx;
    } else if (hasDebugOverrideKey) {
        AppLog("[Gpu2d] ignoring stale gpu.cfg debug overrides (set debug_overrides=1 to enable)\n");
    }
    if (any) AppLog("[Gpu2d] config sdmc:/mkwii/gpu.cfg loaded: gpu=%d test=%d unif=%d ysign=%.1f "
                    "vflip=%d pnmtx=%d scissor=%d debug=%u clear=%08X texmb=%zu\n",
                    g_gpu2d ? 1 : 0, g_gpuTestMode, g_gpuProjUnif, g_gpuYSpace,
                    g_gpuVFlip ? 1 : 0, g_gpuPnMtx ? 1 : 0, g_gpuScissor ? 1 : 0, g_gpuDebugQuads,
                    g_gpuClearOverride, g_gpuTexCap / (1024 * 1024));
    return any;
}

static void Gpu2dConfigure() {
    g_gpu2d = std::getenv("MKW_NO_GPU2D") == nullptr;
    if (const char* ys = std::getenv("MKW_GPU_YSIGN")) g_gpuYSpace = std::stof(ys);
    g_gpuVFlip = std::getenv("MKW_GPU_VFLIP") != nullptr;
    g_gpuPnMtx = std::getenv("MKW_GPU_NOPNMTX") == nullptr;
    g_gpuScissor = std::getenv("MKW_GPU_SCISSOR") != nullptr;
    if (const char* clr = std::getenv("MKW_GPU_CLEAR")) {
        g_gpuClearOverride = static_cast<uint32_t>(std::strtoul(clr, nullptr, 16)) << 8 | 0xFFu;
    }
    g_gpuDebugQuads = std::getenv("MKW_GPU_NODEBUG") ? 0u : 4u;
    if (const char* t = std::getenv("MKW_GPU_TEST")) g_gpuTestMode = std::atoi(t);
    if (const char* u = std::getenv("MKW_GPU_UNIF")) g_gpuProjUnif = std::atoi(u) & 3;
#ifdef __3DS__
    Gpu2dReadConfig(); // the config file wins: it is the one that is easy to edit on device
#endif
    if (const char* mb = std::getenv("MKW_GPU_TEX_MB")) {
        const long v = std::atol(mb);
        if (v > 0) g_gpuTexCap = static_cast<size_t>(v) * 1024ull * 1024ull;
    }
}

// Same transform the software rasteriser applies for ortho draws (modelview is forced to identity
// there), so the GPU layout matches the pixels the software path produced.
static bool Project2DPoint(const float pos[3], float* outX, float* outY) {
    const auto& P = g_gx.proj;
    const float ex = pos[0], ey = pos[1], ez = pos[2];
    float cx = P[0][0] * ex + P[0][1] * ey + P[0][2] * ez + P[0][3];
    float cy = P[1][0] * ex + P[1][1] * ey + P[1][2] * ez + P[1][3];
    float cw = P[3][0] * ex + P[3][1] * ey + P[3][2] * ez + P[3][3];
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw)) return false;
    if (std::fabs(cw) < 1e-6f) cw = 1.0f;
    const float invW = 1.0f / cw;
    const float ndcX = cx * invW;
    const float ndcY = cy * invW;

    const float efbW = (g_gx.efbWidth > 0) ? static_cast<float>(g_gx.efbWidth) : 608.0f;
    const float efbH = (g_gx.efbHeight > 0) ? static_cast<float>(g_gx.efbHeight) : 456.0f;
    const bool fullVp = (g_gx.vpLeft <= 4.0f && g_gx.vpTop <= 4.0f &&
                         g_gx.vpWidth >= 500.0f && g_gx.vpHeight >= 400.0f);
    if (fullVp) {
        *outX = (ndcX * 0.5f + 0.5f) * static_cast<float>(kScreenW);
        *outY = (0.5f + g_gpuYSpace * ndcY * 0.5f) * static_cast<float>(kScreenH);
    } else {
        const float efbX = g_gx.vpLeft + (ndcX * 0.5f + 0.5f) * g_gx.vpWidth;
        const float efbY = g_gx.vpTop + (0.5f + g_gpuYSpace * ndcY * 0.5f) * g_gx.vpHeight;
        *outX = efbX * (static_cast<float>(kScreenW) / efbW);
        *outY = efbY * (static_cast<float>(kScreenH) / efbH);
    }
    return true;
}

static uint64_t TexSlotKey(const TexSlot& slot) {
    return (static_cast<uint64_t>(slot.dataAddr) << 32) ^
           (static_cast<uint64_t>(slot.width) << 20) ^
           (static_cast<uint64_t>(slot.height) << 8) ^
           static_cast<uint64_t>(slot.format);
}

// Upload a decoded texture to VRAM, packed to power-of-two size with RGB565 when it is fully opaque
// and RGBA4 otherwise (half the bytes of RGBA8, which is what lets the whole menu set fit).
static C3D_Tex* Gpu2dAcquireTexture(uint64_t key, const DecodedTex& dt,
                                    float* outUScale = nullptr, float* outVScale = nullptr) {
    auto it = g_gpuTextures.find(key);
    if (it != g_gpuTextures.end() && it->second.valid) {
        it->second.lastUse = ++g_gpuTexTick;
        if (outUScale) *outUScale = it->second.uScale;
        if (outVScale) *outVScale = it->second.vScale;
        return &it->second.tex;
    }
    if (dt.width == 0 || dt.height == 0) return nullptr;

    int potW = 8, potH = 8;
    while (potW < dt.width) potW <<= 1;
    while (potH < dt.height) potH <<= 1;
    if (potW > 1024 || potH > 1024) return nullptr;

    bool opaque = true;
    for (size_t i = 0; i < dt.rgba.size(); ++i) {
        if ((dt.rgba[i] & 0xFFu) != 0xFFu) { opaque = false; break; }
    }
    const size_t bytes = static_cast<size_t>(potW) * potH * 2u; // RGB565 / RGBA4 are both 16bpp
    while (g_gpuTexBytes + bytes > g_gpuTexCap && !g_gpuTextures.empty()) {
        // Quads recorded this frame hold raw C3D_Tex pointers, and the previous frame's
        // command list may still be sampling its textures, so neither may be deleted.
        auto victim = g_gpuTextures.end();
        for (auto scan = g_gpuTextures.begin(); scan != g_gpuTextures.end(); ++scan) {
            if (scan->second.lastUse >= g_gpuEvictFloor) continue;
            if (victim == g_gpuTextures.end() || scan->second.lastUse < victim->second.lastUse)
                victim = scan;
        }
        if (victim == g_gpuTextures.end()) break;
        ++g_gpuTexEvictions;
        Gpu2dReleaseEntry(victim);
    }

    GpuTexEntry entry;
    const GPU_TEXCOLOR fmt = opaque ? GPU_RGB565 : GPU_RGBA4;
    if (!C3D_TexInit(&entry.tex, potW, potH, fmt)) {
        AppLog("[Gpu2d] C3D_TexInit failed %dx%d (have %zuKB)\n", potW, potH, g_gpuTexBytes / 1024);
        return nullptr;
    }
    // Write the tiled texels straight into the texture's LINEAR buffer (see PicaTiledIndex),
    // padding included, then flush so the GPU does not sample stale cache lines.  Writing
    // row-major data through C3D_TexUpload without a flush is what produced the "empty"
    // (all-zero) textures earlier: the bytes were still sitting in the ARM11 data cache.
    uint16_t* dst = static_cast<uint16_t*>(entry.tex.data);
    std::memset(dst, 0, bytes);
    for (int y = 0; y < dt.height; ++y) {
        const uint32_t* src = dt.rgba.data() + static_cast<size_t>(y) * dt.width;
        for (int x = 0; x < dt.width; ++x) {
            const uint32_t c = src[x];
            const uint32_t r = (c >> 24) & 0xFFu;
            const uint32_t g = (c >> 16) & 0xFFu;
            const uint32_t b = (c >> 8)  & 0xFFu;
            const uint32_t a = c & 0xFFu;
            uint16_t v;
            if (opaque) {
                v = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                v = static_cast<uint16_t>(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4));
            }
            dst[PicaTiledIndex(x, y, potW)] = v;
        }
    }
    // Clamp-to-edge samples the padding at the image border; replicate the last row/column
    // into it so bilinear filtering at the right/bottom edge does not fade to transparent black.
    for (int y = 0; y < dt.height; ++y)
        for (int x = dt.width; x < potW; ++x)
            dst[PicaTiledIndex(x, y, potW)] = dst[PicaTiledIndex(dt.width - 1, y, potW)];
    for (int y = dt.height; y < potH; ++y)
        for (int x = 0; x < potW; ++x)
            dst[PicaTiledIndex(x, y, potW)] = dst[PicaTiledIndex(x, dt.height - 1, potW)];
    C3D_TexFlush(&entry.tex);
    AppLog("[Gpu2d] upload pot=%dx%d %s texel0=%04X region=%dx%d\n",
        potW, potH, opaque ? "RGB565" : "RGBA4", dst[0], dt.width, dt.height);
    C3D_TexSetFilter(&entry.tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&entry.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    entry.valid = true;
    entry.bytes = bytes;
    entry.lastUse = ++g_gpuTexTick;
    entry.potW = potW;
    entry.potH = potH;
    entry.uScale = static_cast<float>(dt.width) / static_cast<float>(potW);
    entry.vScale = static_cast<float>(dt.height) / static_cast<float>(potH);
    g_gpuTexBytes += bytes;
    ++g_gpuTexUploads;
    auto inserted = g_gpuTextures.emplace(key, entry);
    if (outUScale) *outUScale = inserted.first->second.uScale;
    if (outVScale) *outVScale = inserted.first->second.vScale;
    return &inserted.first->second.tex;
}

// Same slot choice the software path makes for ortho draws.
static int SelectTexSlotFor2D() {
    if (g_gx.texStageEnabled[0] && g_gx.stageTexMap[0] < 8 && g_gx.texSlots[g_gx.stageTexMap[0]].valid)
        return g_gx.stageTexMap[0];
    if (g_gx.activeTexSlot >= 0 && g_gx.activeTexSlot < 8 && g_gx.texSlots[g_gx.activeTexSlot].valid)
        return g_gx.activeTexSlot;
    for (int s = 0; s < 8; ++s)
        if (g_gx.texSlots[s].valid) return s;
    return -1;
}

static void Gpu2dRecordQuad(const GxVertex v[4]) {
    if (g_gpuQuads.size() >= 8192) return;
    GpuQuad q;
    // nw4r::lyt hands DrawQuad the pane's *local* rectangle (anchored at its base position) and
    // loads the pane's global matrix (view * parents * local SRT) with GXLoadPosMtxImm just
    // before.  Dropping that matrix is what made the title look "vertically mirrored": the
    // two halves of the background and the logo pieces all sat at their local origins (e.g. the
    // logo at y 0..-140, i.e. below centre), and the mirrored logo halves -- drawn with a
    // negative-scale matrix -- lost their flip, which is why they appeared to carry pre-flipped
    // UVs.  The software rasteriser still forces identity for ortho draws (pnmtx=0 = same).
    const float* M = g_gx.pnMtx[g_gx.currentPnMtx < 10 ? g_gx.currentPnMtx : 0];
    for (int i = 0; i < 4; ++i) {
        float pos[3] = {v[i].pos[0], v[i].pos[1], v[i].pos[2]};
        if (g_gpuPnMtx) {
            pos[0] = M[0] * v[i].pos[0] + M[1] * v[i].pos[1] + M[2]  * v[i].pos[2] + M[3];
            pos[1] = M[4] * v[i].pos[0] + M[5] * v[i].pos[1] + M[6]  * v[i].pos[2] + M[7];
            pos[2] = M[8] * v[i].pos[0] + M[9] * v[i].pos[1] + M[10] * v[i].pos[2] + M[11];
        }
        float sx = 0.0f, sy = 0.0f;
        if (!Project2DPoint(pos, &sx, &sy)) return;
        q.v[i].x = sx;
        q.v[i].y = sy;
        q.v[i].u = v[i].hasUv0 ? v[i].uv0[0] : 0.0f;
        q.v[i].v = v[i].hasUv0 ? (g_gpuVFlip ? 1.0f - v[i].uv0[1] : v[i].uv0[1]) : 0.0f;
        q.v[i].r = v[i].color0[0] / 255.0f;
        q.v[i].g = v[i].color0[1] / 255.0f;
        q.v[i].b = v[i].color0[2] / 255.0f;
        q.v[i].a = v[i].color0[3] / 255.0f;
    }

    const int slotIdx = SelectTexSlotFor2D();
    const DecodedTex* dt = nullptr;
    C3D_Tex* tex = nullptr;
    if (slotIdx >= 0 && v[0].hasUv0) {
        const TexSlot& slot = g_gx.texSlots[slotIdx];
        dt = GetOrDecodeTexture(slot);
        if (dt) {
            tex = Gpu2dAcquireTexture(TexSlotKey(slot), *dt, &q.uScale, &q.vScale);
            for (const auto& entry : g_gpuTextures) {
                if (&entry.second.tex == tex) { q.potW = entry.second.potW; q.potH = entry.second.potH; }
            }
            // GX wrap (0 clamp, 1 repeat, 2 mirror) only maps onto the PICA when the axis is not
            // padded; the menu's tiled patterns (e.g. the 32x32 one drawn with v 0..30) are POT.
            auto pica = [](uint32_t gxWrap, bool unpadded) {
                if (!unpadded) return GPU_CLAMP_TO_EDGE;
                return gxWrap == 1 ? GPU_REPEAT : gxWrap == 2 ? GPU_MIRRORED_REPEAT : GPU_CLAMP_TO_EDGE;
            };
            q.wrapS = pica(slot.wrapS, q.uScale == 1.0f);
            q.wrapT = pica(slot.wrapT, q.vScale == 1.0f);
        }
    }
    // test=2 draws the real layout with the white texture substituted, to separate "the geometry
    // and blending work" from "the uploaded texture samples as nothing".
    if (g_gpuTestMode == 2 && g_gpuWhiteValid) { tex = &g_gpuWhite; q.uScale = 1.0f; q.vScale = 1.0f; }
    q.tex = tex;
    q.blend = (g_gx.blendMode != 0);
    q.clip[0] = g_gx.clipX0;
    q.clip[1] = g_gx.clipY0;
    q.clip[2] = g_gx.clipX1;
    q.clip[3] = g_gx.clipY1;
    if (g_gpuDebugQuads > 0) {
        --g_gpuDebugQuads;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
        for (int i = 0; i < 4; ++i) {
            minX = std::min(minX, q.v[i].x); maxX = std::max(maxX, q.v[i].x);
            minY = std::min(minY, q.v[i].y); maxY = std::max(maxY, q.v[i].y);
            minU = std::min(minU, q.v[i].u); maxU = std::max(maxU, q.v[i].u);
            minV = std::min(minV, q.v[i].v); maxV = std::max(maxV, q.v[i].v);
        }
        AppLog("[Gpu2d Quad] f=%u rect=(%.1f,%.1f)-(%.1f,%.1f) uv=(%.3f,%.3f)-(%.3f,%.3f)"
               " rgba=(%.2f,%.2f,%.2f,%.2f) tex=%ux%u blend=%u clip=(%d,%d)-(%d,%d) proj=%.5f,%.5f\n",
            g_gxFrame, minX, minY, maxX, maxY, minU, minV, maxU, maxV,
            q.v[0].r, q.v[0].g, q.v[0].b, q.v[0].a,
            dt ? dt->width : 0u, dt ? dt->height : 0u, q.blend ? 1u : 0u,
            q.clip[0], q.clip[1], q.clip[2], q.clip[3], g_gx.proj[0][0], g_gx.proj[1][1]);
        AppLog("[Gpu2d Tex] f=%u gputex=%s pot=%dx%d uScale=%.3f vScale=%.3f cpuFirst=%08X\n",
            g_gxFrame, q.tex ? "ok" : "null", q.potW, q.potH, q.uScale, q.vScale,
            (dt && !dt->rgba.empty()) ? dt->rgba[0] : 0u);
    }
    g_gpuQuads.push_back(q);
    ++g_gpuQuadsRecorded;
}

// Copy the software framebuffer (clear colour + any software-rendered 3D) into a texture so the GPU
// pass can composite the layout on top of it.
static bool Gpu2dUploadBase() {
    static const int kPotW = 512, kPotH = 256;
    if (!g_gpuBaseValid) {
        if (!C3D_TexInit(&g_gpuBase, kPotW, kPotH, GPU_RGB565)) return false;
        C3D_TexSetFilter(&g_gpuBase, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&g_gpuBase, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        g_gpuBaseValid = true;
    }
    // Tiled straight into the texture's LINEAR buffer (FrameBegin(SYNCDRAW) has already waited
    // for the previous frame, so the GPU is not reading it), then flushed.
    uint16_t* dst = static_cast<uint16_t*>(g_gpuBase.data);
    for (int y = 0; y < kScreenH; ++y) {
        const uint32_t* src = g_fbColor + static_cast<size_t>(y) * kScreenW;
        for (int x = 0; x < kScreenW; ++x) {
            const uint32_t c = src[x];
            const uint32_t r = (c >> 24) & 0xFFu, g = (c >> 16) & 0xFFu, b = (c >> 8) & 0xFFu;
            dst[PicaTiledIndex(x, y, kPotW)] =
                static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    C3D_TexFlush(&g_gpuBase);
    ++g_gpuBaseUploads;
    return true;
}

// Project a layout pixel coordinate through the (OrthoTilt) projection on the CPU, so the draw
// does not depend on which float-uniform slot citro3d's built-in vertex shader reads.
static inline void TransformToClip(const C3D_Mtx& m, float x, float y, float* outX, float* outY) {
    // C3D_FVec stores components as {w, z, y, x}, so use named fields instead of raw .c[] indices.
    const float cx = m.r[0].x * x + m.r[0].y * y + m.r[0].w;
    const float cy = m.r[1].x * x + m.r[1].y * y + m.r[1].w;
    const float cw = m.r[3].x * x + m.r[3].y * y + m.r[3].w;
    const float inv = (std::fabs(cw) > 1e-6f) ? 1.0f / cw : 1.0f;
    *outX = cx * inv;
    *outY = cy * inv;
}

static inline void FillGpuDrawVertex(GpuDrawVertex& dst, float x, float y, float u, float v,
                                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TransformToClip(g_gpuProj, x, y, &dst.position[0], &dst.position[1]);
    dst.position[2] = 0.0f;
    dst.normal[0] = dst.normal[1] = 0.0f;
    dst.normal[2] = 1.0f;
    dst.texcoord[0] = u;
    dst.texcoord[1] = v;
    dst.texcoord[2] = dst.texcoord[3] = 0.0f;
    dst.color[0] = r;
    dst.color[1] = g;
    dst.color[2] = b;
    dst.color[3] = a;
}

static inline void EmitImmQuadVertex(float x, float y, float u, float v,
                                     float r, float g, float b, float a) {
    float cx = 0.0f, cy = 0.0f;
    TransformToClip(g_gpuProj, x, y, &cx, &cy);
    C3D_ImmSendAttrib(cx, cy, 0.0f, 1.0f);
    C3D_ImmSendAttrib(0.0f, 0.0f, 1.0f, 0.0f);
    C3D_ImmSendAttrib(u, v, 0.0f, 0.0f);
    C3D_ImmSendAttrib(r, g, b, a);
}

static void Gpu2dRender() {
    if (!g_gpuReady) return;
    const uint64_t profStart = ProfNow();
    struct ProfGuard {
        uint64_t start;
        ~ProfGuard() { g_profGpuTicks += ProfNow() - start; }
    } profGuard{profStart};
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    const uint32_t guestClear = (g_gx.clearColor[0] << 24) | (g_gx.clearColor[1] << 16) |
                                (g_gx.clearColor[2] << 8) | 0xFFu;
    const uint32_t clear = g_gpuClearOverride ? g_gpuClearOverride : guestClear;
    C3D_RenderTargetClear(g_gpuTop, C3D_CLEAR_ALL, clear, 0);
    C3D_FrameDrawOn(g_gpuTop);
    // Match the native renderer's explicit viewport for the rotated top-screen target.
    C3D_SetViewport(0, 0, kScreenH, kScreenW);

    // Match the native renderer: bind the program before initializing its vertex input layout.
    // C3D_BindProgram may update persistent shader/attribute state, so doing this in the opposite
    // order risks invalidating the buffer loaders used by C3D_DrawArrays.
    if (g_gpuProgramReady) C3D_BindProgram(&g_gpuProgram);

    // Attribute layout.  AttrInfo_Init first: citro3d only touches entries it is told about.
    C3D_AttrInfo* ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3); // clip position (pre-projected on the CPU)
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3); // normal (unused by pass-through shader)
    AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4); // UV0/UV1
    AttrInfo_AddLoader(ai, 3, GPU_UNSIGNED_BYTE, 4); // normalized RGBA vertex colour
    for (int i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    {
        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    }

    g_gpuQuadsDrawn = 0;
    // test=1: a pure reference pattern (full-screen quad + two blocks) instead of the guest's
    // layout.  If this does not appear, nothing about the frame setup works and the guest is not
    // involved at all.
    if (g_gpuTestMode == 1) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; AppLog("[Gpu2d] drawing the reference pattern\n"); }
        C3D_TexBind(0, g_gpuWhiteValid ? &g_gpuWhite : nullptr);
        C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);
        const float fx[4] = {0.0f, 400.0f, 0.0f, 400.0f};
        const float fy[4] = {0.0f, 0.0f, 240.0f, 240.0f};
        for (int i = 0; i < 4; ++i) {
            EmitImmQuadVertex(fx[i], fy[i], 0.5f, 0.5f, 0.0f, 115.0f, 230.0f, 255.0f);
        }
        C3D_ImmDrawEnd();
        ++g_gpuQuadsDrawn;
        C3D_SetScissor(static_cast<GPU_SCISSORMODE>(0), 0, 0, 0, 0);
        C3D_FrameEnd(0);
        g_gpuQuads.clear();
        return;
    }

    // Anything the software path drew (3D, or a 2D draw that did not come through the layout hook)
    // is uploaded into g_gpuBase and composited via the VBO underneath the GPU drawn layout.
    const bool hasBaseQuad =
        (g_gpuTestMode != 3) &&
        (g_gx.pixels3DThisFrame > 0 || g_gx.tri2DWritten > 0) &&
        Gpu2dUploadBase();
    const size_t baseVertexOffset = hasBaseQuad ? size_t{6} : size_t{0};

    // Submit layout geometry from one explicit linear-memory vertex buffer.  Immediate mode
    // works for the diagnostic pattern, but the menu path needs a persistent vertex stream with
    // per-draw offsets so each texture/state change is represented by a normal PICA draw call.
    const size_t layoutVertexCount = g_gpuQuads.size() * 6;
    const size_t vertexCount = (g_gpuTestMode == 3) ? size_t{6} : (baseVertexOffset + layoutVertexCount);
    if (vertexCount > g_gpuDrawVertexCapacity) {
        size_t newCapacity = 256;
        while (newCapacity < vertexCount) newCapacity *= 2;
        GpuDrawVertex* replacement = static_cast<GpuDrawVertex*>(linearAlloc(newCapacity * sizeof(GpuDrawVertex)));
        if (!replacement) {
            AppLog("[Gpu2d] vertex buffer alloc failed count=%zu\n", vertexCount);
            C3D_SetScissor(static_cast<GPU_SCISSORMODE>(0), 0, 0, 0, 0);
            C3D_FrameEnd(0);
            g_gpuQuads.clear();
            return;
        }
        if (g_gpuDrawVertices) linearFree(g_gpuDrawVertices);
        g_gpuDrawVertices = replacement;
        g_gpuDrawVertexCapacity = newCapacity;
    }
    constexpr int kTriangleCorners[6] = {0, 1, 2, 0, 2, 3};
    if (hasBaseQuad) {
        const float su = static_cast<float>(kScreenW) / 512.0f;
        const float sv = static_cast<float>(kScreenH) / 256.0f;
        const float xs[4] = {0.0f, static_cast<float>(kScreenW), static_cast<float>(kScreenW), 0.0f};
        const float ys[4] = {0.0f, 0.0f, static_cast<float>(kScreenH), static_cast<float>(kScreenH)};
        const float us[4] = {0.0f, su, su, 0.0f};
        // PICA200 textures have v=1 at memory row 0 (where top-down images are written) and v=0 at potH-1.
        const float vs[4] = {1.0f, 1.0f, 1.0f - sv, 1.0f - sv};
        for (int vi = 0; vi < 6; ++vi) {
            const int c = kTriangleCorners[vi];
            FillGpuDrawVertex(g_gpuDrawVertices[vi], xs[c], ys[c], us[c], vs[c], 255, 255, 255, 255);
        }
    }
    auto toColorByte = [](float c) -> uint8_t {
        return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
    };
    if (g_gpuTestMode == 3) {
        const float xs[6] = {0.0f, 400.0f, 400.0f, 0.0f, 400.0f, 0.0f};
        const float ys[6] = {0.0f, 0.0f, 240.0f, 0.0f, 240.0f, 240.0f};
        for (int i = 0; i < 6; ++i) {
            FillGpuDrawVertex(g_gpuDrawVertices[i], xs[i], ys[i], 0.5f, 0.5f, 0, 115, 230, 255);
        }
        AppLog("[Gpu2d] drawing VBO reference pattern vertices=%zu stride=%zu\n",
               vertexCount, sizeof(GpuDrawVertex));
    } else {
        for (size_t qi = 0; qi < g_gpuQuads.size(); ++qi) {
            const GpuQuad& q = g_gpuQuads[qi];
            for (int vi = 0; vi < 6; ++vi) {
                const GpuQuadVtx& src = q.v[kTriangleCorners[vi]];
                FillGpuDrawVertex(g_gpuDrawVertices[baseVertexOffset + qi * 6 + vi],
                                  src.x, src.y, src.u * q.uScale, 1.0f - src.v * q.vScale,
                                  toColorByte(src.r), toColorByte(src.g),
                                  toColorByte(src.b), toColorByte(src.a));
            }
        }
    }
    if (vertexCount != 0) {
        const u32 vertexBytes = static_cast<u32>(vertexCount * sizeof(GpuDrawVertex));
        GSPGPU_FlushDataCache(g_gpuDrawVertices, vertexBytes);
        C3D_BufInfo* buf = C3D_GetBufInfo();
        BufInfo_Init(buf);
        BufInfo_Add(buf, g_gpuDrawVertices, sizeof(GpuDrawVertex), 4, 0x3210);
    }
    g_gpuQuadsDrawn = 0;
    if (g_gpuTestMode == 3) {
        // Isolate vertex fetch and rasterization from texture sampling and alpha blending.
        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        AppLog("[Gpu2d] VBO reference using primary color, blending disabled\n");
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
        ++g_gpuQuadsDrawn;
        C3D_FrameEnd(0);
        g_gpuQuads.clear();
        return;
    }
    if (hasBaseQuad) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        C3D_TexBind(0, &g_gpuBase);
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    }
    for (size_t qi = 0; qi < g_gpuQuads.size(); ++qi) {
        const GpuQuad& q = g_gpuQuads[qi];
        C3D_Tex* tex = q.tex;
        if (!tex && g_gpuWhiteValid) tex = &g_gpuWhite;
        if (!tex) continue;
        if (g_gpuScissor) {
            C3D_SetScissor(static_cast<GPU_SCISSORMODE>(1), static_cast<uint32_t>(std::max(0, q.clip[0])),
                           static_cast<uint32_t>(std::max(0, q.clip[1])),
                           static_cast<uint32_t>(std::min(kScreenW, q.clip[2])),
                           static_cast<uint32_t>(std::min(kScreenH, q.clip[3])));
        }
        if (!q.blend) C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        if (tex != &g_gpuWhite) C3D_TexSetWrap(tex, q.wrapS, q.wrapT);
        C3D_TexBind(0, tex);
        C3D_DrawArrays(GPU_TRIANGLES, static_cast<u32>(baseVertexOffset + qi * 6), 6);
        if (!q.blend) {
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                           GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                           GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        }
        ++g_gpuQuadsDrawn;
    }
    if (g_gpuScissor) C3D_SetScissor(static_cast<GPU_SCISSORMODE>(0), 0, 0, 0, 0);

    // MKW_GPU_TEST: two reference quads at known coordinates.  If these appear the pipeline
    // (projection uniform, TEV, blend, presentation) works and only the guest geometry is wrong;
    // if they do not, the fault is in the frame setup itself.
    if (g_gpuTestMode == 1) {
        C3D_TexBind(0, g_gpuWhiteValid ? &g_gpuWhite : nullptr);
        C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);
        const float xs[4] = {20.0f, 180.0f, 20.0f, 180.0f};
        const float ys[4] = {20.0f, 20.0f, 100.0f, 100.0f};
        for (int i = 0; i < 4; ++i) {
            EmitImmQuadVertex(xs[i], ys[i], 0.5f, 0.5f, 255.0f, 0.0f, 255.0f, 255.0f);
        }
        C3D_ImmDrawEnd();
        if (g_gpuWhiteValid) {
            // Same quad, untextured white texture, to prove texture sampling as well.
            C3D_TexBind(0, &g_gpuWhite);
            C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);
            const float xs2[4] = {200.0f, 380.0f, 200.0f, 380.0f};
            const float ys2[4] = {20.0f, 20.0f, 100.0f, 100.0f};
            for (int i = 0; i < 4; ++i) {
                EmitImmQuadVertex(xs2[i], ys2[i], 0.25f, 0.25f, 0.0f, 255.0f, 0.0f, 255.0f);
            }
            C3D_ImmDrawEnd();
        }
    }
    C3D_FrameEnd(0);
    g_gpuQuads.clear();
    // The frame just submitted is now in flight; the next one starts recording.
    g_gpuEvictFloor = g_gpuFrameStartTick;
    g_gpuFrameStartTick = g_gpuTexTick + 1;
}

#endif // __3DS__

static void DrawLytQuad2D(uint32_t posAddr, uint32_t sizeAddr, uint32_t texCoordCount, uint32_t texCoordAddr, uint32_t colorAddr, uint8_t extraAlpha, uint32_t hook, uint32_t caller) {
    EnsureInitialized();
    if (!Memory::Contains(posAddr, 8) || !Memory::Contains(sizeAddr, 8)) return;
    float x0 = gx_bridge::ReadGuestFloat(posAddr);
    float y0 = gx_bridge::ReadGuestFloat(posAddr + 4);
    float w  = gx_bridge::ReadGuestFloat(sizeAddr);
    float h  = gx_bridge::ReadGuestFloat(sizeAddr + 4);
    float x1 = x0 + w;
    float y1 = y0 - h;

    GxVertex v[4];
    v[0].pos[0] = x0; v[0].pos[1] = y0; v[0].pos[2] = 0.0f;
    v[1].pos[0] = x1; v[1].pos[1] = y0; v[1].pos[2] = 0.0f;
    v[2].pos[0] = x1; v[2].pos[1] = y1; v[2].pos[2] = 0.0f;
    v[3].pos[0] = x0; v[3].pos[1] = y1; v[3].pos[2] = 0.0f;

    // nw4r::lyt::detail::DrawQuad supplies corner attributes in scan order
    // (left-top, right-top, left-bottom, right-bottom), not the triangle-fan order used
    // for the positions above. Pairing the texcoords/colors straight through swaps the
    // last two corners and shears every textured layout quad.
    constexpr int kCornerOrder[4] = {0, 1, 3, 2};
    for (int i = 0; i < 4; ++i) {
        const int src = kCornerOrder[i];
        if (colorAddr != 0 && Memory::Contains(colorAddr + src * 4, 4)) {
            uint32_t c = Memory::Read32(colorAddr + src * 4);
            v[i].color0[0] = (c >> 24) & 0xFFu;
            v[i].color0[1] = (c >> 16) & 0xFFu;
            v[i].color0[2] = (c >> 8)  & 0xFFu;
            v[i].color0[3] = ((c & 0xFFu) * extraAlpha) / 255u;
        } else {
            v[i].color0[0] = g_gx.tevColor[0][0];
            v[i].color0[1] = g_gx.tevColor[0][1];
            v[i].color0[2] = g_gx.tevColor[0][2];
            v[i].color0[3] = (g_gx.tevColor[0][3] * extraAlpha) / 255u;
        }
        v[i].hasColor0 = true;

        // Four contiguous VEC2 corners; the old 32-byte stride walked past the array and
        // produced huge/NaN UVs on vertices 1-3.
        if (texCoordCount > 0 && texCoordAddr != 0 && Memory::Contains(texCoordAddr + src * 8, 8)) {
            v[i].uv0[0] = gx_bridge::ReadGuestFloat(texCoordAddr + src * 8);
            v[i].uv0[1] = gx_bridge::ReadGuestFloat(texCoordAddr + src * 8 + 4);
            v[i].hasUv0 = true;
        }
    }
    const bool trace = g_gxFrame == kGxTraceFrame;
    FrameSamples before{};
    if (trace) {
        ++g_traceDrawOrder;
        before = SampleFramebuffer();
        uint32_t rgba[4];
        for (int i = 0; i < 4; ++i) {
            const auto& c = v[i].color0;
            rgba[i] = (uint32_t(c[0]) << 24) | (uint32_t(c[1]) << 16) | (uint32_t(c[2]) << 8) | c[3];
        }
        AppLog("[GX Trace %u] layout hook=%08X lr=%08X projection=%u rect=(%f,%f %fx%f) rgba=%08X,%08X,%08X,%08X\n",
            g_traceDrawOrder, hook, caller, g_gx.projType, x0, y0, w, h, rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    // Diagnostic: dump the raw texcoord array words for the first few menu quads. This
    // distinguishes a wrong hook ABI from genuinely tiled UVs.
    static uint32_t s_menuRawDiagnostics = 0;
    if (g_gxFrame >= 80 && g_gxFrame <= 90 && s_menuRawDiagnostics < 8 && texCoordAddr != 0) {
        ++s_menuRawDiagnostics;
        char words[384];
        int off = 0;
        for (int i = 0; i < 10 && off < static_cast<int>(sizeof(words)) - 12; ++i) {
            const uint32_t wv = Memory::Read32(texCoordAddr + i * 4);
            off += std::snprintf(words + off, sizeof(words) - off, " %08X", wv);
        }
        float f[20]{};
        for (int i = 0; i < 10; ++i) {
            const uint32_t wv = Memory::Read32(texCoordAddr + i * 4);
            std::memcpy(&f[i * 2], &wv, 4);
        }
        AppLog("[MenuRaw] n=%u lr=%08X texCoord=%08X cnt=%u raw:%s f=[%.3f,%.3f][%.3f,%.3f][%.3f,%.3f][%.3f,%.3f]\n",
            s_menuRawDiagnostics, caller, texCoordAddr, texCoordCount, words,
            f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
    }

    // Capture a small sample of the first menu's layout quads. This lets us distinguish bad
    // guest layout coordinates from an incorrect GX orthographic transform or texture state.
    static uint32_t s_menuQuadDiagnostics = 0;
    if (g_gxFrame >= 80 && g_gxFrame <= 110 && s_menuQuadDiagnostics < 24) {
        ++s_menuQuadDiagnostics;
        const auto& p = g_gx.proj;
        const float diagEfbW = g_gx.efbWidth > 0 ? static_cast<float>(g_gx.efbWidth) : 608.0f;
        const float diagEfbH = g_gx.efbHeight > 0 ? static_cast<float>(g_gx.efbHeight) : 456.0f;
        const bool fullVp = (g_gx.vpLeft <= 4.0f && g_gx.vpTop <= 4.0f &&
            g_gx.vpWidth >= 500.0f && g_gx.vpHeight >= 400.0f);
        float screen[4][2]{};
        const float xs[4] = {x0, x1, x1, x0};
        const float ys[4] = {y0, y0, y1, y1};
        for (int i = 0; i < 4; ++i) {
            const float cx = p[0][0] * xs[i] + p[0][1] * ys[i] + p[0][3];
            const float cy = p[1][0] * xs[i] + p[1][1] * ys[i] + p[1][3];
            const float cw = p[3][0] * xs[i] + p[3][1] * ys[i] + p[3][3];
            const float invW = std::fabs(cw) > 1e-6f ? 1.0f / cw : 1.0f;
            const float ndcX = cx * invW;
            const float ndcY = cy * invW;
            if (fullVp) {
                screen[i][0] = (ndcX * 0.5f + 0.5f) * kScreenW;
                screen[i][1] = (0.5f - ndcY * 0.5f) * kScreenH;
            } else {
                screen[i][0] = (g_gx.vpLeft + (ndcX * 0.5f + 0.5f) * g_gx.vpWidth) *
                    (static_cast<float>(kScreenW) / diagEfbW);
                screen[i][1] = (g_gx.vpTop + (0.5f - ndcY * 0.5f) * g_gx.vpHeight) *
                    (static_cast<float>(kScreenH) / diagEfbH);
            }
        }
        int diagSlot = -1;
        if (g_gx.texStageEnabled[0] && g_gx.stageTexMap[0] < 8 && g_gx.texSlots[g_gx.stageTexMap[0]].valid)
            diagSlot = g_gx.stageTexMap[0];
        else if (g_gx.activeTexSlot >= 0 && g_gx.activeTexSlot < 8 && g_gx.texSlots[g_gx.activeTexSlot].valid)
            diagSlot = g_gx.activeTexSlot;
        else {
            for (int slot = 0; slot < 8; ++slot) if (g_gx.texSlots[slot].valid) { diagSlot = slot; break; }
        }
        const TexSlot* diagTex = diagSlot >= 0 ? &g_gx.texSlots[diagSlot] : nullptr;
        AppLog("[MenuQuad] n=%u frame=%u hook=%08X lr=%08X proj=%u rect=(%.1f,%.1f %.1fx%.1f) screen=(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f) P=(%.5g,%.5g,%.5g,%.5g) vp=(%.1f,%.1f %.1fx%.1f) texCount=%u uv0=(%.3f,%.3f) uv2=(%.3f,%.3f) color=%02X%02X%02X%02X texOn=%u slot=%d active=%d tex=%08X %ux%u fmt=%u wrap=%u/%u valid=%u\n",
            s_menuQuadDiagnostics, g_gxFrame, hook, caller, g_gx.projType, x0, y0, w, h,
            screen[0][0], screen[0][1], screen[1][0], screen[1][1],
            screen[2][0], screen[2][1], screen[3][0], screen[3][1],
            p[0][0], p[0][3], p[1][1], p[1][3],
            g_gx.vpLeft, g_gx.vpTop, g_gx.vpWidth, g_gx.vpHeight,
            texCoordCount, v[0].uv0[0], v[0].uv0[1], v[2].uv0[0], v[2].uv0[1],
            v[0].color0[0], v[0].color0[1], v[0].color0[2], v[0].color0[3],
            g_gx.texStageEnabled[0] ? 1u : 0u, diagSlot, g_gx.activeTexSlot,
            diagTex ? diagTex->dataAddr : 0, diagTex ? diagTex->width : 0,
            diagTex ? diagTex->height : 0, diagTex ? diagTex->format : 0,
            diagTex ? diagTex->wrapS : 0, diagTex ? diagTex->wrapT : 0,
            diagTex && diagTex->valid ? 1u : 0u);
        const DecodedTex* sampledTexture = (diagTex && diagTex->valid && v[0].hasUv0)
            ? GetOrDecodeTexture(*diagTex) : nullptr;
        // Dump the decoded tile art once per unique texture so texture decode errors are visible.
        static std::set<uint32_t> s_dumpedTex;
        if (sampledTexture && s_dumpedTex.size() < 12 && s_dumpedTex.insert(diagTex->dataAddr).second) {
            char texPath[64];
            std::snprintf(texPath, sizeof(texPath), "%stex_%08X_%ux%u_f%u.ppm",
                (g_gxFrame == 0xFFFFFFFFu) ? "sdmc:/" : "",
                diagTex->dataAddr, sampledTexture->width, sampledTexture->height, sampledTexture->format);
            FILE* tf = std::fopen(texPath, "wb");
            if (tf) {
                std::fprintf(tf, "P6\n%d %d\n255\n", sampledTexture->width, sampledTexture->height);
                for (uint32_t pi = 0; pi < sampledTexture->width * sampledTexture->height; ++pi) {
                    const uint32_t c = sampledTexture->rgba[pi];
                    const uint8_t rgb[3] = {
                        static_cast<uint8_t>((c >> 24) & 0xFFu),
                        static_cast<uint8_t>((c >> 16) & 0xFFu),
                        static_cast<uint8_t>((c >> 8) & 0xFFu)};
                    std::fwrite(rgb, 1, 3, tf);
                }
                std::fclose(tf);
            }
        }
        if (sampledTexture) {
            const float centerU = (v[0].uv0[0] + v[1].uv0[0] + v[2].uv0[0] + v[3].uv0[0]) * 0.25f;
            const float centerV = (v[0].uv0[1] + v[1].uv0[1] + v[2].uv0[1] + v[3].uv0[1]) * 0.25f;
            const uint32_t t0 = SampleTexture(sampledTexture, *diagTex, v[0].uv0[0], v[0].uv0[1]);
            const uint32_t t1 = SampleTexture(sampledTexture, *diagTex, v[1].uv0[0], v[1].uv0[1]);
            const uint32_t t2 = SampleTexture(sampledTexture, *diagTex, v[2].uv0[0], v[2].uv0[1]);
            const uint32_t t3 = SampleTexture(sampledTexture, *diagTex, v[3].uv0[0], v[3].uv0[1]);
            const uint32_t tc = SampleTexture(sampledTexture, *diagTex, centerU, centerV);
            AppLog("[MenuTexSample] n=%u fmt=%u tex=%ux%u uv=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) rgba=%08X/%08X/%08X/%08X center=%08X\n",
                s_menuQuadDiagnostics, sampledTexture->format, sampledTexture->width, sampledTexture->height,
                v[0].uv0[0], v[0].uv0[1], v[1].uv0[0], v[1].uv0[1],
                v[2].uv0[0], v[2].uv0[1], v[3].uv0[0], v[3].uv0[1], t0, t1, t2, t3, tc);
        }
    }

    // PAL startup diagnostic. This picture pane covered the entire framebuffer in the race trace.
    // Its DrawQuad hook and LR are shared by the HUD, so also match its rectangle and color.
    if (hook == 0x80084D20u && caller == 0x8007B294u && g_gx.projType == 1 &&
        x0 == -416.0f && y0 == 228.0f && w == 832.0f && h == 456.0f &&
        std::all_of(std::begin(v), std::end(v), [](const GxVertex& vertex) {
            const auto& c = vertex.color0;
            return c[0] == 255 && c[1] == 255 && c[2] == 255 && c[3] == 255;
        })) {
        const uint32_t sectionMgr = Memory::Read32(0x809C1E38u);
        if (sectionMgr != 0 && Memory::Read32(sectionMgr + 4) == 0x20u) {
            if (trace) {
                AppLog("[GX Trace %u] suppressed full-screen layout quad in section 0x20 (texCoords=%u)\n",
                    g_traceDrawOrder, texCoordCount);
                LogDrawSamples(g_traceDrawOrder, before);
            }
            return;
        }
    }

    // Suppress RecognizePadWindow / connect controller popup in race
    if (hook == 0x80084D20u && caller == 0x8007B294u && g_gx.projType == 1 &&
        ((w == 220.0f && h == 220.0f) || (w == 240.0f && h == 40.0f))) {
        return;
    }

    ++g_gx.quads2DThisFrame;
    if (g_gpu2d && Gpu2dInit()) {
        Gpu2dRecordQuad(v);
        if (trace) LogDrawSamples(g_traceDrawOrder, before);
        return;
    }
    static uint32_t s_menuFillDiagnostics = 0;
    const bool traceMenuFill = g_gxFrame >= 80 && g_gxFrame <= 110 && s_menuFillDiagnostics < 24;
    g_gx.trackTrianglePixels = traceMenuFill;
    g_layoutQuad = true;
    RasterizeTriangle(v[0], v[1], v[2], g_gx.currentPnMtx);
    const uint32_t firstTriPixels = g_gx.lastTrianglePixels;
    RasterizeTriangle(v[0], v[2], v[3], g_gx.currentPnMtx);
    const uint32_t secondTriPixels = g_gx.lastTrianglePixels;
    g_layoutQuad = false;
    g_gx.trackTrianglePixels = false;
    if (traceMenuFill) {
        ++s_menuFillDiagnostics;
        AppLog("[MenuQuadFill] n=%u pixels=%u/%u\n", s_menuFillDiagnostics,
            firstTriPixels, secondTriPixels);
    }
    if (trace) LogDrawSamples(g_traceDrawOrder, before);
}

static void SetEggVtxState(uint32_t type) {
    EnsureInitialized();
    std::memset(g_gx.vtxDesc, 0, sizeof(g_gx.vtxDesc));
    auto& vf = g_gx.vtxFmt[0];
    switch (type) {
    case 0:
    case 5:
        g_gx.vtxDesc[9] = 1; // POS DIRECT
        g_gx.vtxDesc[11] = 1; // CLR0 DIRECT
        vf[9] = {1, 4, 0};
        vf[11] = {1, 5, 0};
        break;
    case 1:
    case 3:
    case 6:
    case 11:
        g_gx.vtxDesc[9] = 2; // POS INDEX8
        g_gx.vtxDesc[10] = 2; // NRM INDEX8
        g_gx.vtxDesc[13] = 2; // TEX0 INDEX8
        vf[9] = {1, 3, 0}; // S16
        vf[10] = {0, 3, 14};
        vf[13] = {1, 4, 0}; // F32
        break;
    case 2:
    case 4:
        g_gx.vtxDesc[9] = 2; // POS INDEX8
        g_gx.vtxDesc[13] = 2; // TEX0 INDEX8
        vf[9] = {1, 3, 0};
        vf[13] = {1, 4, 0};
        break;
    case 7:
    case 8:
        g_gx.vtxDesc[9] = 1; // POS DIRECT
        g_gx.vtxDesc[13] = 1; // TEX0 DIRECT
        vf[9] = {1, 4, 0};
        vf[13] = {1, 4, 0};
        break;
    case 9:
    case 10:
    default:
        g_gx.vtxDesc[9] = 1; // POS DIRECT
        g_gx.vtxDesc[11] = 1; // CLR0 DIRECT
        g_gx.vtxDesc[13] = 1; // TEX0 DIRECT
        vf[9] = {1, 4, 0};
        vf[11] = {1, 5, 0};
        vf[13] = {1, 4, 0};
        break;
    }
}

static void PresentFrameTo3DS(uint32_t frameNumber) {
    EnsureInitialized();
#ifdef __3DS__
    if (g_gpu2d && Gpu2dInit()) {
        Gpu2dRender();
    } else {
    uint8_t* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr, nullptr);
    if (fb) {
        for (int x = 0; x < kScreenW; ++x) {
            uint8_t* colDst = fb + (x * kScreenH) * 3;
            for (int y = 0; y < kScreenH; ++y) {
                uint32_t rgba = g_fbColor[y * kScreenW + x];
                uint8_t* dst = colDst + (kScreenH - 1 - y) * 3;
                dst[0] = static_cast<uint8_t>((rgba >> 8)  & 0xFFu); // B
                dst[1] = static_cast<uint8_t>((rgba >> 16) & 0xFFu); // G
                dst[2] = static_cast<uint8_t>((rgba >> 24) & 0xFFu); // R
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    }
#endif
    if (frameNumber == kGxTraceFrame || frameNumber == 88 || frameNumber == 96 || frameNumber == 104 ||
        frameNumber == 152 || frameNumber == 296 || frameNumber == 400 || frameNumber == 480 || frameNumber == 560 ||
        (frameNumber > 600 && frameNumber % 300 == 0)) {
        char path[64];
        std::snprintf(path, sizeof(path), "sdmc:/frame_%u.ppm", frameNumber);
        FILE* fp = std::fopen(path, "wb");
        if (!fp) {
            // The host harness has no SDMC mount; keep the dump next to the binary so the
            // headless image can be inspected without Azahar.
            std::snprintf(path, sizeof(path), "frame_%u.ppm", frameNumber);
            fp = std::fopen(path, "wb");
        }
        if (fp) {
            std::fprintf(fp, "P6\n%d %d\n255\n", kScreenW, kScreenH);
            for (int i = 0; i < kScreenW * kScreenH; ++i) {
                uint32_t c = g_fbColor[i];
                uint8_t rgb[3] = {
                    static_cast<uint8_t>((c >> 24) & 0xFFu),
                    static_cast<uint8_t>((c >> 16) & 0xFFu),
                    static_cast<uint8_t>((c >> 8)  & 0xFFu)
                };
                std::fwrite(rgb, 1, 3, fp);
            }
            std::fclose(fp);
            AppLog("[GX Bridge] Saved frame dump %s (draws=%u tris=%u quads2D=%u dls=%u texCache=%zuKB)\n",
                path, g_gx.drawCallsThisFrame, g_gx.trisThisFrame, g_gx.quads2DThisFrame, g_gx.dlCallsThisFrame, g_texCacheBytes / 1024);
        }
    }
    if (frameNumber <= 20 || frameNumber % 20 == 0) {
        const float* m0 = g_gx.pnMtx[0];
        AppLog("[GX Stats Frame %u] draws=%u tris=%u quads2D=%u dls=%u texCache=%zuKB px3D=%u entered3D=%u bounds3D=%u a0_2D=%u wrote2D=%u\n",
            frameNumber, g_gx.drawCallsThisFrame, g_gx.trisThisFrame, g_gx.quads2DThisFrame, g_gx.dlCallsThisFrame, g_texCacheBytes / 1024, g_gx.pixels3DThisFrame, g_gx.tri3DEntered, g_gx.tri3DBounds, g_gx.tri2DAlphaZero, g_gx.tri2DWritten);
        AppLog("[GlobeFlag] frame=%u flag=%u bits=%08X\n",
            frameNumber, Memory::Read8(0x809C19A0u), Memory::Read32(0x809C1874u));
        AppLog("[TevState] frame=%u stages=%u cin0=(%u,%u,%u,%u) ain0=(%u,%u,%u,%u) cop=%u aop=%u acomp0=%u ref0=%u op=%u acomp1=%u ref1=%u c0=%u,%u,%u,%u c2=%u,%u,%u,%u\n",
            frameNumber, gx_bridge::g_gx.tevNumStages,
            gx_bridge::g_gx.tevColorIn[0][0], gx_bridge::g_gx.tevColorIn[0][1], gx_bridge::g_gx.tevColorIn[0][2], gx_bridge::g_gx.tevColorIn[0][3],
            gx_bridge::g_gx.tevAlphaIn[0][0], gx_bridge::g_gx.tevAlphaIn[0][1], gx_bridge::g_gx.tevAlphaIn[0][2], gx_bridge::g_gx.tevAlphaIn[0][3],
            gx_bridge::g_gx.tevColorOp[0], gx_bridge::g_gx.tevAlphaOp[0],
            gx_bridge::g_gx.alphaComp0, gx_bridge::g_gx.alphaRef0, gx_bridge::g_gx.alphaCompOp,
            gx_bridge::g_gx.alphaComp1, gx_bridge::g_gx.alphaRef1,
            gx_bridge::g_gx.tevColor[0][0], gx_bridge::g_gx.tevColor[0][1], gx_bridge::g_gx.tevColor[0][2], gx_bridge::g_gx.tevColor[0][3],
            gx_bridge::g_gx.tevColor[2][0], gx_bridge::g_gx.tevColor[2][1], gx_bridge::g_gx.tevColor[2][2], gx_bridge::g_gx.tevColor[2][3]);
        {
            const uint32_t mgr = Memory::Read32(0x809C2144u);
            if (mgr && Memory::Contains(mgr + 30072, 4)) {
                AppLog("[MenuState] frame=%u mgr=%08X state=%u s30076=%u\n",
                    frameNumber, mgr, Memory::Read32(mgr + 30072), Memory::Read32(mgr + 30076));
            }
        }
        {
            // 444-byte "menu director" created by func_8077EF84 right before the globe object.
            // Its fields are the strongest candidate for the missing globe sub-objects.
            const uint32_t dir = Memory::Read32(0x809C2DF0u);
            if (dir && Memory::Contains(dir, 444)) {
                char buf[512];
                int n = std::snprintf(buf, sizeof(buf), "[MenuDirector] frame=%u obj=%08X vt=%08X",
                    frameNumber, dir, Memory::Read32(dir));
                for (int i = 1; i < 16 && n > 0 && n < static_cast<int>(sizeof(buf)) - 16; ++i) {
                    n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), " +%02X=%08X", i * 4, Memory::Read32(dir + i * 4));
                }
                AppLog("%s\n", buf);
            }
            const uint32_t go = Memory::Read32(0x809C1988u);
            if (go && Memory::Contains(go, 64)) {
                AppLog("[GlobeFields] frame=%u obj=%08X vt=%08X p16=%08X p20=%08X p24=%08X p28=%08X b32=%u b33=%u p60=%08X\n",
                    frameNumber, go, Memory::Read32(go), Memory::Read32(go + 16),
                    Memory::Read32(go + 20), Memory::Read32(go + 24), Memory::Read32(go + 28),
                    Memory::Read8(go + 32), Memory::Read8(go + 33), Memory::Read32(go + 60));
            }
        }
        if (gx_bridge::g_panicTotal != 0 && (frameNumber % 200 == 0)) {
            std::string sites;
            uint32_t shown = 0;
            for (const auto& kv : gx_bridge::g_panicSites) {
                if (shown++ >= 6) break;
                sites += " " + std::to_string(kv.first) + "x" + std::to_string(kv.second);
            }
            AppLog("[PanicLoop] frame=%u total=%u sites:%s\n",
                frameNumber, gx_bridge::g_panicTotal, sites.c_str());
        }
        if (frameNumber % 10 == 0 && frameNumber <= 120) {
            AppLog("[RegistryWatch] frame=%u sentinel=[%08X %08X %08X %08X %08X %08X]\n",
                frameNumber,
                Memory::Read32(0x80386168u), Memory::Read32(0x8038616Cu),
                Memory::Read32(0x80386170u), Memory::Read32(0x80386174u),
                Memory::Read32(0x80386178u), Memory::Read32(0x8038617Cu));
        }
        AppLog("[GX Mtx Frame %u] loadCalls=%u xfWrites=%u nonIdentity=%u curPn=%u camUpdate=%u m0 r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
            frameNumber, g_gx.mtxLoadCalls, g_gx.xfMtxWrites, g_gx.mtxNonIdentity,
            g_gx.currentPnMtx, g_gx.camUpdateCalls,
            m0[0], m0[1], m0[2], m0[3], m0[4], m0[5], m0[6], m0[7], m0[8], m0[9], m0[10], m0[11]);
        // ArchiveMgr entry states: func_805411FC only accepts entries whose state is 4 or 5,
        // so this shows whether/when each archive group becomes usable.
        if (g_tracedArchiveMgr && Memory::Contains(g_tracedArchiveMgr, 64)) {
            const uint32_t arrayBase = Memory::Read32(g_tracedArchiveMgr + 4);
            std::string arcOut;
            if (arrayBase && Memory::Contains(arrayBase, 32)) {
                for (uint32_t ty = 0; ty < 8; ++ty) {
                    const uint32_t arc = Memory::Read32(arrayBase + ty * 4);
                    if (!arc || !Memory::Contains(arc, 12)) break;
                    const uint32_t count = Memory::Read16(arc + 8);
                    const uint32_t entries = Memory::Read32(arc + 4);
                    if (ty == 0 && entries) { g_watchArcEntries = entries; g_watchArcCount = count; }
                    if (ty < 8) { g_watchArcs[ty] = arc; if (g_watchArcN < ty + 1) g_watchArcN = ty + 1; }
                    arcOut += " t" + std::to_string(ty) + "=[";
                    for (uint32_t e = 0; e < count && e < 6; ++e) {
                        if (!entries || !Memory::Contains(entries + e * 36u, 36)) break;
                        if (e) arcOut += ",";
                        arcOut += std::to_string(Memory::Read32(entries + e * 36u + 32));
                    }
                    arcOut += "]";
                }
            }
            AppLog("[ArcState] frame=%u%s\n", frameNumber, arcOut.c_str());
        }
    }
    if (frameNumber == kGxTraceFrame) {
        AppLog("[GX Perspective Frame %u] nonFinite=%u nearPlane=%u area=%u offscreen=%u rasterized=%u wrotePixels=%u\n",
            frameNumber, g_perspectiveTrace.nonFinite, g_perspectiveTrace.nearPlane,
            g_perspectiveTrace.area, g_perspectiveTrace.offscreen, g_perspectiveTrace.rasterized,
            g_perspectiveTrace.wrotePixels);
        g_perspectiveTrace = {};
    }
    if (std::getenv("MKW_FORCE_GLOBE") && frameNumber >= 90) {
        Memory::Write8(0x809C19A0u, 1);
        const uint32_t globeObj = Memory::Read32(0x809C1988u);
        if (globeObj && Memory::Contains(globeObj + 32, 1)) {
            Memory::Write8(globeObj + 32, 1);
        }
        if (const char* st = std::getenv("MKW_FORCE_GLOBE_STATE")) {
            const uint32_t mgr = Memory::Read32(0x809C2144u);
            if (mgr && Memory::Contains(mgr + 30072, 4)) {
                // +16 is the state machine's "active" flag; the update returns immediately
                // while it is 0, so a state override alone never runs the handler.
                if (Memory::Contains(mgr + 16, 4)) Memory::Write32(mgr + 16, 1);
                Memory::Write32(mgr + 30072, static_cast<uint32_t>(std::atoi(st)));
            }
        }
    }
    g_gxFrame = frameNumber + 1;
    g_gx.drawCallsThisFrame = 0;
    g_gx.trisThisFrame = 0;
    g_gx.quads2DThisFrame = 0;
    g_gx.pixels3DThisFrame = 0;
    g_gx.tri3DEntered = 0;
    g_gx.tri3DBounds = 0;
    g_gx.tri2DAlphaZero = 0;
    g_gx.tri2DWritten = 0;
    g_gx.dlCallsThisFrame = 0;
    g_gx.mtxLoadCalls = 0;
    g_gx.xfMtxWrites = 0;
    g_gx.mtxNonIdentity = 0;
    g_gx.camUpdateCalls = 0;
    g_gx.drawCallsSkipped = 0;
    g_fifoStream.clear();
    ClearFramebuffer();
}

} // namespace gx_bridge

extern "C" void GX_HLE_FIFO_WriteFloat(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    uint8_t b[4] = {
        static_cast<uint8_t>((u >> 24) & 0xFFu),
        static_cast<uint8_t>((u >> 16) & 0xFFu),
        static_cast<uint8_t>((u >> 8)  & 0xFFu),
        static_cast<uint8_t>(u         & 0xFFu)
    };
    gx_bridge::AppendFifoBytes(b, 4);
}
extern "C" void GX_HLE_FIFO_Write32(uint32_t u) {
    uint8_t b[4] = {
        static_cast<uint8_t>((u >> 24) & 0xFFu),
        static_cast<uint8_t>((u >> 16) & 0xFFu),
        static_cast<uint8_t>((u >> 8)  & 0xFFu),
        static_cast<uint8_t>(u         & 0xFFu)
    };
    gx_bridge::AppendFifoBytes(b, 4);
}
extern "C" void GX_HLE_FIFO_Write16(uint16_t u) {
    uint8_t b[2] = {
        static_cast<uint8_t>((u >> 8) & 0xFFu),
        static_cast<uint8_t>(u        & 0xFFu)
    };
    gx_bridge::AppendFifoBytes(b, 2);
}
extern "C" void GX_HLE_FIFO_Write8(uint8_t u) {
    gx_bridge::AppendFifoBytes(&u, 1);
}
extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t* ptr, uint32_t len) {
    if (ptr && len > 0) {
        gx_bridge::AppendFifoBytes(ptr, len);
    }
}

static std::map<uint32_t, std::string> g_dvdOffsetToPath;
static void InitDvdFstMap() {
    if (!g_dvdOffsetToPath.empty()) return;
    const uint8_t* fst = kStartupFst;
    const uint32_t numEntries = (static_cast<uint32_t>(fst[8]) << 24) |
                                (static_cast<uint32_t>(fst[9]) << 16) |
                                (static_cast<uint32_t>(fst[10]) << 8) |
                                static_cast<uint32_t>(fst[11]);
    const char* strTable = reinterpret_cast<const char*>(fst + numEntries * 12);
    struct DirStack { uint32_t nextEntry; std::string path; };
    std::vector<DirStack> stack;
#ifdef __3DS__
    stack.push_back({numEntries, "sdmc:/mkwii"});
#else
    stack.push_back({numEntries, "../../Assets/extracted-rmcp01/DATA/files"});
#endif

    for (uint32_t i = 1; i < numEntries; ++i) {
        while (!stack.empty() && i >= stack.back().nextEntry) {
            stack.pop_back();
        }
        const uint8_t* entry = fst + i * 12;
        const uint8_t flags = entry[0];
        const uint32_t nameOff = (static_cast<uint32_t>(entry[1]) << 16) |
                                 (static_cast<uint32_t>(entry[2]) << 8) |
                                 static_cast<uint32_t>(entry[3]);
        const char* name = strTable + nameOff;
        if (flags == 1) { // Directory
            const uint32_t nextEntry = (static_cast<uint32_t>(entry[8]) << 24) |
                                       (static_cast<uint32_t>(entry[9]) << 16) |
                                       (static_cast<uint32_t>(entry[10]) << 8) |
                                       static_cast<uint32_t>(entry[11]);
            std::string sub = stack.back().path + "/" + name;
            stack.push_back({nextEntry, std::move(sub)});
        } else { // File
            const uint32_t discWords = (static_cast<uint32_t>(entry[4]) << 24) |
                                       (static_cast<uint32_t>(entry[5]) << 16) |
                                       (static_cast<uint32_t>(entry[6]) << 8) |
                                       static_cast<uint32_t>(entry[7]);
            std::string filePath = stack.back().path + "/" + name;
            g_dvdOffsetToPath[discWords] = std::move(filePath);
        }
    }
    AppLog("[DVD] Indexed %zu files from FST\n", g_dvdOffsetToPath.size());
}

static int32_t HandleDvdRead(uint32_t fileInfoPtr, uint32_t bufferPtr, int32_t length, int32_t offset) {
    InitDvdFstMap();
    if (!Memory::Contains(fileInfoPtr, 0x38)) return -1;
    const uint32_t startWords = Memory::Read32(fileInfoPtr + 0x30);
    const uint32_t fileLen = Memory::Read32(fileInfoPtr + 0x34);

    auto it = g_dvdOffsetToPath.find(startWords);
    if (it == g_dvdOffsetToPath.end()) {
        AppLog("[DVD] Unknown file at offset words 0x%08X\n", startWords);
        return -1;
    }

    if (length < 0) length = static_cast<int32_t>(fileLen) - offset;
    if (length <= 0) return 0;
    if (offset < 0) offset = 0;
    if (bufferPtr == 0) {
        AppLog("[DVD] Read rejected: null bufferPtr for %s (len=%d)\n", it->second.c_str(), length);
        return -1;
    }

    std::FILE* f = std::fopen(it->second.c_str(), "rb");
    if (!f) {
        AppLog("[DVD] Failed to open %s\n", it->second.c_str());
        return -1;
    }
    std::fseek(f, offset, SEEK_SET);
    void* hostDst = Memory::GetPointer(bufferPtr, static_cast<size_t>(length));
    size_t readBytes = std::fread(hostDst, 1, static_cast<size_t>(length), f);
    std::fclose(f);

    AppLog("[DVD] Read %zu bytes from %s to 0x%08X (offset=%d)\n",
        readBytes, it->second.c_str(), bufferPtr, offset);
    std::fflush(stdout);

    Memory::Write32(fileInfoPtr + 0x0C, 0); // DVD_STATE_END
    Memory::Write32(fileInfoPtr + 0x20, static_cast<uint32_t>(readBytes)); // transferred
    return static_cast<int32_t>(readBytes);
}

struct BootRacer { uint32_t character; uint32_t kart; };
static constexpr std::array<BootRacer, 12> kBootRacers{{
    {0x00, 0x01}, {0x07, 0x01}, {0x10, 0x01}, {0x0A, 0x01},
    {0x0F, 0x01}, {0x08, 0x00}, {0x06, 0x00}, {0x0E, 0x00},
    {0x03, 0x02}, {0x09, 0x02}, {0x0B, 0x02}, {0x16, 0x02},
}};

static void PopulateScenario(uint32_t scenario, uint32_t course) {
    if (!scenario || !Memory::Contains(scenario, 0xB50)) return;
    // RacedataScenario layout:
    // +0x00: vtable pointer (MUST NOT OVERWRITE!)
    // +0x04: uint8_t playerCount
    // +0x05: uint8_t unknown_0x5
    // +0x06: uint8_t localPlayerCount
    // +0x07: uint8_t unknown_0x7
    Memory::Write8(scenario + 0x04u, static_cast<uint8_t>(kBootRacers.size())); // playerCount = 12
    Memory::Write8(scenario + 0x05u, 0);
    Memory::Write8(scenario + 0x06u, 1); // localPlayerCount = 1
    Memory::Write8(scenario + 0x07u, 0);

    for (uint32_t i = 0; i < kBootRacers.size(); ++i) {
        const uint32_t player = scenario + 0x08u + i * 0xF0u;
        // RacedataPlayer layout:
        // +0x00: vtable pointer (MUST NOT OVERWRITE!)
        // +0x04: uint8_t unknown_0x4
        // +0x05: int8_t localPlayerNum (0 for Player 0, -1 / 0xFF for CPUs)
        // +0x06: int8_t realControllerId (0 for Player 0 = GC port 0, -1 / 0xFF for CPUs)
        // +0x07: uint8_t unknown_0x7
        // +0x08: uint32_t vehicleId
        // +0x0C: uint32_t characterId
        // +0x10: PlayerType playerType (0 = REAL_LOCAL, 1 = CPU)
        // +0x14: Team team (0 = FFA)
        Memory::Write8(player + 0x04u, 0);
        Memory::Write8(player + 0x05u, i == 0 ? 0 : 0xFF);
        Memory::Write8(player + 0x06u, i == 0 ? 0 : 0xFF);
        Memory::Write8(player + 0x07u, 0);
        Memory::Write32(player + 0x08u, kBootRacers[i].kart);
        Memory::Write32(player + 0x0Cu, kBootRacers[i].character);
        Memory::Write32(player + 0x10u, i == 0 ? 0 : 1);
        Memory::Write32(player + 0x14u, 0);
    }
    const uint32_t settings = scenario + 0xB48u;
    Memory::Write32(settings + 0x00u, course);
    Memory::Write32(settings + 0x04u, 2); // 150cc
    Memory::Write32(settings + 0x08u, 1); // VS mode
    Memory::Write32(settings + 0x0Cu, 0);
    Memory::Write32(settings + 0x10u, 0); // battleType
    Memory::Write32(settings + 0x14u, 2); // CPU hard
    Memory::Write32(settings + 0x18u, 0); // Items balanced
    Memory::Write8(settings + 0x1Cu, 0);  // hudPlayerIds[0] = 0
    Memory::Write8(settings + 0x1Du, 0xFF); // hudPlayerIds[1] = -1
    Memory::Write8(settings + 0x1Eu, 0xFF); // hudPlayerIds[2] = -1
    Memory::Write8(settings + 0x1Fu, 0xFF); // hudPlayerIds[3] = -1
    Memory::Write32(settings + 0x20u, 0); // cupId
    Memory::Write8(settings + 0x24u, 0);  // raceNumber
    Memory::Write8(settings + 0x25u, 3);  // 3 laps
    Memory::Write8(settings + 0x26u, 0);
    Memory::Write8(settings + 0x27u, 0);
    Memory::Write32(settings + 0x28u, 0); // modeFlags
    Memory::Write32(settings + 0x2Cu, 0);
    Memory::Write32(settings + 0x30u, 0);
}

static void SetUpBootRaceScenario(uint32_t course) {
    ReportWorkingSet("Before SetUpBootRaceScenario");
    const uint32_t racedata = Memory::Read32(0x809BD728u);
    if (racedata == 0) return;
    // Populate BOTH menusScenario (+0x0020) and racesScenario (+0x0C10)
    PopulateScenario(racedata + 0x0020u, course);
    PopulateScenario(racedata + 0x0C10u, course);
    AppLog("[BootRace] Menu scenario configured for course 0x%02X (12 racers)\n", course);
}

static uint32_t FindU8Resource(uint32_t arcBase, const char* targetName, uint32_t* outSize = nullptr) {
    if (!arcBase || !Memory::Contains(arcBase, 0x20)) return 0;
    if (Memory::Read32(arcBase) != 0x55AA382Du) return 0;
    const uint32_t rootNodeOffset = Memory::Read32(arcBase + 4);
    if (!Memory::Contains(arcBase + rootNodeOffset, 12)) return 0;
    const uint32_t numNodes = Memory::Read32(arcBase + rootNodeOffset + 8);
    const uint32_t stringTable = arcBase + rootNodeOffset + numNodes * 12;
    for (uint32_t i = 0; i < numNodes; ++i) {
        const uint32_t noff = arcBase + rootNodeOffset + i * 12;
        if (!Memory::Contains(noff, 12)) break;
        const uint32_t typeFlag = Memory::Read8(noff);
        if (typeFlag != 0) continue; // 0 = file, 1 = directory
        const uint32_t nameOffset = (Memory::Read8(noff + 1) << 16) | (Memory::Read8(noff + 2) << 8) | Memory::Read8(noff + 3);
        const uint32_t namePtr = stringTable + nameOffset;
        if (!Memory::Contains(namePtr, 1)) continue;
        const char* nodeName = reinterpret_cast<const char*>(Memory::GetPointer(namePtr, 1));
        if (strcasecmp(nodeName, targetName) == 0) {
            const uint32_t dataOff = Memory::Read32(noff + 4);
            const uint32_t size = Memory::Read32(noff + 8);
            if (outSize) *outSize = size;
            return arcBase + dataOff;
        }
    }
    return 0;
}

static const uint8_t* FindHostU8Resource(const std::vector<uint8_t>& arc, const char* targetName,
                                        uint32_t* outSize = nullptr) {
    if (arc.size() < 0x20 || gx_bridge::ReadBE32(arc.data()) != 0x55AA382Du) return nullptr;
    const uint32_t root = gx_bridge::ReadBE32(arc.data() + 4);
    if (root > arc.size() || arc.size() - root < 12) return nullptr;
    const uint32_t count = gx_bridge::ReadBE32(arc.data() + root + 8);
    if (count > (arc.size() - root) / 12) return nullptr;
    const size_t strings = static_cast<size_t>(root) + static_cast<size_t>(count) * 12;
    if (strings > arc.size()) return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t node = static_cast<size_t>(root) + static_cast<size_t>(i) * 12;
        if (arc[node] != 0) continue;
        const uint32_t nameOff = (static_cast<uint32_t>(arc[node + 1]) << 16) |
                                 (static_cast<uint32_t>(arc[node + 2]) << 8) | arc[node + 3];
        const size_t name = strings + nameOff;
        if (name >= arc.size()) continue;
        size_t end = name;
        while (end < arc.size() && arc[end] != 0) ++end;
        if (end == arc.size()) continue;
        const std::string nodeName(reinterpret_cast<const char*>(arc.data() + name), end - name);
        if (strcasecmp(nodeName.c_str(), targetName) != 0) continue;
        const uint32_t offset = gx_bridge::ReadBE32(arc.data() + node + 4);
        const uint32_t size = gx_bridge::ReadBE32(arc.data() + node + 8);
        if (offset > arc.size() || size > arc.size() - offset) return nullptr;
        if (outSize) *outSize = size;
        return arc.data() + offset;
    }
    return nullptr;
}

static uint32_t LoadMenuOtherResource(const char* requestedName, uint32_t* outSize) {
    struct CachedResource { const char* name; uint32_t address; uint32_t size; };
    static CachedResource s_resources[] = {
        {"OptionBG.brctr", 0, 0},
        {"option_bg.brlyt", 0, 0},
        {"option_bg_Loop.brlan", 0, 0},
    };
    static bool s_loaded = false;
    static bool s_reportedFailure = false;
    const auto reportFailure = [&](const char* reason) -> uint32_t {
        if (!s_reportedFailure) {
            s_reportedFailure = true;
            AppLog("[MenuResource] %s\n", reason);
        }
        return 0;
    };
    if (s_loaded) {
        for (const auto& resource : s_resources) {
            if (strcasecmp(requestedName, resource.name) == 0 && resource.address) {
                if (outSize) *outSize = resource.size;
                return resource.address;
            }
        }
        return 0;
    }

    const char* paths[] = {
#ifdef __3DS__
        "sdmc:/mkwii/Scene/UI/MenuOther.szs",
#endif
        "../../Assets/extracted-rmcp01/DATA/files/Scene/UI/MenuOther.szs",
        "Assets/extracted-rmcp01/DATA/files/Scene/UI/MenuOther.szs"
    };
    std::FILE* file = nullptr;
    for (const char* path : paths) {
        file = std::fopen(path, "rb");
        if (file) break;
    }
    if (!file) {
        return reportFailure("could not open MenuOther.szs from SDMC or workspace paths");
    }
    std::fseek(file, 0, SEEK_END);
    const long fileSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (fileSize < 16) {
        std::fclose(file);
        return reportFailure("MenuOther.szs is too small");
    }
    std::vector<uint8_t> compressed;
    try {
        compressed.resize(static_cast<size_t>(fileSize));
    } catch (...) {
        std::fclose(file);
        return 0;
    }
    if (std::fread(compressed.data(), 1, compressed.size(), file) != compressed.size()) {
        std::fclose(file);
        return reportFailure("short read from MenuOther.szs");
    }
    std::fclose(file);

    if (std::memcmp(compressed.data(), "Yaz0", 4) != 0) {
        return reportFailure("MenuOther.szs has an invalid Yaz0 header");
    }
    const uint32_t expandedSize = gx_bridge::ReadBE32(compressed.data() + 4);
    if (expandedSize == 0 || expandedSize > 16u * 1024u * 1024u) {
        return reportFailure("MenuOther.szs expanded size is out of range");
    }
    // Only three small U8 files are needed here. Stream Yaz0 into a 4 KiB history window,
    // retain just the U8 node/name table, then copy matching file ranges directly to guest RAM.
    // Keeping a second ~8 MiB expanded archive exhausted Old 3DS heap after guest RAM setup.
    constexpr uint32_t kMenuResourceAddress = kRaceSzsBase + kRaceSzsSize - 0x10000u;
    constexpr uint32_t kMenuResourceCapacity = 0x10000u;
    struct ExtractRange { uint32_t begin = 0; uint32_t end = 0; uint32_t guest = 0; };
    std::array<ExtractRange, 3> ranges{};
    std::array<uint8_t, 0x1000> history{};
    std::vector<uint8_t> metadata;
    try { metadata.reserve(0x10000); }
    catch (...) { return reportFailure("could not allocate the small MenuOther U8 index buffer"); }

    size_t src = 16;
    uint32_t dst = 0;
    uint32_t cursor = 0;
    uint32_t nextMetadataProbe = 12;
    uint32_t metadataEnd = 0;
    bool rangesReady = false;
    uint8_t flags = 0;
    int bitsLeft = 0;
    auto acceptByte = [&](uint8_t value) {
        history[dst & 0xFFFu] = value;
        if (!rangesReady && metadata.size() < 0x100000u) metadata.push_back(value);
        if (rangesReady) {
            for (uint32_t i = 0; i < ranges.size(); ++i) {
                const auto& range = ranges[i];
                if (range.end <= range.begin || dst < range.begin || dst >= range.end) continue;
                Memory::Write8(range.guest + (dst - range.begin), value);
            }
        }
        ++dst;
    };
    auto probeMetadata = [&]() -> bool {
        if (rangesReady || metadata.size() < nextMetadataProbe) return true;
        nextMetadataProbe = std::min<uint32_t>(0x100000u, nextMetadataProbe * 2u);
        if (metadata.size() < 12 || gx_bridge::ReadBE32(metadata.data()) != 0x55AA382Du) return true;
        const uint32_t root = gx_bridge::ReadBE32(metadata.data() + 4);
        if (root > 0x100000u || root > metadata.size() || metadata.size() - root < 12) return true;
        const uint32_t count = gx_bridge::ReadBE32(metadata.data() + root + 8);
        if (count > (0x100000u - root) / 12u) return false;
        const uint32_t strings = root + count * 12u;
        if (metadata.size() < strings) return metadata.size() < 0x100000u;
        uint32_t tableEnd = strings;
        for (uint32_t n = 0; n < count; ++n) {
            const uint32_t node = root + n * 12u;
            const uint32_t nameOff = (static_cast<uint32_t>(metadata[node + 1]) << 16) |
                (static_cast<uint32_t>(metadata[node + 2]) << 8) | metadata[node + 3];
            const uint32_t name = strings + nameOff;
            if (name >= 0x100000u) return false;
            uint32_t endName = name;
            while (endName < metadata.size() && metadata[endName] != 0) ++endName;
            if (endName == metadata.size()) return metadata.size() < 0x100000u;
            tableEnd = std::max(tableEnd, endName + 1u);
        }
        metadataEnd = tableEnd;
        if (metadata.size() < metadataEnd) return true;

        for (uint32_t i = 0; i < ranges.size(); ++i) {
            for (uint32_t n = 0; n < count; ++n) {
                const uint32_t node = root + n * 12u;
                if (metadata[node] != 0) continue;
                const uint32_t nameOff = (static_cast<uint32_t>(metadata[node + 1]) << 16) |
                    (static_cast<uint32_t>(metadata[node + 2]) << 8) | metadata[node + 3];
                const char* name = reinterpret_cast<const char*>(metadata.data() + strings + nameOff);
                if (strcasecmp(name, s_resources[i].name) != 0) continue;
                const uint32_t begin = gx_bridge::ReadBE32(metadata.data() + node + 4);
                const uint32_t size = gx_bridge::ReadBE32(metadata.data() + node + 8);
                if (!size || size > 0x1000u || begin > expandedSize || size > expandedSize - begin) break;
                cursor = (cursor + 31u) & ~31u;
                if (cursor > kMenuResourceCapacity || size > kMenuResourceCapacity - cursor) break;
                ranges[i] = {begin, begin + size, kMenuResourceAddress + cursor};
                s_resources[i].address = ranges[i].guest;
                s_resources[i].size = size;
                cursor += size;
                break;
            }
        }
        rangesReady = true;
        // Some archives place a tiny requested resource before the final name string.
        for (const auto& range : ranges) {
            if (range.end <= range.begin || range.begin >= metadata.size()) continue;
            const uint32_t copyEnd = std::min<uint32_t>(range.end, static_cast<uint32_t>(metadata.size()));
            for (uint32_t p = range.begin; p < copyEnd; ++p)
                Memory::Write8(range.guest + (p - range.begin), metadata[p]);
        }
        return true;
    };

    while (dst < expandedSize && src < compressed.size()) {
        if (bitsLeft == 0) {
            flags = compressed[src++];
            bitsLeft = 8;
        }
        uint32_t count = 1;
        uint32_t distance = 0;
        if (flags & 0x80u) {
            if (src >= compressed.size()) return reportFailure("truncated literal in MenuOther.szs Yaz0 stream");
        } else {
            if (src + 1 >= compressed.size()) return reportFailure("truncated back-reference in MenuOther.szs Yaz0 stream");
            const uint8_t a = compressed[src++];
            const uint8_t b = compressed[src++];
            distance = (((static_cast<uint32_t>(a) & 0x0Fu) << 8) | b) + 1u;
            count = a >> 4;
            if (!count) {
                if (src >= compressed.size()) return reportFailure("truncated long back-reference in MenuOther.szs Yaz0 stream");
                count = static_cast<uint32_t>(compressed[src++]) + 18u;
            } else count += 2u;
            if (distance > dst) return reportFailure("invalid back-reference in MenuOther.szs Yaz0 stream");
        }
        flags <<= 1;
        --bitsLeft;
        for (uint32_t j = 0; j < count && dst < expandedSize; ++j) {
            uint8_t value;
            if (!distance) value = compressed[src++];
            else value = history[(dst - distance) & 0xFFFu];
            acceptByte(value);
            if (!probeMetadata()) return reportFailure("invalid or oversized MenuOther U8 node table");
        }
    }
    if (dst != expandedSize) return reportFailure("MenuOther.szs decompression ended early");
    if (!rangesReady) return reportFailure("could not read the MenuOther U8 node/name table");
    for (const auto& resource : s_resources) {
        if (resource.address)
            AppLog("[MenuResource] Extracted %s from MenuOther.szs to %08X (size=%u)\n",
                resource.name, resource.address, resource.size);
    }
    s_loaded = true;
    if (!s_resources[0].address) {
        reportFailure("decompressed MenuOther.szs but OptionBG.brctr was absent");
    }
    for (const auto& resource : s_resources) {
        if (strcasecmp(requestedName, resource.name) == 0 && resource.address) {
            if (outSize) *outSize = resource.size;
            return resource.address;
        }
    }
    return 0;
}

static uint32_t g_raceSzsBase = 0;
static uint32_t g_raceESzsBase = 0;

static void EnsureRaceArchivesLoaded() {
    if (g_raceSzsBase != 0) return;
    const char* racePaths[] = {
#ifdef __3DS__
        "sdmc:/mkwii/Scene/UI/Race.szs",
#endif
        "../../Assets/extracted-rmcp01/DATA/files/Scene/UI/Race.szs",
        "Assets/extracted-rmcp01/DATA/files/Scene/UI/Race.szs"
    };
    const char* raceEPaths[] = {
#ifdef __3DS__
        "sdmc:/mkwii/Scene/UI/Race_E.szs",
#endif
        "../../Assets/extracted-rmcp01/DATA/files/Scene/UI/Race_E.szs",
        "Assets/extracted-rmcp01/DATA/files/Scene/UI/Race_E.szs"
    };
    auto loadAndDecompress = [](const char* paths[], size_t numPaths, uint32_t guestDst, size_t maxDst) -> uint32_t {
        std::FILE* f = nullptr;
        for (size_t i = 0; i < numPaths; ++i) {
            f = std::fopen(paths[i], "rb");
            if (f) break;
        }
        if (!f) {
            AppLog("[RaceSZS] Failed to open %s\n", paths[0]);
            return 0;
        }
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 16) { std::fclose(f); return 0; }
        std::vector<uint8_t> compressed(sz);
        std::fread(compressed.data(), 1, sz, f);
        std::fclose(f);

        uint8_t* dstPtr = Memory::GetPointer(guestDst, maxDst);
        if (!dstPtr) return 0;

        const uint32_t expandSize = (static_cast<uint32_t>(compressed[4]) << 24) |
                                    (static_cast<uint32_t>(compressed[5]) << 16) |
                                    (static_cast<uint32_t>(compressed[6]) << 8) |
                                    static_cast<uint32_t>(compressed[7]);
        if (expandSize > maxDst) {
            AppLog("[RaceSZS] Decompressed size %u exceeds buffer %zu\n", expandSize, maxDst);
            return 0;
        }
        uint32_t srcIdx = 16;
        uint32_t dstIdx = 0;
        uint32_t mask = 0;
        uint8_t flags = 0;
        while (dstIdx < expandSize && srcIdx < compressed.size()) {
            if (mask == 0) {
                flags = compressed[srcIdx++];
                mask = 0x80;
            }
            if (flags & mask) {
                dstPtr[dstIdx++] = compressed[srcIdx++];
            } else {
                if (srcIdx + 1 >= compressed.size()) break;
                const uint8_t b1 = compressed[srcIdx++];
                const uint8_t b2 = compressed[srcIdx++];
                const uint32_t dist = ((static_cast<uint32_t>(b1 & 0x0F) << 8) | b2) + 1;
                uint32_t count = b1 >> 4;
                if (count == 0) {
                    if (srcIdx >= compressed.size()) break;
                    count = static_cast<uint32_t>(compressed[srcIdx++]) + 18;
                } else {
                    count += 2;
                }
                uint32_t copyIdx = dstIdx - dist;
                for (uint32_t i = 0; i < count && dstIdx < expandSize; ++i) {
                    dstPtr[dstIdx++] = dstPtr[copyIdx++];
                }
            }
            mask >>= 1;
        }
        AppLog("[RaceSZS] Loaded and decompressed %u bytes to 0x%08X\n", expandSize, guestDst);
        return guestDst;
    };

    constexpr size_t nRacePaths = sizeof(racePaths) / sizeof(racePaths[0]);
    constexpr size_t nRaceEPaths = sizeof(raceEPaths) / sizeof(raceEPaths[0]);
    // The PAL archives expand to 4,913,440 and 1,195,840 bytes.
    constexpr size_t raceCapacity = 0x004C0000u;
    g_raceSzsBase = loadAndDecompress(racePaths, nRacePaths, kRaceSzsBase, raceCapacity);
    g_raceESzsBase = loadAndDecompress(raceEPaths, nRaceEPaths, kRaceSzsBase + raceCapacity, kRaceSzsSize - raceCapacity);
}

struct CourseModelTrace {
    const char* filename;
    uint32_t archive = 0;
    uint32_t size = 0;
    std::set<uint32_t> objects;
};
static uint32_t g_tracedCourseU8 = 0;
// Every decompressed SZS image we have produced. Used as a last-resort lookup source for resource
// requests the guest's ArchiveMgr cannot satisfy yet (see [Model Rescue]).
static std::vector<uint32_t> g_decompressedArcs;
static CourseModelTrace g_courseModelTrace[] = {
    {"course_model.brres"}, {"vrcorn_model.brres"}, {"map_model.brres"},
};

static uint32_t ReadTraceWord(uint32_t address) {
    return address && Memory::Contains(address, 4) ? Memory::Read32(address) : 0;
}

static CourseModelTrace* FindTracedModel(uint32_t model) {
    for (auto& trace : g_courseModelTrace) {
        if (trace.archive && model >= trace.archive && model - trace.archive < trace.size)
            return &trace;
    }
    return nullptr;
}

static const CourseModelTrace* FindTracedModelFilename(uint32_t address) {
    if (address && Memory::Contains(address, 1) && Memory::Read8(address) == '/') ++address;
    for (const auto& trace : g_courseModelTrace) {
        const size_t length = std::strlen(trace.filename) + 1;
        if (address && Memory::Contains(address, length) &&
            std::memcmp(Memory::GetPointer(address, length), trace.filename, length) == 0) {
            return &trace;
        }
    }
    return nullptr;
}

extern "C" void MkwOnDirectCall(uint32_t target) {
    static std::map<uint32_t, uint32_t> s_counts;
    ++s_counts[target];
    static uint32_t s_last = 0;
    if (gx_bridge::g_gxFrame != s_last && gx_bridge::g_gxFrame >= 200) {
        s_last = gx_bridge::g_gxFrame;
        std::string out;
        for (const auto& kv : s_counts) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), " %08X=%u", kv.first, kv.second);
            out += buf;
        }
        AppLog("[DirectDraw] frame=%u%s\n", gx_bridge::g_gxFrame, out.c_str());
    }
}

// Scripted pad state.
//
// Historically the port auto-pulsed GameCube A+START from frame 20 to skip the race intro
// flyover.  That pulse also runs on the menu/title path, where it makes scripted navigation
// experiments impossible, so MKW_NO_AUTOPULSE=1 turns it off and MKW_PRESS_A=<frame> /
// MKW_PRESS_START=<frame> press a single button for a 3-frame window instead.
// Set by ArgumentParser::parse: true when the race shortcut forces section 0x20.  The menu path
// (false) lets the real PageManipulatorManager / ControlButtonInfo code run so pages react to
// the pad; the shortcut keeps the historical HLE stubs that the race boot needed.
static bool g_raceShortcut = false;

enum class ScriptPadKind { kGcPad, kClassic };

// Frame on which the GC pad's A bit is masked, so a *held* A becomes a fresh press edge.  The pad
// registration window opens only after the section transition finishes, and MKWii binds on the
// press edge alone, so an A press made while the title was still fading in would otherwise never
// register.  Set by the pad-registration nudge at endRender.
static uint32_t g_padEdgeMaskFrame = 0xFFFFFFFFu;
// Frames left to keep reporting A as held after the player pressed it while the pad was still
// unregistered.  The registration window only opens once the section transition finishes and the
// guest binds on the press *edge* alone, so an A press made the moment the title appears (a second
// or two earlier) would otherwise be thrown away and the title would ignore the pad for ever.
static uint32_t g_padHoldAUntilBound = 0;
static bool g_padAPressSeen = false;   // saw an A press while nothing was registered yet

static uint32_t ScriptedPadButtons(ScriptPadKind kind)
{
    const uint32_t frame = gx_bridge::g_gxFrame;
    const bool gc = (kind == ScriptPadKind::kGcPad);
    const uint32_t kA = gc ? 0x0100u : 0x0010u;
    const uint32_t kStart = gc ? 0x1000u : 0x0400u;

    // The A+START autopulse exists to skip the race intro on the forced race shortcut.  It must
    // never run on the menu path: a synthesised A every other frame walks the title -> main menu
    // -> options -> "Return to Wii Menu" dialog by itself, and looks like the port is stuck on a
    // white box.  MKW_NO_AUTOPULSE=1 still disables it for the race path.
    if (g_raceShortcut && std::getenv("MKW_NO_AUTOPULSE") == nullptr) {
        if (frame >= 20 && frame <= 100000 && (frame % 4) < 2) return kA | kStart;
        return 0;
    }
    uint32_t out = 0;
    // MKW_PRESS_LEN widens the scripted window (default 3 frames); a longer window guarantees a
    // fresh press edge for the guest's "press a button to connect your pad" binding logic, which
    // only samples the edge on the frames it happens to run.
    uint32_t len = 3;
    if (const char* s = std::getenv("MKW_PRESS_LEN")) {
        const long v = std::strtol(s, nullptr, 10);
        if (v > 0 && v < 100000) len = static_cast<uint32_t>(v);
    }
    if (const char* s = std::getenv("MKW_PRESS_A")) {
        const long at = std::strtol(s, nullptr, 10);
        if (at >= 0 && frame >= static_cast<uint32_t>(at) && frame < static_cast<uint32_t>(at) + len) out |= kA;
    }
    if (const char* s = std::getenv("MKW_PRESS_START")) {
        const long at = std::strtol(s, nullptr, 10);
        if (at >= 0 && frame >= static_cast<uint32_t>(at) && frame < static_cast<uint32_t>(at) + len) out |= kStart;
    }
    if (frame == g_padEdgeMaskFrame) out &= ~kA;   // one frame of "A released" -> fresh edge
    if (gc && g_padHoldAUntilBound > 0 && frame != g_padEdgeMaskFrame) {
        --g_padHoldAUntilBound;
        out |= kA;
    }
    return out;
}

bool handle_startup_hle(uint32_t target, CpuContext* cpu)
{
    g_hleRing[g_hleRingPos] = target;
    g_hleRingPos = (g_hleRingPos + 1) % 8;
    // Watch the menu globe object's visibility byte (*(0x809C1988)+32). It is 0 for the whole
    // natural boot, so the globe draw is skipped. Log the first transition and the call that
    // caused it, to find the state-machine edge that should show the globe.
    {
        static uint32_t s_lastGlobeFlag = 0xFFFFFFFFu;
        static uint32_t s_lastTarget = 0;
        static uint32_t s_lastLr = 0;
        const uint32_t globeObj = Memory::Read32(0x809C1988u);
        const uint32_t flag = (globeObj && Memory::Contains(globeObj + 32, 1))
            ? Memory::Read8(globeObj + 32) : 0xFFFFFFFFu;
        if (flag != s_lastGlobeFlag) {
            AppLog("[GlobeWatch] flag=%u prevTarget=%08X prevLr=%08X nowTarget=%08X nowLr=%08X obj=%08X frame=%u\n",
                flag, s_lastTarget, s_lastLr, target, cpu ? cpu->lr : 0, globeObj, gx_bridge::g_gxFrame);
            s_lastGlobeFlag = flag;
        }
        s_lastTarget = target;
        s_lastLr = cpu ? cpu->lr : 0;
    }
    // Watch the menu director's +04/+08/+0C/+10 slots (created by func_8077EF84). +08 is the
    // globe object; +04/+0C/+10 are the candidates for its missing drawable sub-objects.
    {
        static uint32_t s_lastDir[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        static uint32_t s_lastDirTarget = 0;
        static uint32_t s_lastDirLr = 0;
        const uint32_t dir = Memory::Read32(0x809C2DF0u);
        if (dir && Memory::Contains(dir + 16, 4)) {
            const uint32_t slots[4] = {
                Memory::Read32(dir + 4), Memory::Read32(dir + 8),
                Memory::Read32(dir + 12), Memory::Read32(dir + 16)
            };
            if ((slots[0] | slots[1] | slots[2] | slots[3]) != (s_lastDir[0] | s_lastDir[1] | s_lastDir[2] | s_lastDir[3])) {
                AppLog("[DirWatch] +04=%08X +08=%08X +0C=%08X +10=%08X prevTarget=%08X prevLr=%08X nowTarget=%08X nowLr=%08X frame=%u\n",
                    slots[0], slots[1], slots[2], slots[3], s_lastDirTarget, s_lastDirLr,
                    target, cpu ? cpu->lr : 0, gx_bridge::g_gxFrame);
                for (int i = 0; i < 4; ++i) s_lastDir[i] = slots[i];
            }
        }
        s_lastDirTarget = target;
        s_lastDirLr = cpu ? cpu->lr : 0;
    }
    // Stack-depth guard. Each recompiled guest frame consumes ~4 KB of host stack (0xF20 in the
    // prologue), so a guest loop that recurses a few thousand deep overflows the stack and dies
    // with an opaque SIGSEGV in ___chkstk_darwin. Track high-water usage and log the cycle that
    // drives it instead of crashing.
    {
        static uintptr_t s_stackBase = 0;
        static bool s_reported = false;
        uint8_t probe;
        const uintptr_t sp = reinterpret_cast<uintptr_t>(&probe);
        if (s_stackBase == 0 || sp > s_stackBase) s_stackBase = sp;
        const uintptr_t used = s_stackBase - sp;
        static uintptr_t s_maxUsed = 0;
        if (used > s_maxUsed) s_maxUsed = used;
        static uint32_t s_stackPeakLogFrame = 0;
        if (gx_bridge::g_gxFrame >= 84 && gx_bridge::g_gxFrame <= 120 &&
            gx_bridge::g_gxFrame % 4 == 0 && s_stackPeakLogFrame != gx_bridge::g_gxFrame) {
            s_stackPeakLogFrame = gx_bridge::g_gxFrame;
            AppLog("[StackPeak] frame=%u used=%zuKB\n",
                gx_bridge::g_gxFrame, static_cast<size_t>(s_maxUsed / 1024));
        }
        if (used > 5u * 1024u * 1024u && !s_reported) {
            s_reported = true;
            AppLog("[StackGuard] used=%zuKB target=%08X lr=%08X pc=%08X frame=%u\n",
                static_cast<size_t>(used / 1024), target,
                cpu ? cpu->lr : 0, cpu ? cpu->pc : 0, gx_bridge::g_gxFrame);
            std::fflush(stdout);
        }
        if (used > 7u * 1024u * 1024u) {
            static uint32_t s_dumpCount = 0;
            if (s_dumpCount < 40) {
                ++s_dumpCount;
                AppLog("[StackGuard] refusing deeper call target=%08X lr=%08X\n",
                    target, cpu ? cpu->lr : 0);
            }
            if (cpu) cpu->gpr[3] = 0;
            return true;
        }
    }
    // First-appearance filter for the archive-load window. The ArchiveMgr entries flip 0 -> 4
    // between frames ~84 and ~100, but no call that receives an archive pointer happens after
    // frame 84 and no task job runs in that window. So build a baseline of everything called at
    // frames 86-89 and then report only targets that are NEW at frames 90-100: that leaves the
    // code that starts running only once the load completes.
    if (gx_bridge::g_gxFrame >= 86 && gx_bridge::g_gxFrame <= 100) {
        static std::set<uint32_t> s_baseline;
        static std::set<uint64_t> s_reportedNew;
        if (gx_bridge::g_gxFrame <= 89) {
            s_baseline.insert(target);
        } else if (s_baseline.find(target) == s_baseline.end()) {
            const uint64_t k = (static_cast<uint64_t>(gx_bridge::g_gxFrame) << 32) | target;
            if (s_reportedNew.size() < 200 && s_reportedNew.insert(k).second) {
                AppLog("[ArcNew] frame=%u target=%08X lr=%08X r3=%08X r4=%08X\n",
                    gx_bridge::g_gxFrame, target, cpu ? cpu->lr : 0,
                    cpu ? cpu->gpr[3] : 0, cpu ? cpu->gpr[4] : 0);
            }
        }
    }
    // Camera watch: who touches RaceCameraMgr (*(0x809C1998)) or its embedded camera container
    // (mgr+16)? The container is empty, so the function that should register the kart cameras
    // either never runs or runs without the manager.
    if (cpu && gx_bridge::g_gxFrame >= 84 && gx_bridge::g_gxFrame <= 400) {
        const uint32_t camMgr = Memory::Read32(0x809C1998u);
        if (camMgr) {
            static std::set<uint32_t> s_seenCamTargets;
            bool hit = false;
            for (int r = 3; r <= 6 && !hit; ++r) {
                const uint32_t v = cpu->gpr[r];
                if (v == camMgr || v == camMgr + 16u) hit = true;
            }
            if (hit && s_seenCamTargets.size() < 48 && s_seenCamTargets.insert(target).second) {
                AppLog("[CamWatch] frame=%u target=%08X lr=%08X r3=%08X r4=%08X count=%u\n",
                    gx_bridge::g_gxFrame, target, cpu->lr, cpu->gpr[3], cpu->gpr[4],
                    Memory::Contains(camMgr + 32u, 4) ? Memory::Read32(camMgr + 32u) : 0xFFFFFFFFu);
            }
        }
    }
    // Non-intercepting trace: which guest functions are handed an ArchiveMgr object or entry
    // pointer while the archive groups are still loading? The one that advances entry state
    // 0/1 -> 4 is the per-frame pump we need to drive synchronously.
    if (gx_bridge::g_watchArcEntries && gx_bridge::g_watchArcCount &&
        gx_bridge::g_gxFrame >= 80 && gx_bridge::g_gxFrame <= 130) {
        const uint32_t lo = gx_bridge::g_watchArcEntries;
        const uint32_t hi = lo + gx_bridge::g_watchArcCount * 36u;
        auto watched = [&](uint32_t v) {
            if (v >= lo && v < hi) return true;
            if (gx_bridge::g_tracedArchiveMgr && v == gx_bridge::g_tracedArchiveMgr) return true;
            for (uint32_t i = 0; i < gx_bridge::g_watchArcN && i < 8; ++i) {
                if (gx_bridge::g_watchArcs[i] && v == gx_bridge::g_watchArcs[i]) return true;
            }
            return false;
        };
        bool hit = false;
        int hitReg = -1;
        if (cpu) {
            for (int r = 3; r <= 10; ++r) {
                if (watched(cpu->gpr[r])) { hit = true; hitReg = r; break; }
            }
        }
        if (hit) {
            static std::set<uint64_t> s_seenEntryTargets;
            const uint64_t key = (static_cast<uint64_t>(gx_bridge::g_gxFrame) << 32) | target;
            if (s_seenEntryTargets.size() < 400 && s_seenEntryTargets.insert(key).second) {
                AppLog("[ArcWatch] target=%08X frame=%u reg=r%d lr=%08X\n",
                    target, gx_bridge::g_gxFrame, hitReg, cpu->lr);
            }
        }
    }
    if (target == 0x8061C96Cu && cpu && cpu->lr == 0x80601B40u) {
        const uint32_t page = cpu->gpr[3];
        const uint32_t vtable = ReadTraceWord(page);
        AppLog("[Race Page Init] frame=%u page=%08X vtable=%08X slot12=%08X guestWord=%08X\n",
            gx_bridge::g_gxFrame, page, vtable,
            vtable ? ReadTraceWord(vtable + 48) : 0, ReadTraceWord(target));
    }
    if (target == 0x80634C90u) {
        AppLog("[SectionMgr] 0x80634C90 (create) called! lr=%08X\n", cpu ? cpu->lr : 0);
    }
    if (target == 0x80634D40u) {
        AppLog("[SectionMgr] 0x80634D40 (ctor) called! this=%08X lr=%08X\n", cpu ? cpu->gpr[3] : 0, cpu ? cpu->lr : 0);
    }
    if (target == 0x80634E44u) {
        AppLog("[SectionMgr] 0x80634E44 (init) called! this=%08X lr=%08X\n", cpu ? cpu->gpr[3] : 0, cpu ? cpu->lr : 0);
    }
    if (target == 0x800841B0u && cpu) {
        const uint32_t resAcc = cpu->gpr[3];
        const uint32_t type = cpu->gpr[4];
        const uint32_t namePtr = cpu->gpr[5];
        const uint32_t sizeOut = cpu->gpr[6];
        const char* name = (namePtr && Memory::Contains(namePtr, 1)) ? reinterpret_cast<const char*>(Memory::GetPointer(namePtr, 1)) : "";
        if (name && (std::strstr(name, "course") || std::strstr(name, "kcl"))) {
            AppLog("[ResourceAccessor] Looking for '%s' type=%u resAcc=%08X lr=%08X\n", name, type, resAcc, cpu->lr);
        }

        func_800841B0(cpu);
        if (gx_bridge::g_gxFrame >= 80 && gx_bridge::g_gxFrame <= 95) {
            static uint32_t s_accessorLogs = 0;
            if (s_accessorLogs < 60 && name && name[0]) {
                ++s_accessorLogs;
                AppLog("[Accessor] frame=%u '%s' type=%u -> %08X lr=%08X\n",
                    gx_bridge::g_gxFrame, name, type, cpu->gpr[3], cpu->lr);
            }
        }
        if (cpu->gpr[3] != 0) return true;

        if (resAcc && Memory::Contains(resAcc, 16)) {
            uint32_t node = Memory::Read32(resAcc + 8);
            const uint32_t endNode = resAcc + 8;
            int count = 0;
            while (node != 0 && node != endNode && count++ < 16 && Memory::Contains(node, 64)) {
                const uint32_t arcData = Memory::Read32(node + 8);
                uint32_t foundSize = 0;
                uint32_t found = FindU8Resource(arcData, name, &foundSize);
                if (found != 0) {
                    cpu->gpr[3] = found;
                    if (sizeOut && Memory::Contains(sizeOut, 4)) {
                        Memory::Write32(sizeOut, foundSize);
                    }
                    AppLog("[U8 GetResource HLE] Found '%s' in arc %08X at %08X (size=%u)\n",
                        name, arcData, found, foundSize);
                    return true;
                }
                node = Memory::Read32(node);
            }
        }
        // Fallback to preloaded Race.szs and Race_E.szs
        EnsureRaceArchivesLoaded();
        for (uint32_t arcData : {g_raceSzsBase, g_raceESzsBase}) {
            if (arcData != 0) {
                uint32_t foundSize = 0;
                uint32_t found = FindU8Resource(arcData, name, &foundSize);
                if (found != 0) {
                    cpu->gpr[3] = found;
                    if (sizeOut && Memory::Contains(sizeOut, 4)) {
                        Memory::Write32(sizeOut, foundSize);
                    }
                    AppLog("[U8 GetResource HLE] Fallback found '%s' in race arc %08X at %08X (size=%u)\n",
                        name, arcData, found, foundSize);
                    return true;
                }
            }
        }
        // Menu archives are loaded asynchronously by the game, and some menu resource groups
        // are not mounted yet when their first lookup occurs. Search only the decompressed U8
        // images already owned by the guest; this returns the archive's original resource bytes
        // and avoids copying or allocating memory on the 3DS.
        for (uint32_t arcData : g_decompressedArcs) {
            if (!arcData || !Memory::Contains(arcData, 4)) continue;
            uint32_t foundSize = 0;
            const uint32_t found = FindU8Resource(arcData, name, &foundSize);
            if (found != 0) {
                cpu->gpr[3] = found;
                if (sizeOut && Memory::Contains(sizeOut, 4)) {
                    Memory::Write32(sizeOut, foundSize);
                }
                static std::set<std::string> s_seenUiResourceFallbacks;
                const std::string key = std::string(name) + "|" + std::to_string(arcData);
                if (s_seenUiResourceFallbacks.size() < 128 && s_seenUiResourceFallbacks.insert(key).second) {
                    AppLog("[U8 GetResource HLE] Decompressed-archive fallback found '%s' in %08X at %08X (size=%u)\n",
                        name, arcData, found, foundSize);
                }
                return true;
            }
        }
        if (strcasecmp(name, "OptionBG.brctr") == 0 ||
            strcasecmp(name, "option_bg.brlyt") == 0 ||
            strcasecmp(name, "option_bg_Loop.brlan") == 0) {
            uint32_t foundSize = 0;
            const uint32_t found = LoadMenuOtherResource(name, &foundSize);
            if (found) {
                cpu->gpr[3] = found;
                if (sizeOut && Memory::Contains(sizeOut, 4)) Memory::Write32(sizeOut, foundSize);
                return true;
            }
        }
        AppLog("[U8 GetResource HLE] Not found: '%s' (type=%08X)\n", name, type);
        return true;
    }
    if (target == 0x8020F768u) {
        AppLog("[EGG::Archive::Mount] data=%08X heap=%08X align=%d (magic=%08X)\n",
            cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
            (cpu->gpr[3] && Memory::Contains(cpu->gpr[3], 4)) ? Memory::Read32(cpu->gpr[3]) : 0);
        const uint32_t u8 = cpu->gpr[3];
        if (!g_courseModelTrace[0].archive && ReadTraceWord(u8) == 0x55AA382Du) {
            uint32_t courseSize = 0;
            const uint32_t course = FindU8Resource(u8, g_courseModelTrace[0].filename, &courseSize);
            if (ReadTraceWord(course) == 0x62726573u) {
                g_tracedCourseU8 = u8;
                for (auto& trace : g_courseModelTrace) {
                    trace.archive = FindU8Resource(u8, trace.filename, &trace.size);
                    AppLog("[Race Model] U8=%08X %s brres=%08X size=%u\n",
                        u8, trace.filename, trace.archive, trace.size);
                }
            }
        }
        return false;
    }
    if (target == 0x80124500u) {
        AppLog("[ARC::InitHandle] arcData=%08X handle=%08X (magic=%08X) lr=%08X r4=%08X r5=%08X r6=%08X\n",
            cpu->gpr[3], cpu->gpr[4],
            (cpu->gpr[3] && Memory::Contains(cpu->gpr[3], 4)) ? Memory::Read32(cpu->gpr[3]) : 0,
            cpu->lr, cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
        return false;
    }
    if (cpu && (cpu->lr == 0x805C2CD4u || cpu->lr == 0x805E88F4u || cpu->lr == 0x805E8910u || cpu->lr == 0x805E8958u)) {
        {
            const uint32_t namePtr = cpu->gpr[5];
            const char* resName = (namePtr && Memory::Contains(namePtr, 1))
                ? reinterpret_cast<const char*>(Memory::GetPointer(namePtr, 1)) : "";
            AppLog("[ControlLoader Trace] lr=%08X target=%08X r3=%08X r4=%08X r5=%08X name='%s'\n",
                cpu->lr, target, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], resName);
        }
        if ((cpu->lr == 0x805C2CD4u || cpu->lr == 0x805E88F4u) && cpu->gpr[5] && Memory::Contains(cpu->gpr[5], 32)) {
            AppLog("  GetResource name='%s'\n", reinterpret_cast<const char*>(Memory::GetPointer(cpu->gpr[5], 1)));
            const uint32_t resAcc = cpu->gpr[3];
            if (resAcc && Memory::Contains(resAcc, 16)) {
                uint32_t node = Memory::Read32(resAcc + 8);
                const uint32_t endNode = resAcc + 8;
                int count = 0;
                while (node != 0 && node != endNode && count++ < 10 && Memory::Contains(node, 64)) {
                    const uint32_t prefixPtr = node + 36;
                    std::string prefix = Memory::Contains(prefixPtr, 1) ? reinterpret_cast<const char*>(Memory::GetPointer(prefixPtr, 1)) : "(unmapped)";
                    AppLog("    ArcNode[%d]=%08X arcHandle=%08X prefix='%s'\n", count, node, Memory::Read32(node + 8), prefix.c_str());
                    node = Memory::Read32(node);
                }
            }
        }
    }
    if (cpu && (cpu->lr == 0x806FEAE0u || cpu->lr == 0x806FEB28u)) {
        static uint32_t s_sndTableAlloc = 0x81520000u;
        uint32_t size = cpu->gpr[4];
        if (size < 32) size = 32;
        s_sndTableAlloc = (s_sndTableAlloc + 31) & ~31;
        cpu->gpr[3] = s_sndTableAlloc;
        s_sndTableAlloc += size;
        AppLog("[SoundHeap] Fallback alloc %u bytes at 0x%08X for lr=%08X (target=%08X)\n",
            size, cpu->gpr[3], cpu->lr, target);
        return true;
    }
    if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame && cpu &&
        (cpu->lr == 0x80070B1Cu || cpu->lr == 0x80070BACu)) {
        AppLog("[GX Scene Object] lr=%08X proc=%08X object=%08X operation=%u\n",
            cpu->lr, target, cpu->gpr[3], cpu->gpr[4]);
    }
    if (target == 0) {
        if (cpu && cpu->lr == 0x8055BE74u) {
            static unsigned nullModelCalls = 0;
            if (nullModelCalls++ < 12) {
                const uint32_t director = cpu->gpr[27];
                AppLog("[Race Model] InitG3D null dispatch director=%08X mdl=%08X object=%08X vtable=%08X\n",
                    director, director ? ReadTraceWord(director + 12) : 0, cpu->gpr[3], ReadTraceWord(cpu->gpr[3]));
            }
        }
        if (cpu && (cpu->lr == 0x805E951Cu || cpu->lr == 0x805E95E0u || cpu->lr == 0x805E96A4u)) {
            const uint32_t obj = cpu->gpr[3];
            const uint32_t vt = (obj && Memory::Contains(obj, 4)) ? Memory::Read32(obj) : 0;
            static int s_rttiPrints = 0;
            if (++s_rttiPrints <= 5) {
                AppLog("[Pane RTTI] obj=%08X vt=%08X lr=%08X\n", obj, vt, cpu->lr);
            }
            return true;
        }
        static std::set<uint32_t> s_seenNullLrs;
        if (cpu && s_seenNullLrs.insert(cpu->lr).second) {
            AppLog("[HLE] Ignored indirect call to NULL at lr=%08X (unique)\n", cpu->lr);
        }
        return true;
    }
    if (target < 0x80000000u) {
        static std::set<uint32_t> s_seenInvalidTargets;
        if (s_seenInvalidTargets.insert(target).second) {
            AppLog("[HLE] Ignored invalid code address target %08X at lr=%08X\n", target, cpu ? cpu->lr : 0);
        }
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }
    if (cpu && cpu->lr >= 0x805BB200u && cpu->lr < 0x805BB400u) {
        static std::set<uint32_t> s_seenSectionDispatch;
        if (s_seenSectionDispatch.size() < 16 && s_seenSectionDispatch.insert(target).second) {
            AppLog("[SectionDispatch] target=%08X lr=%08X this=%08X frame=%u\n",
                target, cpu->lr, cpu->gpr[3], gx_bridge::g_gxFrame);
        }
    }
    if (cpu && cpu->lr == 0x80830784u) {
        static std::set<uint32_t> s_seenGlobeEntry;
        const uint32_t key = target;
        if (s_seenGlobeEntry.size() < 16 && s_seenGlobeEntry.insert(key).second) {
            AppLog("[GlobeEntry] target=%08X entry=%08X frame=%u\n",
                target, cpu->gpr[3], gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x8059E1FCu) {
        static std::set<uint32_t> s_seenE1FC;
        const uint32_t obj = cpu ? cpu->gpr[3] : 0;
        if (s_seenE1FC.size() < 8 && s_seenE1FC.insert(obj).second) {
            AppLog("[GlobeObj] obj=%08X vt=%08X b32=%u p4=%08X b9=%u p20=%08X p24=%08X p28=%08X frame=%u\n",
                obj, obj ? Memory::Read32(obj) : 0, obj ? Memory::Read8(obj + 32) : 0, obj ? Memory::Read32(obj + 4) : 0,
                obj ? Memory::Read8(obj + 9) : 0, obj ? Memory::Read32(obj + 20) : 0,
                obj ? Memory::Read32(obj + 24) : 0, obj ? Memory::Read32(obj + 28) : 0,
                gx_bridge::g_gxFrame);
            const uint32_t list = obj ? Memory::Read32(obj + 4) : 0;
            if (list && Memory::Contains(list, 40)) {
                const uint32_t e0 = list;
                AppLog("[GlobeEntry0] e0=%08X vt=%08X p12=%08X p16=%08X\n",
                    e0, Memory::Read32(e0), Memory::Read32(e0 + 12), Memory::Read32(e0 + 16));
            }
        }
    }
    if (target == 0x80555264u || target == 0x8055472Cu || target == 0x8051BD34u || target == 0x8059E1FCu) {
        static std::set<uint32_t> s_seenGlobePath;
        if (s_seenGlobePath.size() < 16 && s_seenGlobePath.insert(target).second) {
            AppLog("[GlobePath] target=%08X lr=%08X this=%08X frame=%u\n",
                target, cpu ? cpu->lr : 0, cpu ? cpu->gpr[3] : 0, gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x8051BC78u) {
        static std::set<uint32_t> s_seenSceneDraw;
        const uint32_t obj = cpu ? cpu->gpr[3] : 0;
        if (s_seenSceneDraw.size() < 8 && s_seenSceneDraw.insert(obj).second) {
            AppLog("[SceneDrawObj] obj=%08X p9516=%08X b9520=%u b9521=%u frame=%u\n",
                obj, obj ? Memory::Read32(obj + 9516) : 0,
                obj ? Memory::Read8(obj + 9520) : 0, obj ? Memory::Read8(obj + 9521) : 0,
                gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x80214C74u) {
        static std::set<uint32_t> s_seenGlobeDraw;
        const uint32_t obj = cpu ? cpu->gpr[3] : 0;
        if (s_seenGlobeDraw.size() < 8 && s_seenGlobeDraw.insert(obj).second) {
            AppLog("[GlobeDraw] obj=%08X r4=%08X r5=%08X r6=%08X r7=%08X frame=%u\n",
                obj, cpu ? cpu->gpr[4] : 0, cpu ? cpu->gpr[5] : 0,
                cpu ? cpu->gpr[6] : 0, cpu ? cpu->gpr[7] : 0, gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x805A1A18u) {
        static std::set<uint32_t> s_seenA18;
        const uint32_t obj = cpu ? cpu->gpr[3] : 0;
        const uint32_t vt = (obj && Memory::Contains(obj, 4)) ? Memory::Read32(obj) : 0;
        const uint32_t draw20 = (vt && Memory::Contains(vt + 20, 4)) ? Memory::Read32(vt + 20) : 0;
        const uint32_t key = (obj << 8) ^ (draw20 & 0xFFu);
        if (s_seenA18.size() < 16 && s_seenA18.insert(key).second) {
            AppLog("[EarthA18] obj=%08X vt=%08X draw20=%08X r4=%u frame=%u\n",
                obj, vt, draw20, cpu ? cpu->gpr[4] : 0, gx_bridge::g_gxFrame);
        }
    }
    if (cpu && (cpu->lr == 0x805A1B2Cu || cpu->lr == 0x805A1B60u)) {
        static std::set<uint32_t> s_seenEarth;
        const uint32_t key = (cpu->lr << 16) ^ (target & 0xFFFFu);
        if (s_seenEarth.size() < 12 && s_seenEarth.insert(key).second) {
            AppLog("[EarthDraw] lr=%08X target=%08X this=%08X frame=%u\n",
                cpu->lr, target, cpu->gpr[3], gx_bridge::g_gxFrame);
        }
    }
    {
        static const uint32_t kLifecycle[] = {
            0x805BD3A8u, 0x805BD518u, 0x805BD720u, 0x805BDBE0u,
            0x8063CE6Cu, 0x8063D004u, 0x8063DAC0u, 0x8063D954u,
            0x8063D278u, 0x8063D268u,
            0x808504E4u, // Pages::MainMenu::CreateExternalControl
            0x80850580u, // Pages::MainMenu::CreateControl
            0x8060245Cu, // Page::InitControlGroup
            0x8060246Cu, // Page::AddControl
            0x805C2868u, // ControlGroup::InitControls
            0x80601D24u, // Page::Init
            0x80836B9Cu, // Pages::Menu::OnInit
            0x8083752Cu, // Pages::Menu::SelectButton
            0x805C2620u, // ControlGroup::Init(Page)
        };
        for (uint32_t lc : kLifecycle) {
            if (target == lc) {
                static std::set<uint64_t> s_seenLife;
                const uint32_t th = cpu ? cpu->gpr[3] : 0;
                const uint64_t lk = (static_cast<uint64_t>(target) << 32) | th;
                if (s_seenLife.size() < 400 && s_seenLife.insert(lk).second) {
                    AppLog("[Lifecycle] tgt=%08X this=%08X r4=%08X r5=%08X p152=%08X lr=%08X frame=%u\n",
                        target, th, cpu ? cpu->gpr[4] : 0, cpu ? cpu->gpr[5] : 0,
                        (th && Memory::Contains(th + 152, 4)) ? Memory::Read32(th + 152) : 0,
                        cpu ? cpu->lr : 0, gx_bridge::g_gxFrame);
                }
                break;
            }
        }
    }
    if (target == 0x805BDAF0u) {
        static int s_seenBtn = 0;
        if (cpu && s_seenBtn < 12) {
            ++s_seenBtn;
            const uint32_t th = cpu->gpr[3];
            const uint32_t arr = (th && Memory::Contains(th + 152, 4)) ? Memory::Read32(th + 152) : 0;
            AppLog("[BtnAnim] this=%08X vt=%08X slot=%u arr=%08X lr=%08X frame=%u\n", th,
                (th && Memory::Contains(th, 4)) ? Memory::Read32(th) : 0, cpu->gpr[4], arr,
                cpu->lr, gx_bridge::g_gxFrame);
            if (arr && Memory::Contains(arr, 0x120)) {
                for (int e = 0; e < 4; ++e) {
                    const uint32_t o = arr + e * 68;
                    AppLog("  elem%d=%08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", e, o,
                        Memory::Read32(o), Memory::Read32(o + 4), Memory::Read32(o + 8), Memory::Read32(o + 12),
                        Memory::Read32(o + 16), Memory::Read32(o + 20), Memory::Read32(o + 24), Memory::Read32(o + 28));
                }
            }
        }
    }
    if (target == 0x8063CBE4u && cpu) { // AnimationGroup::Update
        const uint32_t g = cpu->gpr[3];
        static int s_seenGrp = 0;
        static bool s_reportedBad = false;
        const uint32_t tbl = (g && Memory::Contains(g, 4)) ? Memory::Read32(g) : 0;
        const uint32_t cnt = (g && Memory::Contains(g + 4, 4)) ? Memory::Read32(g + 4) : 0;
        const uint32_t idx = (g && Memory::Contains(g + 56, 4)) ? Memory::Read32(g + 56) : 0;
        const uint32_t slotPtr = (tbl && Memory::Contains(tbl + idx * 16, 4)) ? (tbl + idx * 16) : 0;
        const uint32_t tr = slotPtr ? Memory::Read32(slotPtr) : 0;
        const bool bad = (idx != 0xFFFFFFFFu) && (!slotPtr || !tr || !Memory::Contains(tr, 20));
        if (bad && !s_reportedBad) {
            s_reportedBad = true;
            AppLog("[AnimGrpBad] g=%08X tbl=%08X cnt=%u idx=%u slot=%08X tr=%08X b53=%u f60=%g lr=%08X frame=%u\n",
                g, tbl, cnt, idx, slotPtr, tr,
                (g && Memory::Contains(g + 53, 1)) ? Memory::Read8(g + 53) : 0,
                (g && Memory::Contains(g + 60, 4)) ? static_cast<float>(Memory::Read32(g + 60)) : 0.0f,
                cpu->lr, gx_bridge::g_gxFrame);
            if (tbl && Memory::Contains(tbl, 4)) {
                for (int b = 0; b < 16; ++b) {
                    AppLog("   tbl[%d]=%08X\n", b,
                        Memory::Contains(tbl + b * 16, 4) ? Memory::Read32(tbl + b * 16) : 0);
                }
            }
            if (g && Memory::Contains(g, 64)) {
                for (int b = 0; b < 64; b += 16) {
                    AppLog("   grp+%02X: %08X %08X %08X %08X\n", b,
                        Memory::Read32(g + b), Memory::Read32(g + b + 4),
                        Memory::Read32(g + b + 8), Memory::Read32(g + b + 12));
                }
            }
        }
        if (s_seenGrp < 40) {
            const uint64_t k = (static_cast<uint64_t>(g) << 32) ^ idx;
            static std::set<uint64_t> s_grpSeen;
            if (s_grpSeen.insert(k).second) {
                ++s_seenGrp;
                AppLog("[AnimGrp] g=%08X tbl=%08X cnt=%u idx=%u tr=%08X lr=%08X frame=%u\n",
                    g, tbl, cnt, idx, tr, cpu->lr, gx_bridge::g_gxFrame);
            }
        }
    }
    if (target == 0x80622D08u || target == 0x80622DA0u || target == 0x80623228u ||
        target == 0x80623288u || target == 0x80623390u) {
        static std::set<uint64_t> s_seenSecPage;
        const uint32_t key = (static_cast<uint64_t>(target) << 32) ^
            (static_cast<uint64_t>(cpu ? cpu->gpr[3] : 0) << 8) ^ (cpu ? cpu->gpr[4] : 0);
        if (s_seenSecPage.size() < 40 && s_seenSecPage.insert(key).second) {
            AppLog("[SecPage] tgt=%08X this=%08X r4=%08X r5=%08X lr=%08X frame=%u\n",
                target, cpu ? cpu->gpr[3] : 0, cpu ? cpu->gpr[4] : 0, cpu ? cpu->gpr[5] : 0,
                cpu ? cpu->lr : 0, gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x8085151Cu || target == 0x808515A0u || target == 0x808516ACu ||
        target == 0x805EFE40u || target == 0x805EEF20u || target == 0x805EEC58u ||
        target == 0x8062B240u || target == 0x80626078u || target == 0x806261E8u ||
        target == 0x8063AD64u || target == 0x8063B698u || target == 0x8063C210u ||
        target == 0x80850DB0u || target == 0x808503DCu || target == 0x80850D54u ||
        target == 0x8063AFC8u || target == 0x8063C398u || target == 0x8063AE94u ||
        target == 0x8063C27Cu || target == 0x8063B7F0u) {
        static std::set<uint32_t> s_seenAction;
        if (s_seenAction.size() < 24 && s_seenAction.insert(target).second) {
            AppLog("[MenuAction] target=%08X this=%08X r4=%08X r5=%08X lr=%08X frame=%u\n",
                target, cpu ? cpu->gpr[3] : 0, cpu ? cpu->gpr[4] : 0, cpu ? cpu->gpr[5] : 0,
                cpu ? cpu->lr : 0, gx_bridge::g_gxFrame);
        }
    }
    switch (target) {
    case 0x8061B6ECu: case 0x8061BCC8u: case 0x8061B490u: case 0x8061AF20u:
    case 0x8061B3CCu: case 0x8061B398u: case 0x8061B9A0u: case 0x8061B338u:
    case 0x80524264u: case 0x8061B7C4u: case 0x8061BA48u: case 0x8061BFFCu:
    case 0x805238F0u: case 0x805237E8u: case 0x80523520u: case 0x80523690u:
    case 0x8051FC84u: case 0x805201B0u: case 0x80522690u: case 0x80524628u:
    case 0x80524788u: case 0x80523724u: case 0x8051F308u: case 0x8051F22Cu:
    case 0x80520118u: case 0x80521110u: case 0x80521768u: case 0x805242D8u:
    case 0x80523EBCu: case 0x80521554u:
    {
        static std::set<uint64_t> s_padSeen;
        static const char* const kPadNames[] = {
            "SectionPad::RequestPad", "SectionPad::ResetAllPads", "SectionPad::SetID",
            "SectionPad::Update", "SectionPad::HasAController", "SectionPad::GetControllerHolder",
            "SectionPad::ResetPadsStatus", "SectionPad::ResetPadsStatusAndSet0x54",
            "In::Manager::ResetRealController", "SectionPad::ResetPad", "SectionPad::HasControllerChanged",
            "Pages::RecognizePad::__ct", "In::Manager::Update", "In::Manager::UpdateControllers",
            "In::Manager::Init", "In::Manager::Prepare", "In::WiiController::UpdateImpl",
            "In::GCNController::UpdateImpl", "In::Manager::ResetKPAD", "In::Manager::CopyPADStatus",
            "In::Manager::SyncWiimotes", "In::Manager::ResetSelfWiiAndGCN", "In::Controller::Init",
            "In::WiiController::Init", "In::GCNController::Init", "In::Holder::Init",
            "In::RealHolder::Update", "In::Manager::InitControllerHolders",
            "In::Manager::TrySetController", "In::RealHolder::SetController"};
        static const uint32_t kPadAddrs[] = {
            0x8061B6ECu, 0x8061BCC8u, 0x8061B490u, 0x8061AF20u, 0x8061B3CCu, 0x8061B398u,
            0x8061B9A0u, 0x8061B338u, 0x80524264u, 0x8061B7C4u, 0x8061BA48u, 0x8061BFFCu,
            0x805238F0u, 0x805237E8u, 0x80523520u, 0x80523690u, 0x8051FC84u, 0x805201B0u,
            0x80522690u, 0x80524628u, 0x80524788u, 0x80523724u, 0x8051F308u, 0x8051F22Cu,
            0x80520118u, 0x80521110u, 0x80521768u, 0x805242D8u, 0x80523EBCu, 0x80521554u};
        const char* nm = "?";
        for (int i = 0; i < 30; ++i) if (kPadAddrs[i] == target) nm = kPadNames[i];
        {
            static uint32_t s_padCalls[30] = {0};
            for (int i = 0; i < 30; ++i) if (kPadAddrs[i] == target) { ++s_padCalls[i]; break; }
            g_padCallCounts = &s_padCalls[0];
        }
        const uint32_t ths = cpu ? cpu->gpr[3] : 0;
        const uint64_t key = (static_cast<uint64_t>(target) << 32) ^ (static_cast<uint64_t>(ths) << 16) ^
            (cpu ? cpu->gpr[4] : 0) ^ (static_cast<uint64_t>(cpu ? cpu->lr : 0) << 40);
        if (s_padSeen.size() < 60 && s_padSeen.insert(key).second) {
            AppLog("[Pad] %s(%08X) this=%08X r4=%08X r5=%08X lr=%08X frame=%u\n", nm, target,
                ths, cpu ? cpu->gpr[4] : 0, cpu ? cpu->gpr[5] : 0, cpu ? cpu->lr : 0,
                gx_bridge::g_gxFrame);
        }
        break;
    }
    default: break;
    }
    if (target == 0x80623390u || target == 0x80601A60u) { // Section::CreatePageById / Page::Init
        static std::set<uint64_t> s_seenPage;
        const uint32_t key = (static_cast<uint64_t>(target) << 32) |
            (target == 0x80623390u ? (cpu ? cpu->gpr[3] : 0) : (cpu ? cpu->gpr[3] : 0));
        if (s_seenPage.size() < 60 && s_seenPage.insert(key).second) {
            AppLog("[PageFlow] %s id=%u this=%08X lr=%08X frame=%u\n",
                target == 0x80623390u ? "CreatePageById" : "Page::Init",
                target == 0x80623390u ? (cpu ? cpu->gpr[3] : 0) : (cpu ? cpu->gpr[4] : 0),
                cpu ? cpu->gpr[3] : 0, cpu ? cpu->lr : 0, gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x80198CA8u || target == 0x80198D88u) {
        static int s_heapLogs = 0;
        const bool bigAlloc = (target == 0x80198D88u && cpu && cpu->gpr[4] >= 4194304u);
        if (cpu && s_heapLogs < 40 && (target == 0x80198CA8u || bigAlloc)) {
            ++s_heapLogs;
            if (target == 0x80198CA8u) {
                AppLog("[HeapCreate] start=%08X size=%u attr=%08X frame=%u\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], gx_bridge::g_gxFrame);
            } else {
                AppLog("[HeapAlloc] heap=%08X size=%u align=%u frame=%u\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], gx_bridge::g_gxFrame);
            }
        }
    }
    if (target == 0x8063C91Cu || target == 0x805C4380u || target == 0x8063C820u) {
        static std::set<uint64_t> s_seenAnimPlay;
        if (cpu) {
            const uint32_t obj = cpu->gpr[3];
            const uint64_t key = (static_cast<uint64_t>(target) << 32) | obj;
            if (s_seenAnimPlay.size() < 24 && s_seenAnimPlay.insert(key).second) {
                const uint32_t tbl = (obj && Memory::Contains(obj, 4)) ? Memory::Read32(obj) : 0;
                AppLog("[AnimPlay] tgt=%08X lr=%08X this=%08X idx=%u tbl=%08X lst=%08X cur=%08X b34=%u b35=%u f1=%g frame=%u\n",
                    target, cpu->lr, obj, cpu->gpr[4], tbl,
                    (obj && Memory::Contains(obj + 24, 4)) ? Memory::Read32(obj + 24) : 0,
                    (obj && Memory::Contains(obj + 56, 4)) ? Memory::Read32(obj + 56) : 0,
                    (obj && Memory::Contains(obj + 52, 1)) ? Memory::Read8(obj + 52) : 0,
                    (obj && Memory::Contains(obj + 53, 1)) ? Memory::Read8(obj + 53) : 0,
                    static_cast<float>(cpu->fpr[1].d), gx_bridge::g_gxFrame);
                if (target == 0x805C4380u && obj && Memory::Contains(obj, 8)) {
                    const uint32_t arr = Memory::Read32(obj + 152);
                    AppLog("[AnimPlay]   arr=%08X elem0=%08X elem1=%08X\n", arr,
                        arr ? Memory::Read32(arr) : 0,
                        (arr && Memory::Contains(arr + 0x44, 4)) ? Memory::Read32(arr + 0x44) : 0);
                }
            }
        }
    }
    if (target == 0x8063C91Cu) {
        static std::set<uint32_t> s_seenListNull;
        const uint32_t obj = cpu ? cpu->gpr[3] : 0;
        const uint32_t p24 = (obj && Memory::Contains(obj + 24, 4)) ? Memory::Read32(obj + 24) : 0;
        if (obj && p24 == 0 && s_seenListNull.size() < 8 && s_seenListNull.insert(cpu->lr).second) {
            AppLog("[ListNull] lr=%08X obj=%08X vt=%08X p20=%08X p24=%08X p56=%08X frame=%u\n",
                cpu->lr, obj, (obj && Memory::Contains(obj, 4)) ? Memory::Read32(obj) : 0,
                (obj && Memory::Contains(obj + 20, 4)) ? Memory::Read32(obj + 20) : 0,
                p24,
                (obj && Memory::Contains(obj + 56, 4)) ? Memory::Read32(obj + 56) : 0,
                gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x8059DFBCu || target == 0x8059E080u || target == 0x8059E0E4u) {
        static std::set<uint32_t> s_seenGlobeCtor;
        const uint32_t key = (target << 16) ^ (cpu ? (cpu->lr & 0xFFFFu) : 0);
        if (s_seenGlobeCtor.size() < 12 && s_seenGlobeCtor.insert(key).second) {
            AppLog("[GlobeCtor] target=%08X lr=%08X pc=%08X this=%08X frame=%u\n",
                target, cpu ? cpu->lr : 0, cpu ? cpu->pc : 0, cpu ? cpu->gpr[3] : 0,
                gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x80554D10u || target == 0x80554E6Cu || target == 0x80554AD4u) {
        static std::set<uint32_t> s_seenGlobeShow;
        if (s_seenGlobeShow.size() < 12 && s_seenGlobeShow.insert(target).second) {
            AppLog("[GlobeShow] target=%08X lr=%08X this=%08X flag=%u frame=%u\n",
                target, cpu ? cpu->lr : 0, cpu ? cpu->gpr[3] : 0,
                Memory::Read8(0x809C19A0u), gx_bridge::g_gxFrame);
        }
    }
    if (cpu && cpu->lr == 0x805A1A20u) {
        static std::set<uint32_t> s_seenEarthDraw20;
        const uint32_t key = target;
        if (s_seenEarthDraw20.size() < 16 && s_seenEarthDraw20.insert(key).second) {
            const uint32_t obj = cpu->gpr[3];
            const uint32_t vt = (obj && Memory::Contains(obj, 4)) ? Memory::Read32(obj) : 0;
            AppLog("[EarthDraw20] target=%08X obj=%08X vt=%08X frame=%u\n",
                target, obj, vt, gx_bridge::g_gxFrame);
        }
    }
    if (cpu && cpu->lr >= 0x8051B3C8u && cpu->lr < 0x8051B7B0u) {
        static std::set<uint32_t> s_seenCalcCalls;
        if (s_seenCalcCalls.size() < 24 && s_seenCalcCalls.insert(target).second) {
            AppLog("[MenuCalcCall] target=%08X lr=%08X this=%08X frame=%u\n",
                target, cpu->lr, cpu->gpr[3], gx_bridge::g_gxFrame);
        }
    }
    if (cpu && (cpu->lr == 0x80562504u || cpu->lr == 0x8051BC9Cu || cpu->lr == 0x8051BCA8u)) {
        static std::set<uint32_t> s_seenSceneCalls;
        if (s_seenSceneCalls.size() < 12 && s_seenSceneCalls.insert(target).second) {
            AppLog("[SceneCall] target=%08X lr=%08X this=%08X frame=%u\n",
                target, cpu->lr, cpu->gpr[3], gx_bridge::g_gxFrame);
        }
    }
    if (target == 0x8051BC78u || target == 0x80009988u || target == 0x8023AEACu) {
        static std::map<uint32_t, uint32_t> s_drawChain;
        ++s_drawChain[target];
        static uint32_t s_lastDrawLog = 0;
        if (gx_bridge::g_gxFrame != s_lastDrawLog && gx_bridge::g_gxFrame >= 200) {
            s_lastDrawLog = gx_bridge::g_gxFrame;
            std::string out;
            for (const auto& kv : s_drawChain) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " %08X=%u", kv.first, kv.second);
                out += buf;
            }
            AppLog("[DrawChain] frame=%u%s\n", gx_bridge::g_gxFrame, out.c_str());
        }
    }
    if (cpu && gx_bridge::g_gxFrame >= 90 && gx_bridge::g_gxFrame <= 92) {
        static std::set<uint32_t> s_seenIndirect;
        if (s_seenIndirect.size() < 120 && s_seenIndirect.insert(target).second) {
            AppLog("[Indirect] frame=%u target=%08X lr=%08X this=%08X\n",
                gx_bridge::g_gxFrame, target, cpu->lr, cpu->gpr[3]);
        }
    }
    static uint64_t s_hleCallCount = 0;
    ++s_hleCallCount;
    if (s_hleCallCount % 100000 == 0) {
        std::fprintf(stderr, "[HLE Loop] target=%08X pc=%08X count=%llu frame=%u lr=%08X\n",
            target, cpu ? cpu->pc : 0, (unsigned long long)s_hleCallCount,
            gx_bridge::g_gxFrame, cpu ? cpu->lr : 0);
    }
    static bool interruptsEnabled = true;
    static uint32_t pendingConfigCommand = 0;
    constexpr uint32_t configOpenCallback = 0x801B0450u;
    constexpr uint32_t nandMissing = static_cast<uint32_t>(-12);

    // --- In-memory NAND backing -------------------------------------------------
    // Mario Kart Wii keeps its license in NAND:/rksys.dat. The port previously failed every
    // NANDOpen, so the game sat on the "new save data" prompt forever. Serve a real save from
    // disk (SDMC on device, the extracted assets on the host) and keep writes in memory.
    struct NandFile {
        std::string path;
        size_t pos = 0;
    };
    // Only *written* data is kept in the host heap; read-only NAND files (rksys.dat is 2.8 MiB) are
    // streamed straight from disk on each NANDRead.  Keeping a host-side copy of the save cost more
    // heap than the Old 3DS has left once the guest arenas are mapped.
    static std::map<std::string, std::vector<uint8_t>> s_nandStore;
    static std::map<std::string, bool> s_nandLoaded;
    static std::map<std::string, std::string> s_nandDisk;
    static std::map<uint32_t, NandFile> s_nandOpen;
    static uint32_t s_nandNextFd = 0x500;
    auto nandFileSize = [](const std::string& path) -> size_t {
        auto st = s_nandStore.find(path);
        if (st != s_nandStore.end() && !st->second.empty()) return st->second.size();
        auto dsk = s_nandDisk.find(path);
        if (dsk == s_nandDisk.end()) return 0;
        FILE* fp = std::fopen(dsk->second.c_str(), "rb");
        if (!fp) return 0;
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::fclose(fp);
        return size > 0 ? static_cast<size_t>(size) : 0;
    };
    auto nandLoad = [](const std::string& path) -> bool {
        if (s_nandLoaded[path]) {
            return s_nandStore.find(path) != s_nandStore.end() || s_nandDisk.find(path) != s_nandDisk.end();
        }
        s_nandLoaded[path] = true;
        if (path.find("rksys.dat") == std::string::npos) return false;
        const char* candidates[] = {
            "sdmc:/mkwii/rksys.dat",
            "../../Assets/extracted-rmcp01/save/rksys.dat",
            "Assets/extracted-rmcp01/save/rksys.dat",
            "../../Assets/save/rksys.dat",
        };
        for (const char* candidate : candidates) {
            FILE* fp = std::fopen(candidate, "rb");
            if (!fp) continue;
            std::fseek(fp, 0, SEEK_END);
            const long size = std::ftell(fp);
            std::fclose(fp);
            if (size > 0) {
                s_nandDisk[path] = candidate;
                AppLog("[NAND] '%s' -> %s (%ld bytes, streamed from disk)\n", path.c_str(), candidate, size);
                return true;
            }
        }
        return false;
    };
    auto nandInfoFd = [](uint32_t info) -> uint32_t {
        return (info && Memory::Contains(info, 12)) ? Memory::Read32(info) : 0;
    };
    const uint32_t channel = cpu->gpr[3];
    switch (target) {
    case 0x802439B4u: // func_802439B4: returns 0
        cpu->gpr[3] = 0;
        return true;
    // Match desktop WiiCompiled EXI HLE overrides (MMIO at 0xCD0068xx stubbed)
    case 0x80169164u: // EXILock
    case 0x80169260u: // EXIUnlock
    case 0x801689D0u: // EXISelect
    case 0x80168B00u: // EXIDeselect
    case 0x80168380u: // EXISync
    case 0x80167E78u: // SetExiInterruptMask
        cpu->gpr[3] = 1;
        return true;
    case 0x80167F68u: { // EXIImm
        const uint32_t buffer = cpu->gpr[4];
        const uint32_t length = cpu->gpr[5];
        const uint32_t type = cpu->gpr[6];
        if (type == 0 && buffer != 0 && Memory::Contains(buffer, length)) {
            std::memset(Memory::GetPointer(buffer, length), 0, length);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x80168288u: { // EXIDma
        const uint32_t buffer = cpu->gpr[4];
        const uint32_t length = cpu->gpr[5];
        if (buffer != 0 && Memory::Contains(buffer, length)) {
            std::memset(Memory::GetPointer(buffer, length), 0, length);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801A197Cu: // LCQueueLength. DMA completes synchronously in PPC_WriteSpr.
        cpu->gpr[3] = 0;
        return true;
    case 0x801A1988u: // LCQueueWait
        return true;
    // Match the existing desktop overrides for hardware-only operations.
    case 0x80006348u:
    case 0x801A1600u:
    case 0x801A162Cu:
    case 0x801A165Cu: // DCStoreRange
    case 0x801A16B8u: // DCStoreRangeNoSync
    case 0x801A168Cu: // Guest data RAM is directly backed by host memory.
    case 0x801A1710u: // Translated code is separate from the guest RAM image.
    case 0x801A1AE4u:
    case 0x801B2DE0u: // Desktop SIInit skips hardware setup.
    case 0x8012E5B8u: case 0x8012E5C0u: case 0x8012E5C8u:
    case 0x8012E5D0u: case 0x8012E5D8u: case 0x8012E5E0u:
    case 0x8012E654u: case 0x8012E684u:
        return true;
    case 0x80197380u: { // WPAD status read (chan, out, len) - unlinked, so fill it explicitly
        const uint32_t out = cpu->gpr[4];
        const uint32_t len = cpu->gpr[5];
        static uint32_t s_wpadStatusLogs = 0;
        if (s_wpadStatusLogs < 4) {
            ++s_wpadStatusLogs;
            AppLog("[WPAD] status read chan=%u out=%08X len=%u lr=%08X\n",
                cpu->gpr[3], out, len, cpu->lr);
        }
        if (out && len && len <= 0x100 && Memory::Contains(out, len)) {
            // No buttons pressed. Leaving this buffer untouched let stale guest memory decide
            // the HOME button state, which put the game on the "Return to the Wii Menu" page.
            std::memset(Memory::GetPointer(out, len), 0, len);
            // Late A-pulse only: pulsing during boot makes the game skip the menu load, but
            // after the menu is up it should drive the license/WiFi navigation.
            if (len >= 2 && gx_bridge::g_gxFrame >= 200 && (gx_bridge::g_gxFrame % 8) < 3) {
                Memory::Write16(out, 0x0800); // WPAD_BUTTON_A
            }
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801BD734u: { // WPAD::Read(chan, status) - must fill the buffer, not leave it stale
        static uint32_t s_wpadReadLogs = 0;
        if (s_wpadReadLogs < 6) {
            ++s_wpadReadLogs;
            AppLog("[WPAD] Read chan=%u status=%08X r5=%08X r6=%08X lr=%08X\n",
                cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->lr);
        }
        const uint32_t statusPtr = cpu->gpr[4];
        if (statusPtr && Memory::Contains(statusPtr, 0x60)) {
            std::memset(Memory::GetPointer(statusPtr, 0x60), 0, 0x60);
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801BF5C4u: // WPAD::Init
    case 0x801BF64Cu: // WPAD::GetStatus
        cpu->gpr[3] = 2; // WPAD_STATE_ENABLED
        return true;
    case 0x801C0990u: { // WPAD::Probe
        const uint32_t typePtr = cpu->gpr[4];
        if (typePtr != 0 && Memory::Contains(typePtr, 4)) {
            Memory::Write32(typePtr, 2); // Classic Controller
        }
        cpu->gpr[3] = 0; // WPAD_ERR_NONE
        return true;
    }
    case 0x8019812Cu: { // KPADRead(chan, status, size)
        uint32_t chan = cpu->gpr[3];
        uint32_t statusPtr = cpu->gpr[4];
        if (statusPtr != 0 && Memory::Contains(statusPtr, 0x40)) {
            std::memset(Memory::GetPointer(statusPtr, 0x40), 0, 0x40);
            if (chan == 0) {
                uint32_t hold = 0;
#ifdef __3DS__
                hidScanInput();
                uint32_t kHeld = hidKeysHeld();
                circlePosition circlePos;
                hidCircleRead(&circlePos);
                float lx = static_cast<float>(circlePos.dx) / 150.0f;
                float ly = static_cast<float>(circlePos.dy) / 150.0f;
                lx = std::max(-1.0f, std::min(1.0f, lx));
                ly = std::max(-1.0f, std::min(1.0f, ly));

                // Classic Controller mapping
                if (kHeld & KEY_A) hold |= 0x0010; // CL_A (Accelerate)
                if (kHeld & KEY_B) hold |= 0x0040; // CL_B (Brake)
                if (kHeld & KEY_X) hold |= 0x0008; // CL_X (Item)
                if (kHeld & KEY_Y) hold |= 0x0020; // CL_Y (Wheelie)
                if (kHeld & (KEY_R | KEY_ZR)) hold |= 0x0200; // CL_R (Drift)
                if (kHeld & (KEY_L | KEY_ZL)) hold |= 0x2000; // CL_L (Item)
                if (kHeld & KEY_START) hold |= 0x0400; // CL_PLUS (Pause)
                if (kHeld & KEY_SELECT) hold |= 0x1000; // CL_MINUS
                if (kHeld & KEY_DUP) hold |= 0x0001;
                if (kHeld & KEY_DLEFT) hold |= 0x0002;
                if (kHeld & KEY_DDOWN) hold |= 0x4000;
                if (kHeld & KEY_DRIGHT) hold |= 0x8000;

                // Extension (Classic Controller) at offset 0x30
                Memory::Write32(statusPtr + 0x30, hold);
                gx_bridge::WriteGuestFloat(statusPtr + 0x3C, lx);
                gx_bridge::WriteGuestFloat(statusPtr + 0x40, ly);
#endif
                hold |= ScriptedPadButtons(ScriptPadKind::kClassic);
                Memory::Write32(statusPtr + 0x30, hold);
            }
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801AF2F0u: // PAD::Init
        cpu->gpr[3] = 1;
        return true;
    case 0x801AF44Cu: { // PAD::Read(PADStatus* status)
        uint32_t statusPtr = cpu->gpr[3];
        if (statusPtr != 0 && Memory::Contains(statusPtr, 48)) {
            uint16_t buttons = 0;
            int8_t stickX = 0;
            int8_t stickY = 0;
            uint8_t triggerL = 0;
            uint8_t triggerR = 0;

#ifdef __3DS__
            hidScanInput();
            uint32_t kHeld = hidKeysHeld();
            circlePosition circlePos;
            hidCircleRead(&circlePos);

            // Circle pad mapping to GameCube stick (-128..127)
            int sx = (static_cast<int>(circlePos.dx) * 127) / 150;
            int sy = (static_cast<int>(circlePos.dy) * 127) / 150;
            stickX = static_cast<int8_t>(std::max(-128, std::min(127, sx)));
            stickY = static_cast<int8_t>(std::max(-128, std::min(127, sy)));

            // D-Pad override/fallback
            if (kHeld & KEY_DLEFT)  stickX = -127;
            if (kHeld & KEY_DRIGHT) stickX = 127;
            if (kHeld & KEY_DUP)    stickY = 127;
            if (kHeld & KEY_DDOWN)  stickY = -127;

            // Buttons mapping to GameCube controller:
            // PAD_BUTTON_A = 0x0100 (Accelerate)
            if (kHeld & KEY_A) buttons |= 0x0100;
            // PAD_BUTTON_B = 0x0200 (Brake / Reverse)
            if (kHeld & KEY_B) buttons |= 0x0200;
            // PAD_BUTTON_X = 0x0400 (Item)
            if (kHeld & KEY_X) buttons |= 0x0400;
            // PAD_BUTTON_Y = 0x0800 (Wheelie)
            if (kHeld & KEY_Y) buttons |= 0x0800;
            // PAD_BUTTON_START = 0x1000 (Pause / Skip intro)
            if (kHeld & KEY_START) buttons |= 0x1000;
            // PAD_TRIGGER_R = 0x0020 (Drift / Hop)
            if (kHeld & (KEY_R | KEY_ZR)) {
                buttons |= 0x0020;
                triggerR = 255;
            }
            // PAD_TRIGGER_L = 0x0040 (Use Item), PAD_TRIGGER_Z = 0x0010 (Hold Item)
            if (kHeld & (KEY_L | KEY_ZL | KEY_SELECT)) {
                buttons |= 0x0050; // L + Z
                triggerL = 255;
            }
#endif

            // Auto-pulse A + START (race intro skip) or the scripted button window.
            buttons |= static_cast<uint16_t>(ScriptedPadButtons(ScriptPadKind::kGcPad));

            // Write Port 0 PADStatus (12 bytes)
            Memory::Write16(statusPtr + 0, buttons);
            Memory::Write8(statusPtr + 2, static_cast<uint8_t>(stickX));
            Memory::Write8(statusPtr + 3, static_cast<uint8_t>(stickY));
            Memory::Write8(statusPtr + 4, 0); // substickX
            Memory::Write8(statusPtr + 5, 0); // substickY
            Memory::Write8(statusPtr + 6, triggerL);
            Memory::Write8(statusPtr + 7, triggerR);
            Memory::Write8(statusPtr + 8, (buttons & 0x0100) ? 255 : 0); // analogA
            Memory::Write8(statusPtr + 9, (buttons & 0x0200) ? 255 : 0); // analogB
            Memory::Write8(statusPtr + 10, 0); // err: 0 = PAD_ERR_NONE (Connected!)
            Memory::Write8(statusPtr + 11, 0); // reserved

            // Ports 1, 2, 3: Disconnected (err = -1)
            for (int i = 1; i < 4; ++i) {
                Memory::Write16(statusPtr + i * 12 + 0, 0);
                Memory::Write8(statusPtr + i * 12 + 10, static_cast<uint8_t>(-1)); // PAD_ERR_NO_CONTROLLER
            }
        }
        cpu->gpr[3] = 0x80000000u; // PAD_CHAN0_BIT: Controller 0 is CONNECTED!
        return true;
    }
    case 0x801AF908u: // PAD::ControlMotor
        cpu->gpr[3] = 0;
        return true;
    case 0x801AB670u: // OS::SetResetCallback
    case 0x801AB75Cu: // OS::SetPowerCallback
    case 0x801ABA48u: // OS::__SetVIAutoDimming
        cpu->gpr[3] = 0;
        return true;
    case 0x801A08E0u: { // OSSetPeriodicAlarm
        const uint32_t alarm = cpu->gpr[3];
        const uint32_t periodHi = cpu->gpr[7];
        const uint32_t periodLo = cpu->gpr[8];
        const uint32_t handler = cpu->gpr[9];
        if (alarm != 0 && Memory::Contains(alarm, 0x28)) {
            Memory::Write32(alarm + 0x00u, handler);
            Memory::Write32(alarm + 0x18u, periodHi);
            Memory::Write32(alarm + 0x1Cu, periodLo);
            Memory::Write32(alarm + 0x10u, 0);
            Memory::Write32(alarm + 0x14u, 0);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801A0870u: { // OSSetAlarm
        const uint32_t alarm = cpu->gpr[3];
        const uint32_t handler = cpu->gpr[7];
        if (alarm != 0 && Memory::Contains(alarm, 0x28)) {
            Memory::Write32(alarm + 0x00u, handler);
            Memory::Write32(alarm + 0x18u, 0);
            Memory::Write32(alarm + 0x1Cu, 0);
            Memory::Write32(alarm + 0x10u, 0);
            Memory::Write32(alarm + 0x14u, 0);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x800BD860u: // RFLiIsWorking
        cpu->gpr[3] = 0;
        return true;
    case 0x8061F7CCu:
    case 0x8061F828u:
    case 0x8061F9B4u:
    case 0x8061FBECu:
    case 0x805BB278u:
    case 0x805BB254u:
    case 0x806025B0u: { // page init - dump the live page object once
        static bool s_dumpedPage = false;
        if (!s_dumpedPage && cpu->gpr[3] && Memory::Contains(cpu->gpr[3], 160)) {
            s_dumpedPage = true;
            std::string dump;
            for (int i = 0; i < 40; ++i) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(cpu->gpr[3] + i * 4));
                dump += buf;
            }
            AppLog("[PageDump] this=%08X%s\n", cpu->gpr[3], dump.c_str());
        }
        break;
    }
    case 0x806024B0u:
    case 0x80602530u:
    case 0x805c9820u:
    case 0x805c9888u:
    case 0x805c9890u:
    case 0x805c988cu: { // page base + globe page overrides
        static std::map<uint32_t, uint32_t> s_pageMethodCounts;
        ++s_pageMethodCounts[target];
        static uint32_t s_lastLogFrame = 0;
        if (gx_bridge::g_gxFrame != s_lastLogFrame && gx_bridge::g_gxFrame >= 200) {
            s_lastLogFrame = gx_bridge::g_gxFrame;
            std::string out;
            for (const auto& kv : s_pageMethodCounts) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " %08X=%u", kv.first, kv.second);
                out += buf;
            }
            AppLog("[PageMethodCounts] frame=%u%s\n", gx_bridge::g_gxFrame, out.c_str());
        }
        return false;
    }
    case 0x80554E14u: { // gates the page-manager update on *(mgr+76)
        static uint32_t s_lastFrame = 0;
        if (gx_bridge::g_gxFrame >= 84 && gx_bridge::g_gxFrame <= 130 && gx_bridge::g_gxFrame != s_lastFrame) {
            s_lastFrame = gx_bridge::g_gxFrame;
            const uint32_t mgr = Memory::Read32(0x809BD748u);
            const uint32_t flag = (mgr && Memory::Contains(mgr + 76, 1)) ? Memory::Read8(mgr + 76) : 0xFFu;
            AppLog("[PageGate] frame=%u mgr=%08X flag=%u\n", gx_bridge::g_gxFrame, mgr, flag);
        }
        return false;
    }
    case 0x806223A0u: { // PageManager entry (page switch / create)
        static bool s_dumpedMgr = false;
        if (!s_dumpedMgr && cpu->gpr[3] && Memory::Contains(cpu->gpr[3], 160)) {
            s_dumpedMgr = true;
            std::string dump;
            for (int i = 0; i < 40; ++i) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(cpu->gpr[3] + i * 4));
                dump += buf;
            }
            AppLog("[PageMgrDump] this=%08X%s\n", cpu->gpr[3], dump.c_str());
        }
        static std::set<uint32_t> s_seenArgs;
        const uint32_t key = (cpu->gpr[3] & 0xFFFF0000u) ^ (cpu->gpr[4] & 0xFFFFu) ^ (cpu->gpr[5] << 8);
        if (s_seenArgs.size() < 24 && s_seenArgs.insert(key).second) {
            AppLog("[PageMgr] frame=%u this=%08X r4=%08X r5=%08X r6=%08X lr=%08X\n",
                gx_bridge::g_gxFrame, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->lr);
        }
        return false;
    }
    case 0x80601D24u: { // Page::Init(page)
        const uint32_t page = cpu->gpr[3];
        static uint32_t s_pageInitLogs = 0;
        if (s_pageInitLogs < 20 && page && Memory::Contains(page, 64)) {
            ++s_pageInitLogs;
            AppLog("[PageInit] page=%08X vt=%08X f4=%08X f8=%08X fC=%08X f10=%08X f14=%08X lr=%08X\n",
                page, Memory::Read32(page), Memory::Read32(page + 4), Memory::Read32(page + 8),
                Memory::Read32(page + 12), Memory::Read32(page + 16), Memory::Read32(page + 20), cpu->lr);
        }
        return false; // let the real Page::Init run
    }
    case 0x805EF688u: { // PageManipulatorManager::OnReset
        if (!g_raceShortcut) return false; // menu path: run the real manipulator code
        if (cpu && cpu->gpr[3]) {
            const uint32_t manipMgr = cpu->gpr[3];
            if (Memory::Contains(manipMgr, 64)) {
                Memory::Write8(manipMgr + 12, 0);
            }
        }
        return true;
    }
    case 0x805EF5A8u: { // PageManipulatorManager::Activate
        if (!g_raceShortcut) return false; // menu path: run the real manipulator code
        if (cpu && cpu->gpr[3]) {
            const uint32_t manipMgr = cpu->gpr[3];
            if (Memory::Contains(manipMgr, 64)) {
                Memory::Write32(manipMgr + 4, cpu->gpr[4]);
                Memory::Write32(manipMgr + 8, cpu->gpr[5]);
                Memory::Write8(manipMgr + 12, 0);
            }
        }
        return true;
    }
    case 0x805EF768u: { // PageManipulatorManager::SetGlobalHandler
        if (!g_raceShortcut) return false; // menu path: run the real manipulator code
        if (cpu && cpu->gpr[3]) {
            const uint32_t manipMgr = cpu->gpr[3];
            const uint32_t r4 = cpu->gpr[4];
            const uint32_t r5 = cpu->gpr[5];
            const uint32_t r6 = cpu->gpr[6];
            if (Memory::Contains(manipMgr, 64)) {
                Memory::Write32(manipMgr + 16 + (r4 << 2), r5);
                Memory::Write8(manipMgr + 52 + r4, static_cast<uint8_t>(r6));
            }
        }
        return true;
    }
    case 0x805EF888u: // ControlButtonInfo::Reset
    case 0x805EF898u: // ControlButtonInfo::Update
    {
        if (!g_raceShortcut) return false; // menu path: run the real manipulator code
        return true;
    }
    case 0x8061C86Cu: {
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x8061C8E8u: {
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        return true;
    }
    case 0x8061CA40u: { // PageRecognizePad::OnReset (slot 13)
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        const uint32_t secMgr = ReadTraceWord(0x809C1E38);
        if (secMgr && Memory::Contains(secMgr, 4)) {
            const uint32_t sec = Memory::Read32(secMgr);
            if (sec && Memory::Contains(sec, 910)) {
                Memory::Write8(sec + 904, 0);
                Memory::Write8(sec + 906, 0);
            }
        }
        return true;
    }
    case 0x8061CAA8u: { // PageRecognizePad::CheckActions (slot 14)
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        if (cpu && cpu->gpr[3]) {
            const uint32_t page = cpu->gpr[3];
            if (Memory::Contains(page, 16)) {
                Memory::Write8(page + 12, 1);
            }
        }
        return true;
    }
    case 0x8061CB94u:   // PageRecognizePad::Slot15
    case 0x8061CBE0u: { // PageRecognizePad::Slot16
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        if (cpu && cpu->gpr[3]) {
            const uint32_t page = cpu->gpr[3];
            if (Memory::Contains(page, 16)) {
                Memory::Write8(page + 12, 1);
            }
        }
        return true;
    }
    case 0x8061CBF4u: // PageRecognizePad::Slot17
    case 0x8061CC38u: // PageRecognizePad::Slot18
    case 0x8061CD24u: // PageRecognizePad::Slot19
    case 0x8061CDA8u: // PageRecognizePad::Slot21
    case 0x8061CE2Cu: // PageRecognizePad::Slot22
    case 0x8061CF00u: // PageRecognizePad::Slot23
    case 0x8061CFECu: // PageRecognizePad::Slot24
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        return true;
    case 0x8061C0D8u: // Pages::RecognizePad::OnInit - suppress dialog
    case 0x8061C1C0u: // Pages::RecognizePad::OnActivate
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        return true;
    case 0x8061C518u: // Pages::RecognizePad::IsVisible
        if (std::getenv("MKW_NATURAL_PAGES")) return false;
        if (cpu) cpu->gpr[3] = 0; // Not visible
        return true;
    case 0x805F9B50u: // BlackBackControlForMessage::Load
    case 0x805F9BC4u: // BlackBackControlForMessage::SetPositionAnim
    case 0x805F9CC8u: // BusySymbol::Load
    case 0x805F9D5Cu: // BusySymbol::InitSelf
    case 0x806018ECu: // RecognizePadWindow::InitSelf
    case 0x806018F8u: // RecognizePadWindow::Calc
    case 0x806018FCu: // RecognizePadWindow::OnUpdate
    case 0x80601940u: // RecognizePadWindow::GetRuntimeTypeInfo
    case 0x8060194Cu: // PadControl::GetRuntimeTypeInfo
        return true;
    case 0x80022B30u: // nw4r::db::PrintToBuffer_
    case 0x80022EE0u: // nw4r::db::Console_Printf
    case 0x80022FC0u: // nw4r::db::Console_VPrintf
    case 0x80025D00u: // nw4r::db::Assertion_Printf_
    case 0x80025DC0u: // nw4r::db::Assertion_VPrintf_
    case 0x80025E70u: // nw4r::db::Panic_
    case 0x80025FB0u: { // nw4r::db::VPanic
        const uint32_t file = cpu->gpr[3];
        const uint32_t line = cpu->gpr[4];
        const uint32_t msg = cpu->gpr[5];
        std::string fileStr = "unknown", msgStr = "unknown";
        if (file && Memory::Contains(file, 1)) fileStr = reinterpret_cast<const char*>(Memory::GetPointer(file, 1));
        if (msg && Memory::Contains(msg, 1)) msgStr = reinterpret_cast<const char*>(Memory::GetPointer(msg, 1));
        if (cpu) {
            ++gx_bridge::g_panicTotal;
            ++gx_bridge::g_panicSites[cpu->lr];
        }
        static int s_panicPrints = 0;
        if (++s_panicPrints <= 10) {
            AppLog("[NW4R Panic] %s:%u (caller lr=%08X): %s\n", fileStr.c_str(), line, cpu ? cpu->lr : 0, msgStr.c_str());
            if (cpu) {
                const uint32_t sp = cpu->gpr[1];
                AppLog("  savedlr=%08X f1=%g frame=%u\n",
                    (sp && Memory::Contains(sp + 68, 4)) ? Memory::Read32(sp + 68) : 0,
                    static_cast<float>(cpu->fpr[1].d), gx_bridge::g_gxFrame);
            }
            if (cpu) {
                AppLog("  r20=%08X r29=%08X r30=%08X r31=%08X\n", cpu->gpr[20], cpu->gpr[29], cpu->gpr[30], cpu->gpr[31]);
                if (cpu->gpr[30] && Memory::Contains(cpu->gpr[30], 64)) {
                    AppLog("  *(r30+20)=%08X *(r30+24)=%08X *(r30+28)=%08X\n",
                        Memory::Read32(cpu->gpr[30] + 20),
                        Memory::Read32(cpu->gpr[30] + 24),
                        Memory::Read32(cpu->gpr[30] + 28));
                }
                const uint32_t o31 = cpu->gpr[31];
                if (o31 && Memory::Contains(o31, 64)) {
                    AppLog("  r31obj=%08X vt=%08X p0=%08X p20=%08X p24=%08X p56=%08X\n",
                        o31, Memory::Read32(o31), Memory::Read32(o31),
                        Memory::Read32(o31 + 20), Memory::Read32(o31 + 24), Memory::Read32(o31 + 56));
                    uint32_t node = Memory::Read32(o31 + 24);
                    char buf[512]; int n = 0;
                    for (int i = 0; i < 10 && node && Memory::Contains(node, 16); ++i) {
                        n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), " %08X->%08X", node, Memory::Read32(node));
                        node = Memory::Read32(node);
                    }
                    AppLog("  list:%s (tail=%08X sentinel=%08X)\n", buf, node, o31 + 24);
                    for (int b = 0; b < 64; b += 16) {
                        AppLog("  obj+%02X: %08X %08X %08X %08X\n", b,
                            Memory::Read32(o31 + b), Memory::Read32(o31 + b + 4),
                            Memory::Read32(o31 + b + 8), Memory::Read32(o31 + b + 12));
                    }
                    const uint32_t l0 = Memory::Read32(o31 + 24);
                    if (l0 && Memory::Contains(l0, 64)) {
                        for (int b = 0; b < 64; b += 16) {
                            AppLog("  list+%02X: %08X %08X %08X %08X\n", b,
                                Memory::Read32(l0 + b), Memory::Read32(l0 + b + 4),
                                Memory::Read32(l0 + b + 8), Memory::Read32(l0 + b + 12));
                        }
                    }
                    const uint32_t vt31 = Memory::Read32(o31);
                    if (vt31 && Memory::Contains(vt31, 32)) {
                        AppLog("  vt=%08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", vt31,
                            Memory::Read32(vt31), Memory::Read32(vt31 + 4),
                            Memory::Read32(vt31 + 8), Memory::Read32(vt31 + 12),
                            Memory::Read32(vt31 + 16), Memory::Read32(vt31 + 20),
                            Memory::Read32(vt31 + 24), Memory::Read32(vt31 + 28));
                    }
                }
            }
        }
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }
    case 0x805411FCu: { // ArchiveMgr::GetFile(mgr, type, name, flags)
        // func_805411FC only accepts ArchiveMgr entries whose state (entry+32) is 4 or 5:
        //   array = *(mgr+4); arc = *(array + type*4);
        //   count = *(u16*)(arc+8); entries = *(arc+4); stride 36
        // When every lookup in every type returns 0, no archive group is usable and NOTHING can
        // be loaded (karts, fonts, post effects). That is exactly the kart blocker.
        const uint32_t namePtr = cpu->gpr[5];
        const uint32_t archiveType = cpu->gpr[4];
        const uint32_t mgrPtr = cpu->gpr[3];
        const uint32_t caller = cpu->lr;
        if (!gx_bridge::g_tracedArchiveMgr) {
            gx_bridge::g_tracedArchiveMgr = mgrPtr;
            // Populate the entry watch window immediately (the frame hook only runs every 20
            // frames, which would miss the archive-load window entirely).
            const uint32_t ab = mgrPtr ? Memory::Read32(mgrPtr + 4) : 0;
            for (uint32_t ty = 0; ab && ty < 8 && Memory::Contains(ab + ty * 4, 4); ++ty) {
                const uint32_t arc = Memory::Read32(ab + ty * 4);
                if (!arc || !Memory::Contains(arc, 12)) break;
                gx_bridge::g_watchArcs[ty] = arc;
                gx_bridge::g_watchArcN = ty + 1;
                if (ty == 0) {
                    const uint32_t cnt = Memory::Read16(arc + 8);
                    const uint32_t ent = Memory::Read32(arc + 4);
                    if (ent) { gx_bridge::g_watchArcEntries = ent; gx_bridge::g_watchArcCount = cnt; }
                }
            }
            AppLog("[ArcWatch] watching mgr=%08X entries=%08X count=%u arcs=%u\n",
                mgrPtr, gx_bridge::g_watchArcEntries, gx_bridge::g_watchArcCount, gx_bridge::g_watchArcN);
        }
        std::string reqName;
        if (namePtr && Memory::Contains(namePtr, 1)) {
            reqName = reinterpret_cast<const char*>(Memory::GetPointer(namePtr, 1));
        }

        // Compact per-type state snapshot: shows when/whether an archive group becomes usable.
        {
            static std::set<uint32_t> s_dumpedFrames;
            const uint32_t f = gx_bridge::g_gxFrame;
            if ((f < 90 || f % 100 == 0) && mgrPtr && Memory::Contains(mgrPtr, 64) &&
                s_dumpedFrames.size() < 40 && s_dumpedFrames.insert(f).second) {
                const uint32_t arrayBase = Memory::Read32(mgrPtr + 4);
                std::string out;
                if (arrayBase && Memory::Contains(arrayBase, 32)) {
                    for (uint32_t ty = 0; ty < 8; ++ty) {
                        const uint32_t arc = Memory::Read32(arrayBase + ty * 4);
                        if (!arc || !Memory::Contains(arc, 12)) break;
                        const uint32_t count = Memory::Read16(arc + 8);
                        const uint32_t entries = Memory::Read32(arc + 4);
                        out += " t" + std::to_string(ty) + "=[";
                        for (uint32_t e = 0; e < count && e < 6; ++e) {
                            if (!entries || !Memory::Contains(entries + e * 36u, 36)) break;
                            if (e) out += ",";
                            out += std::to_string(Memory::Read32(entries + e * 36u + 32));
                        }
                        out += "]";
                    }
                }
                AppLog("[ArcState] frame=%u%s\n", f, out.c_str());
            }
        }

        func_805411FC(cpu);
        uint32_t result = cpu->gpr[3];
        // Some resources (Menu.bmg, the Font.szs .brfnt files) live in UI archives that the
        // ArchiveMgr group has not mounted yet when the menu layout first asks for them, and
        // the game never retries. Recover the bytes from any UI archive we have already
        // decompressed into guest memory. The returned pointer is the archive's own resource
        // data, so no copy or extra allocation is involved.
        if (!result && !reqName.empty() && reqName.size() < 160) {
            const char* leaf = reqName.c_str();
            const char* slash = std::strrchr(leaf, '/');
            if (slash) leaf = slash + 1;
            for (uint32_t arcData : g_decompressedArcs) {
                if (!arcData || !Memory::Contains(arcData, 4)) continue;
                uint32_t foundSize = 0;
                uint32_t found = FindU8Resource(arcData, reqName.c_str(), &foundSize);
                if (!found && leaf != reqName.c_str()) {
                    found = FindU8Resource(arcData, leaf, &foundSize);
                }
                if (!found) continue;
                result = found;
                cpu->gpr[3] = found;
                static std::set<std::string> s_loggedArchiveRecoveries;
                if (s_loggedArchiveRecoveries.size() < 64 && s_loggedArchiveRecoveries.insert(reqName).second) {
                    AppLog("[ArchiveMgr] recovered '%s' from decompressed arc=%08X data=%08X size=%u frame=%u\n",
                        reqName.c_str(), arcData, found, foundSize, gx_bridge::g_gxFrame);
                }
                break;
            }
        }
        const auto* trace = FindTracedModelFilename(namePtr);
        if (trace) {
            AppLog("[Race Model] ArchiveMgr::GetFile %s lr=%08X type=%u brres=%08X magic=%08X\n",
                trace->filename, caller, archiveType, result, ReadTraceWord(result));
        }
        if (!reqName.empty()) {
            static std::set<std::string> s_seenArchiveLookups;
            static std::set<std::string> s_resolvedArchiveLookups;
            static uint32_t s_fontLookupLogs = 0;
            std::string key = std::to_string(archiveType) + "|" + reqName;
            const bool fontLike = reqName.find("font") != std::string::npos ||
                reqName.find(".brfnt") != std::string::npos ||
                reqName.find(".bmg") != std::string::npos;
            if (fontLike && result && s_resolvedArchiveLookups.size() < 40 &&
                s_resolvedArchiveLookups.insert(key).second) {
                AppLog("[ArchiveMgr] RESOLVED type=%u name='%s' -> %08X frame=%u\n",
                    archiveType, reqName.c_str(), result, gx_bridge::g_gxFrame);
            }
            if (fontLike && !result && gx_bridge::g_gxFrame <= 400 && s_fontLookupLogs < 40) {
                ++s_fontLookupLogs;
                AppLog("[ArchiveMgr] font lookup failed type=%u name='%s' frame=%u\n",
                    archiveType, reqName.c_str(), gx_bridge::g_gxFrame);
            }
            if (s_seenArchiveLookups.size() < 200 && s_seenArchiveLookups.insert(key).second) {
                AppLog("[ArchiveMgr] type=%u name='%s' -> %08X (magic=%08X) frame=%u\n",
                    archiveType, reqName.c_str(), result, ReadTraceWord(result),
                    gx_bridge::g_gxFrame);
            }
        }
        return true;
    }
    case 0x80124894u: { // ARC::ConvertPathToEntrynum
        const auto* trace = FindTracedModelFilename(cpu->gpr[4]);
        if (!trace) return false;
        const uint32_t caller = cpu->lr;
        const uint32_t archive = ReadTraceWord(channel);
        func_80124894(cpu);
        AppLog("[Race Model] ARC path %s lr=%08X handle=%08X u8=%08X entry=%d\n",
            trace->filename, caller, channel, archive, static_cast<int32_t>(cpu->gpr[3]));
        return true;
    }
    case 0x8020FA80u: { // EGG::Archive::GetFileFast
        if (!g_tracedCourseU8 || ReadTraceWord(channel + 24) != g_tracedCourseU8) return false;
        const uint32_t state = ReadTraceWord(channel + 16);
        const uint32_t table = ReadTraceWord(channel + 28);
        const uint32_t count = ReadTraceWord(channel + 36);
        const uint32_t entry = cpu->gpr[4];
        const uint32_t info = cpu->gpr[5];
        const uint32_t caller = cpu->lr;
        const bool entryMapped = table && entry < count && entry < (UINT32_MAX - table) / 12 &&
            Memory::Contains(table + entry * 12, 12);
        const uint32_t row = entryMapped ? table + entry * 12 : 0;
        const uint32_t offset = row ? ReadTraceWord(row + 4) : 0;
        const uint32_t length = row ? ReadTraceWord(row + 8) : 0;
        const uint32_t expected = offset && offset <= UINT32_MAX - g_tracedCourseU8
            ? g_tracedCourseU8 + offset : 0;
        const auto* trace = FindTracedModel(expected);
        if (!trace || trace->archive != expected) return false;

        func_8020FA80(cpu);
        AppLog("[Race Model] GetFileFast %s lr=%08X state=%u table=%08X count=%u entry=%u expected=%08X result=%08X\n",
            trace->filename, caller, state, table, count, entry, expected, cpu->gpr[3]);
        // Diagnostic fallback for the traced stock files. Keep the original
        // archive, entry, guest address and EGG file-info layout.
        if (cpu->gpr[3] == 0 && state == 1 && (ReadTraceWord(row) >> 24) == 0 &&
            length == trace->size && Memory::Contains(expected, length) &&
            ReadTraceWord(expected) == 0x62726573u && (!info || Memory::Contains(info, 8))) {
            if (info) {
                Memory::Write32(info, offset);
                Memory::Write32(info + 4, length);
            }
            cpu->gpr[3] = expected;
            AppLog("[Race Model] GetFileFast recovered %s at %08X\n", trace->filename, expected);
        }
        return true;
    }
    case 0x8004C240u: // nw4r::g3d::ResFile::GetResMdl(name)
    case 0x8004C2B0u: // nw4r::g3d::ResFile::GetResMdl(index)
    case 0x8004C350u: { // nw4r::g3d::ResFile::GetResMdl(u32 index)
        const uint32_t archive = ReadTraceWord(channel);
        auto* trace = FindTracedModel(archive);
        if (!trace) {
            // Non-traced archive (kart/driver models). Call through and record whether the lookup
            // EVER succeeds and at which frame - this is what tells us whether the synchronous
            // kart creation at frame 84 permanently misses the archives that finish at ~frame 100.
            std::string nm;
            if (target == 0x8004C240u) {
                const uint32_t namePtr = cpu->gpr[4];
                if (namePtr && Memory::Contains(namePtr, 1)) {
                    nm = reinterpret_cast<const char*>(Memory::GetPointer(namePtr, 1));
                }
                func_8004C240(cpu);
            } else if (target == 0x8004C2B0u) {
                func_8004C2B0(cpu);
            } else {
                func_8004C350(cpu);
            }
            const uint32_t mdl = cpu->gpr[3];
            // RESCUE: section 32 creates the karts at frame 84, but the ArchiveMgr groups only
            // become usable at ~frame 100, so the guest's own lookup fails and is never retried.
            // We already hold every decompressed archive image, so resolve the model directly from
            // those instead of returning NULL. Same mechanism that already makes the course models
            // load (see FindU8Resource / the [Race Model] recovery path).
            uint32_t rescued = 0;
            if (mdl == 0 && !nm.empty() && target == 0x8004C240u) {
                uint32_t foundSize = 0;
                for (uint32_t arcData : g_decompressedArcs) {
                    if (!arcData || !Memory::Contains(arcData, 4)) continue;
                    const uint32_t found = FindU8Resource(arcData, nm.c_str(), &foundSize);
                    if (found && Memory::Contains(found, 8) && ReadTraceWord(found) == 0x62726573u) {
                        rescued = found;
                        cpu->gpr[3] = found;
                        static std::set<std::string> s_seenRescue;
                        std::string rk = nm + "|" + std::to_string(arcData);
                        if (s_seenRescue.size() < 96 && s_seenRescue.insert(rk).second) {
                            AppLog("[Model Rescue] '%s' from arc=%08X -> mdl=%08X size=%u frame=%u\n",
                                nm.c_str(), arcData, found, foundSize, gx_bridge::g_gxFrame);
                        }
                        break;
                    }
                }
            }
            static std::set<std::string> s_seenOk, s_seenFail;
            std::string key = nm + "|res=" + std::to_string(channel);
            if (rescued != 0) {
                // already logged above
            } else if (mdl != 0) {
                if (s_seenOk.size() < 64 && s_seenOk.insert(key).second) {
                    AppLog("[Model Ok] '%s' resFile=%08X -> mdl=%08X magic=%08X frame=%u\n",
                        nm.c_str(), channel, mdl, ReadTraceWord(mdl), gx_bridge::g_gxFrame);
                }
            } else if (s_seenFail.size() < 64 && s_seenFail.insert(key).second) {
                AppLog("[Model Fail] '%s' resFile=%08X archive=%08X frame=%u lr=%08X\n",
                    nm.c_str(), channel, archive, gx_bridge::g_gxFrame, cpu->lr);
            }
            return true;
        }
        const uint32_t query = cpu->gpr[4];
        const uint32_t caller = cpu->lr;
        if (target == 0x8004C240u) func_8004C240(cpu);
        else if (target == 0x8004C2B0u) func_8004C2B0(cpu);
        else func_8004C350(cpu);
        const uint32_t model = cpu->gpr[3];
        AppLog("[Race Model] lookup %s fn=%08X lr=%08X query=%08X mdl=%08X magic=%08X\n",
            trace->filename, target, caller, query, model, ReadTraceWord(model));
        return true;
    }
    case 0x80070D20u: // nw4r::g3d::ScnMdlSimple::Construct
    case 0x80073470u: { // nw4r::g3d::ScnMdl::Construct
        const uint32_t model = ReadTraceWord(cpu->gpr[5]);
        auto* trace = FindTracedModel(model);
        if (!trace) return false;
        const uint32_t caller = cpu->lr;
        if (target == 0x80070D20u) func_80070D20(cpu);
        else func_80073470(cpu);
        const uint32_t object = cpu->gpr[3];
        if (object) trace->objects.insert(object);
        AppLog("[Race Model] construct %s fn=%08X lr=%08X mdl=%08X heap=%08X object=%08X vtable=%08X\n",
            trace->filename, target, caller, model, channel, object, ReadTraceWord(object));
        return true;
    }
    case 0x8006EB60u: { // nw4r::g3d::ScnGroup::Insert
        const uint32_t object = cpu->gpr[5];
        for (const auto& trace : g_courseModelTrace) {
            if (!trace.objects.count(object)) continue;
            const uint32_t slot = cpu->gpr[4];
            const uint32_t caller = cpu->lr;
            func_8006EB60(cpu);
            AppLog("[Race Model] insert %s lr=%08X group=%08X slot=%u object=%08X result=%u\n",
                trace.filename, caller, channel, slot, object, cpu->gpr[3]);
            return true;
        }
        return false;
    }
    case 0x8004C5D0u: { // nw4r::g3d::ResFile::GetResTex(int index)
        func_8004C5D0(cpu);
        if (cpu->gpr[3] == 0) {
            // Missing texture fallback: provide dummy ResTexData descriptor so model UV calculation succeeds
            static const uint32_t kDummyTexGuestAddr = 0x80003200u;
            static bool s_initialized = false;
            if (!s_initialized) {
                s_initialized = true;
                Memory::Write32(kDummyTexGuestAddr + 0x00, 0x54455830); // 'TEX0'
                Memory::Write32(kDummyTexGuestAddr + 0x04, 64);
                Memory::Write16(kDummyTexGuestAddr + 0x1C, 64); // width = 64
                Memory::Write16(kDummyTexGuestAddr + 0x1E, 64); // height = 64
            }
            cpu->gpr[3] = kDummyTexGuestAddr;
        }
        return true;
    }
    case 0x8004C780u: // nw4r::g3d::ResFile::GetResAnmChr(int index)
    case 0x8004C7F0u: { // nw4r::g3d::ResFile::GetResAnmChr(const char* name)
        if (target == 0x8004C780u) func_8004C780(cpu);
        else func_8004C7F0(cpu);
        if (cpu->gpr[3] == 0) {
            // Missing skeletal animation fallback: provide dummy ResAnmChrData descriptor with frameCount = 1
            static const uint32_t kDummyChrGuestAddr = 0x80003280u;
            static bool s_initialized = false;
            if (!s_initialized) {
                s_initialized = true;
                Memory::Write32(kDummyChrGuestAddr + 0x00, 0x43485230); // 'CHR0'
                Memory::Write32(kDummyChrGuestAddr + 0x04, 64);
                Memory::Write16(kDummyChrGuestAddr + 0x20, 1); // offset 32: frameCount = 1
            }
            cpu->gpr[3] = kDummyChrGuestAddr;
        }
        return true;
    }
    case 0x8004C900u: { // nw4r::g3d::ResFile::GetResAnmClr(const char* name)
        func_8004C900(cpu);
        if (cpu->gpr[3] == 0) {
            // Missing color animation fallback: provide dummy ResAnmClrData descriptor with offset=0
            static const uint32_t kDummyClrGuestAddr = 0x80003240u;
            static bool s_initialized = false;
            if (!s_initialized) {
                s_initialized = true;
                Memory::Write32(kDummyClrGuestAddr + 0x00, 0x434C5230); // 'CLR0'
                Memory::Write32(kDummyClrGuestAddr + 0x04, 32);
                Memory::Write32(kDummyClrGuestAddr + 0x10, 0); // *(dummy + 16) = 0 (signals no anim node, sets r29 = 0)
            }
            cpu->gpr[3] = kDummyClrGuestAddr;
        }
        return true;
    }
    case 0x80015500u: { // __write_console
        const uint32_t buf = cpu->gpr[4];
        const uint32_t countPtr = cpu->gpr[5];
        if (buf != 0 && countPtr != 0 && Memory::Contains(countPtr, 4)) {
            const uint32_t count = Memory::Read32(countPtr);
            if (count > 0 && count < 4096 && Memory::Contains(buf, count)) {
                std::string str(count, '\0');
                for (size_t i = 0; i < count; ++i) str[i] = static_cast<char>(Memory::Read8(buf + i));
                AppLog("[Console] %s", str.c_str());
            }
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x800A35E0u: // nw4r::snd::SoundPlayer::StopAllSound
        return true;
    case 0x800A3020u: { // EGG::SoundHeap::Alloc fallback in mapped guest RAM
        const uint32_t size = cpu->gpr[4];
        uint32_t align = cpu->gpr[5];
        if (align < 32) align = 32;
        static uint32_t g_soundAlloc = 0x81200000u;
        g_soundAlloc = (g_soundAlloc + align - 1) & ~(align - 1);
        cpu->gpr[3] = g_soundAlloc;
        g_soundAlloc += size;
        AppLog("[SoundHeap] Alloc %u bytes at 0x%08X\n", size, cpu->gpr[3]);
        return true;
    }
    case 0x807BE030u: { // CollisionDirector::FindOctreeNode
        const uint32_t colData = cpu->gpr[3];
        if (colData == 0 || !Memory::Contains(colData, 60) || Memory::Read32(colData + 12) == 0) {
            cpu->gpr[3] = 0; // No octree mapped: return null node (no hit)
            return true;
        }
        return false; // Valid octree: proceed to translated function
    }
    case 0x80227B18u: { // EGG::Frustum::GetPerspectiveParams
        if (Memory::Contains(channel, 48)) {
            float s = gx_bridge::ReadGuestFloat(channel + 20);
            if (!std::isfinite(s) || s <= 0.001f || s > 1000.0f) {
                // Default fovY ~ 54 degrees -> cot(27 deg) = 1.9626105f
                gx_bridge::WriteGuestFloat(channel + 20, 1.9626105f);
            }
        }
        if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame && Memory::Contains(channel, 48)) {
            const uint32_t sda = cpu->gpr[13];
            AppLog("[GX Frustum] obj=%08X lr=%08X size=(%g,%g) scale=(%g,%g,%g) sdaScale=(%g,%g) screenScale=(%g,%g)\n",
                channel, cpu->lr, gx_bridge::ReadGuestFloat(channel + 8), gx_bridge::ReadGuestFloat(channel + 12),
                gx_bridge::ReadGuestFloat(channel + 20), gx_bridge::ReadGuestFloat(channel + 40),
                gx_bridge::ReadGuestFloat(channel + 44), gx_bridge::ReadGuestFloat(sda - 23976),
                gx_bridge::ReadGuestFloat(sda - 23984), gx_bridge::ReadGuestFloat(sda - 23768),
                gx_bridge::ReadGuestFloat(sda - 23776));
        }
        return false;
    }
    case 0x8006FAD0u: // nw4r::g3d::ScnRoot::GatherDrawScnObj
    case 0x8006FC20u: // nw4r::g3d::ScnRoot::DrawOpa
    case 0x8006FCA0u: { // nw4r::g3d::ScnRoot::DrawXlu
        if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame && Memory::Contains(channel, 244)) {
            const uint32_t gather = Memory::Read32(channel + 232);
            if (gather != 0 && Memory::Contains(gather, 24)) {
                AppLog("[GX Scene] fn=%08X root=%08X lr=%08X gather=%08X opaque=%u translucent=%u\n",
                    target, channel, cpu->lr, gather, Memory::Read32(gather + 16), Memory::Read32(gather + 20));
            }
        }
        return false;
    }
    case 0x800A3450u: { // nw4r::g3d::ScnRoot::CalcWorld
        const uint32_t scnRoot = cpu->gpr[3];
        if (scnRoot && Memory::Contains(scnRoot, 8)) {
            const uint32_t sentinel = scnRoot + 4;
            uint32_t curr = Memory::Read32(sentinel);
            if (curr == 0 || curr < 0x80000000) {
                Memory::Write32(sentinel, sentinel);
            } else {
                uint32_t prev = sentinel;
                int count = 0;
                while (curr != sentinel && count++ < 1000) {
                    if (curr == 0 || curr < 0x80000000 || !Memory::Contains(curr, 4)) {
                        Memory::Write32(prev, sentinel);
                        break;
                    }
                    prev = curr;
                    curr = Memory::Read32(curr);
                }
                if (count >= 1000) {
                    Memory::Write32(sentinel, sentinel);
                }
            }
        }
        return false;
    }
    case 0x807002BCu: // Audio::GroupBankRequester::ProcessRequests
        return true;
    case 0x8020F688u: // EGG::Allocator::__dt (empty blr)
        return true;
    case 0x802131E8u: { // EGG::SoundHeapMgr::CreateSoundHeap (stfs f0, 0x14(r3))
        if (cpu && cpu->gpr[3] != 0 && Memory::Contains(cpu->gpr[3] + 0x14, 4)) {
            const float val = static_cast<float>(cpu->fpr[0].d);
            uint32_t raw = 0;
            std::memcpy(&raw, &val, 4);
            Memory::Write32(cpu->gpr[3] + 0x14, raw);
        }
        return true;
    }
    case 0x801A2660u: { // OS::Panic
        const uint32_t file = cpu->gpr[3];
        const uint32_t line = cpu->gpr[4];
        const uint32_t msg = cpu->gpr[5];
        std::string fileStr = "unknown", msgStr = "unknown";
        if (file && Memory::Contains(file, 1)) fileStr = reinterpret_cast<const char*>(Memory::GetPointer(file, 1));
        if (msg && Memory::Contains(msg, 1)) msgStr = reinterpret_cast<const char*>(Memory::GetPointer(msg, 1));
        AppLog("[OSPanic] %s:%u: %s\n", fileStr.c_str(), line, msgStr.c_str());
        return true;
    }
    case 0x80168FA0u: // Desktop EXIInit skips hardware register setup.
        cpu->gpr[3] = 0;
        return true;
    case 0x801A00E0u: // Desktop skips exception vector setup.
        Memory::Write32(0x80386868u, 0x81800000u);
        cpu->gpr[3] = 0;
        return true;
    case 0x801A661Cu:
        Memory::Write32(0x803868F8u, 0x80003040u);
        Memory::Write32(0x800000C4u, 0);
        Memory::Write32(0x800000C8u, 0);
        cpu->gpr[3] = 0;
        return true;
    case 0x801AAD5Cu:
    case 0x801AAD7Cu: {
        using namespace std::chrono;
        static const auto epoch = steady_clock::now();
        const auto elapsed = duration_cast<nanoseconds>(steady_clock::now() - epoch);
        const uint64_t ticks = TimeBaseContract::NanosecondsToTicks(elapsed.count());
        cpu->gpr[3] = static_cast<uint32_t>(ticks >> 32);
        cpu->gpr[4] = static_cast<uint32_t>(ticks);
        return true;
    }
    case 0x801A65ACu: {
        cpu->gpr[3] = interruptsEnabled;
        interruptsEnabled = false;
        return true;
    }
    case 0x801A16E4u: { // DCZeroRange clears whole guest cache lines.
        if (cpu->gpr[4] == 0) return true;
        const auto start = channel & ~31u;
        const uint64_t length = (uint64_t{channel - start} + cpu->gpr[4] + 31u) & ~uint64_t{31};
        if (!Memory::Contains(start, static_cast<size_t>(length))) {
            AppLog("Unmapped DCZeroRange: %08lX size %llX\n",
                static_cast<unsigned long>(start), static_cast<unsigned long long>(length));
            return false;
        }
        std::memset(Memory::GetPointer(start, static_cast<size_t>(length)), 0, static_cast<size_t>(length));
        return true;
    }
    case 0x8019F33Cu: // Retail console profile for the startup harness.
        cpu->gpr[3] = 0x12u;
        return true;
    case 0x801A25D0u: { // OSReport
        char message[256]{};
        for (size_t i = 0; i + 1 < sizeof(message) && Memory::Contains(channel + i); ++i) {
            message[i] = static_cast<char>(Memory::Read8(channel + i));
            if (message[i] == 0) break;
        }
        if (std::strstr(message, "%s:%d Panic:")) {
            const uint32_t filePtr = cpu->gpr[4];
            const uint32_t line = cpu->gpr[5];
            std::string fileStr = "unknown";
            if (filePtr && Memory::Contains(filePtr, 1)) {
                fileStr = reinterpret_cast<const char*>(Memory::GetPointer(filePtr, 1));
            }
            AppLog("\n[Guest OSPanic] %s:%u\n", fileStr.c_str(), line);
        } else if (std::strncmp(message, "heap ", 5) == 0) {
            AppLog("Guest report: heap (%08X) free=%u -> alloc size=%u align=%u\n",
                cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7]);
        } else if (std::strstr(message, "cannot allocate")) {
            AppLog("Guest report: cannot allocate %lu bytes from heap %08lX\n",
                (unsigned long)cpu->gpr[4], (unsigned long)cpu->gpr[5]);
        } else {
            AppLog("Guest report: %s\n", message);
        }
        return true;
    }
    case 0x801A1358u:
        std::puts("Audio DSP deferred for headless startup");
        return true;
    case 0x8016B850u: { // GX::Init
        const uint32_t fifoBase = cpu->gpr[3];
        const uint32_t fifoSize = cpu->gpr[4];
        const uint32_t kFifoObjAddr = 0x80343740u;
        const uint32_t kGXDataAddr = 0x803437C0u;
        constexpr uint32_t kGXDataSize = 0x600u;
        const uint32_t kGXDataPtrAddr = 0x803886C8u;

        for (uint32_t offset = 0; offset < kGXDataSize; offset += 4) {
            Memory::Write32(kGXDataAddr + offset, 0);
        }
        Memory::Write8(kGXDataAddr + 0x5f8u, 0);
        Memory::Write8(kGXDataAddr + 0x5f9u, 1);
        Memory::Write8(kGXDataAddr + 0x5fau, 1);
        Memory::Write32(kGXDataAddr + 0x5e4u, 0);
        Memory::Write32(kGXDataAddr + 0x5e8u, 0);
        Memory::Write32(kGXDataAddr + 0x5fcu, 0);
        Memory::Write32(kGXDataPtrAddr, kGXDataAddr);

        constexpr uint32_t kGxCurrentThreadPtrAddr = 0x803867c4u;
        constexpr uint32_t kGxThreadQueueAddr = 0x803867c0u;
        constexpr uint32_t kCpuFifoObjAddr = 0x80343de4u;
        constexpr uint32_t kGpFifoObjAddr = 0x80343dc0u;
        constexpr size_t kFifoObjSize = 0x24u;
        constexpr uint32_t kFifoWrapFlagAddr = 0x803867b0u;
        constexpr uint32_t kFifoWrapFlag2Addr = 0x803867b1u;

        Memory::Write32(kGxCurrentThreadPtrAddr, Memory::Read32(0x800000E4u));
        Memory::Write32(kGxThreadQueueAddr, 0);
        for (size_t offset = 0; offset < kFifoObjSize; offset += 4) {
            Memory::Write32(kCpuFifoObjAddr + offset, 0);
            Memory::Write32(kGpFifoObjAddr + offset, 0);
        }
        Memory::Write8(kFifoWrapFlagAddr, 0);
        Memory::Write8(kFifoWrapFlag2Addr, 0);

        constexpr uint32_t kPeThreadQueueAddr = 0x803867d0u;
        Memory::Write32(kPeThreadQueueAddr, 0);
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            uint16_t cur = Memory::Read16(gd + 0x0Au);
            Memory::Write16(gd + 0x0Au, static_cast<uint16_t>(cur | 0x000Fu));
            Memory::Write32(gd + 0x254u, 0);
            Memory::Write32(gd + 0x174u, 0x0f0000ffu);
            Memory::Write32(gd + 0x7cu, 0x22000000u);
            Memory::Write32(gd + 0x170u, 0x27000000u);
            const uint32_t baseRegs[] = {0x30u, 0x38u};
            for (int i = 0; i < 2; ++i) {
                uint32_t r = baseRegs[i];
                Memory::Write32(gd + 0x108u + i * 0x10u, r << 24);
                Memory::Write32(gd + 0x128u + i * 0x10u, (r + 1) << 24);
                Memory::Write32(gd + 0x10cu + i * 0x10u, (r + 2) << 24);
                Memory::Write32(gd + 0x12cu + i * 0x10u, (r + 3) << 24);
                Memory::Write32(gd + 0x110u + i * 0x10u, (r + 4) << 24);
                Memory::Write32(gd + 0x130u + i * 0x10u, (r + 5) << 24);
                Memory::Write32(gd + 0x114u + i * 0x10u, (r + 6) << 24);
                Memory::Write32(gd + 0x134u + i * 0x10u, (r + 7) << 24);
            }
        }
        cpu->gpr[3] = kFifoObjAddr;
        return true;
    }
    case 0x801B94A4u: // VIInit
    case 0x801B9294u: { // __VIInit
        constexpr uint32_t kViInitializedFlagAddr = 0x80386b38u;
        constexpr uint32_t kViTimingGuardAddr = 0x80386b44u;
        constexpr uint32_t kViTvFormatAddr = 0x80386ba8u;
        constexpr uint32_t kViRenderWidthAddr = 0x80350864u;
        constexpr uint32_t kViRenderHeightAddr = 0x80350866u;
        constexpr uint32_t kViXfbWidthAddr = 0x80350872u;
        constexpr uint32_t kViXfbHeightAddr = 0x8035087cu;
        constexpr uint32_t kViRetraceCountAddr = 0x80386be4u;
        constexpr uint32_t kViPreRetraceCallback = 0x80386bb8u;
        constexpr uint32_t kViPostRetraceCallback = 0x80386bb4u;
        constexpr uint32_t kViNextFrameBufferAddr = 0x80386ba0u;
        constexpr uint32_t kViNextFrameBufferHwAddr = 0x80350890u;
        constexpr uint32_t kViRetraceQueueAddr = 0x80386bc0u;

        Memory::Write8(kViInitializedFlagAddr, 1);
        Memory::Write8(kViTimingGuardAddr, 1);
        Memory::Write32(kViTvFormatAddr, 1); // PAL
        Memory::Write16(kViRenderWidthAddr, 640);
        Memory::Write16(kViRenderHeightAddr, 480);
        Memory::Write16(kViXfbWidthAddr, 640);
        Memory::Write16(kViXfbHeightAddr, 480);
        Memory::Write32(kViRetraceCountAddr, 0);
        Memory::Write32(kViPreRetraceCallback, 0);
        Memory::Write32(kViPostRetraceCallback, 0);
        Memory::Write32(kViNextFrameBufferAddr, 0);
        Memory::Write32(kViNextFrameBufferHwAddr, 0);
        Memory::Write32(kViRetraceQueueAddr, 0);
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801B9934u:
    case 0x801B993Cu:
    case 0x801B9944u:
    case 0x801BAD38u: // VIGetDTVStatus
    case 0x801B9F6Cu: // VIConfigure
    case 0x801BA9A4u: // VIFlush
    case 0x801BAB2Cu: // VISetBlack
        cpu->gpr[3] = 0;
        return true;
    case 0x801BAAB8u: // VISetNextFrameBuffer
        Memory::Write32(0x80386ba0u, cpu->gpr[3]);
        Memory::Write32(0x80350890u, cpu->gpr[3]);
        cpu->gpr[3] = 0;
        return true;
    case 0x801B90F4u: { // VISetPreRetraceCallback
        const uint32_t prev = Memory::Read32(0x80386bb8u);
        Memory::Write32(0x80386bb8u, cpu->gpr[3]);
        cpu->gpr[3] = prev;
        return true;
    }
    case 0x801B9138u: { // VISetPostRetraceCallback
        const uint32_t prev = Memory::Read32(0x80386bb4u);
        Memory::Write32(0x80386bb4u, cpu->gpr[3]);
        cpu->gpr[3] = prev;
        return true;
    }
    case 0x801BABA4u: { // VIGetRetraceCount
        static uint32_t s_retrace = 1;
        static uint32_t s_callCount = 0;
        if (++s_callCount % 8 == 0) ++s_retrace;
        cpu->gpr[3] = s_retrace;
        return true;
    }
    case 0x80022480u: // nw4r::db::DirectPrint_SetupFB
    case 0x80022AB0u: // nw4r::db::Console_DrawDirect
    case 0x80022900u: // nw4r::db::DoDrawConsole_
    case 0x800220E0u: // nw4r::db::DrawStringLineToXfb
    case 0x800221C0u: // nw4r::db::DrawCharToXfb
        cpu->gpr[3] = 0;
        return true;
    case 0x801B99ECu: { // VIWaitForRetrace
        const uint32_t count = Memory::Read32(0x80386be4u) + 1;
        Memory::Write32(0x80386be4u, count);
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801B1CACu: // SCGetEuRgb60Mode
        cpu->gpr[3] = 1;
        return true;
    case 0x801B1BE4u: // SCGetAspectRatio
        cpu->gpr[3] = 0; // 4:3
        return true;
    case 0x801B1D0Cu: // SCGetLanguage
        cpu->gpr[3] = 1; // English
        return true;
    case 0x801B1D84u: // SCGetProgressiveMode
        cpu->gpr[3] = 0;
        return true;
    case 0x801B1DD8u: // SCGetScreenSaverMode
        cpu->gpr[3] = 0;
        return true;
    case 0x801B2074u: // SCGetSimpleAddressData
    case 0x801B2130u: // SCGetNetContentRestrictions
    case 0x801B216Cu: // SCGetEULA
    case 0x801B21A8u: // SCGetWCFlags
    case 0x801B21E4u: // SCGetFreeChannelAppCount
    case 0x801B1F68u: // SCGetWpadMotorMode
    case 0x801B1FC4u: // SCGetWpadSensorBarPosition
    case 0x801B2018u: // SCGetWpadSpeakerVolume
        cpu->gpr[3] = 0;
        return true;
    case 0x800847C0u: { // nw4r::lyt::detail::DrawQuad (6 args)
        gx_bridge::DrawLytQuad2D(cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], 255, target, cpu->lr);
        return true;
    }
    case 0x80084D20u: { // nw4r::lyt::detail::DrawQuad (with extra alpha in r8)
        gx_bridge::DrawLytQuad2D(cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], static_cast<uint8_t>(cpu->gpr[8] & 0xFFu), target, cpu->lr);
        return true;
    }
    case 0x8021B344u: // EGG::DrawGX::SetVtxState
    case 0x8021B688u:
        gx_bridge::SetEggVtxState(cpu->gpr[3]);
        return true;
    case 0x8016F0F0u: { // GXBegin(type, vtxfmt, nverts)
        uint8_t primType = static_cast<uint8_t>(cpu->gpr[3] & 0xF8u);
        uint8_t vatFmt   = static_cast<uint8_t>(cpu->gpr[4] & 0x07u);
        uint16_t nverts  = static_cast<uint16_t>(cpu->gpr[5]);
        uint8_t cmdByte  = primType | vatFmt;
        GX_HLE_FIFO_Write8(cmdByte);
        GX_HLE_FIFO_Write16(nverts);
        return true;
    }
    case 0x8016F1B0u: { // GXEnd()
        return true;
    }
    case 0x8016F438u: { // GXSetDispCopySrc(left, top, wd, ht)
        if (cpu->gpr[5] >= 320 && cpu->gpr[6] >= 200) {
            gx_bridge::g_gx.efbWidth = cpu->gpr[5];
            gx_bridge::g_gx.efbHeight = cpu->gpr[6];
            gx_bridge::UpdateScissorScreenRect();
        }
        return true;
    }
    case 0x8016DC34u: { // GXClearVtxDesc
        std::memset(gx_bridge::g_gx.vtxDesc, 0, sizeof(gx_bridge::g_gx.vtxDesc));
        return true;
    }
    case 0x8016D3A4u: { // GXSetVtxDesc(attr, type)
        if (cpu->gpr[3] < 21) {
            gx_bridge::g_gx.vtxDesc[cpu->gpr[3]] = cpu->gpr[4];
        }
        return true;
    }
    case 0x8016D608u: { // GXSetVtxDescv(attrPtr)
        uint32_t ptr = cpu->gpr[3];
        if (ptr != 0) {
            for (int i = 0; i < 32 && Memory::Contains(ptr, 8); ++i, ptr += 8) {
                uint32_t attr = Memory::Read32(ptr);
                uint32_t type = Memory::Read32(ptr + 4);
                if (attr == 0xFFu || attr >= 21) break;
                gx_bridge::g_gx.vtxDesc[attr] = type;
            }
        }
        return true;
    }
    case 0x8016DC68u: { // GXSetVtxAttrFmt(vtxfmt, attr, cnt, type, frac)
        uint32_t fmtIdx = cpu->gpr[3] & 7u;
        uint32_t attr = cpu->gpr[4];
        if (attr < 21) {
            gx_bridge::g_gx.vtxFmt[fmtIdx][attr].cnt  = cpu->gpr[5];
            gx_bridge::g_gx.vtxFmt[fmtIdx][attr].type = cpu->gpr[6];
            gx_bridge::g_gx.vtxFmt[fmtIdx][attr].frac = static_cast<uint8_t>(cpu->gpr[7] & 0xFFu);
        }
        return true;
    }
    case 0x8016DE08u: { // GXSetVtxAttrFmtv(vtxfmt, listPtr)
        uint32_t fmtIdx = cpu->gpr[3] & 7u;
        uint32_t ptr = cpu->gpr[4];
        if (ptr != 0) {
            for (int i = 0; i < 32 && Memory::Contains(ptr, 16); ++i, ptr += 16) {
                uint32_t attr = Memory::Read32(ptr);
                if (attr == 0xFFu || attr >= 21) break;
                gx_bridge::g_gx.vtxFmt[fmtIdx][attr].cnt  = Memory::Read32(ptr + 4);
                gx_bridge::g_gx.vtxFmt[fmtIdx][attr].type = Memory::Read32(ptr + 8);
                gx_bridge::g_gx.vtxFmt[fmtIdx][attr].frac = static_cast<uint8_t>(Memory::Read8(ptr + 12));
            }
        }
        return true;
    }
    case 0x8016E32Cu: { // GXSetArray(attr, base_ptr, stride)
        uint32_t attr = cpu->gpr[3];
        if (attr < 25) {
            gx_bridge::g_gx.vtxArray[attr].base = cpu->gpr[4];
            gx_bridge::g_gx.vtxArray[attr].stride = static_cast<uint8_t>(cpu->gpr[5] & 0xFFu);
        }
        return true;
    }
    case 0x8017301Cu: { // GXSetProjection(mtxAddr, type)
        uint32_t mtxAddr = cpu->gpr[3];
        gx_bridge::g_gx.projType = (cpu->gpr[4] != 0) ? 1 : 0;
        if (mtxAddr != 0 && Memory::Contains(mtxAddr, 64)) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    gx_bridge::g_gx.proj[r][c] = gx_bridge::ReadGuestFloat(mtxAddr + (r * 4 + c) * 4);
                }
            }
            if (gx_bridge::g_gx.projType == 0) {
                gx_bridge::SanitizePerspectiveProjection();
            }
            gx_bridge::TraceProjection("GXSetProjection", mtxAddr, cpu);
        }
        return true;
    }
    case 0x80173080u: { // GXSetProjectionv(ptr)
        uint32_t ptr = cpu->gpr[3];
        if (ptr != 0 && Memory::Contains(ptr, 28)) {
            float typeF = gx_bridge::ReadGuestFloat(ptr);
            gx_bridge::g_gx.xfProjType = (typeF != 0.0f) ? 1 : 0;
            for (int i = 0; i < 6; ++i) {
                gx_bridge::g_gx.xfProj[i] = gx_bridge::ReadGuestFloat(ptr + 4 + i * 4);
            }
            gx_bridge::RebuildProjFromXf();
            gx_bridge::TraceProjection("GXSetProjectionv", ptr, cpu);
        }
        return true;
    }
    case 0x8017310Cu: { // GXLoadPosMtxImm(mtxPtr, id)
        uint32_t mtxPtr = cpu->gpr[3];
        uint32_t id = cpu->gpr[4];
        uint32_t idx = (id / 3u) % 10u;
        if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame) {
            AppLog("[GXLoadPosMtxImm] ptr=%08X id=%u idx=%u lr=%08X\n", mtxPtr, id, idx, cpu->lr);
        }
        if (mtxPtr != 0 && Memory::Contains(mtxPtr, 48)) {
            for (int i = 0; i < 12; ++i) {
                gx_bridge::g_gx.pnMtx[idx][i] = gx_bridge::ReadGuestFloat(mtxPtr + i * 4);
            }
            gx_bridge::SanitizeModelViewMatrix(gx_bridge::g_gx.pnMtx[idx]);
            if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame) {
                const auto& m = gx_bridge::g_gx.pnMtx[idx];
                AppLog("[GXLoadPosMtxImm] val r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
                    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
            }
        }
        return true;
    }
    case 0x8017315Cu: { // GXLoadPosMtxIndx(mtx_indx, id)
        uint32_t arrayIdx = cpu->gpr[3] & 0xFFFFu;
        uint32_t id = cpu->gpr[4];
        uint32_t dstIdx = (id / 3u) % 10u;
        uint32_t baseAddr = gx_bridge::g_gx.vtxArray[21].base;
        uint32_t stride = gx_bridge::g_gx.vtxArray[21].stride;
        if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame) {
            AppLog("[GXLoadPosMtxIndx] arrayIdx=%u id=%u dstIdx=%u base=%08X stride=%u lr=%08X\n",
                arrayIdx, id, dstIdx, baseAddr, stride, cpu->lr);
        }
        if (baseAddr != 0 && stride != 0) {
            uint32_t srcAddr = baseAddr + arrayIdx * stride;
            if (Memory::Contains(srcAddr, 48)) {
                for (int i = 0; i < 12; ++i) {
                    gx_bridge::g_gx.pnMtx[dstIdx][i] = gx_bridge::ReadGuestFloat(srcAddr + i * 4);
                }
                gx_bridge::SanitizeModelViewMatrix(gx_bridge::g_gx.pnMtx[dstIdx]);
                if (gx_bridge::g_gxFrame == gx_bridge::kGxTraceFrame) {
                    const auto& m = gx_bridge::g_gx.pnMtx[dstIdx];
                    AppLog("[GXLoadPosMtxIndx] val r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
                        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
                }
            }
        }
        return true;
    }
    case 0x80173214u: { // GXSetCurrentMtx(id)
        gx_bridge::g_gx.currentPnMtx = (cpu->gpr[3] / 3u) % 10u;
        return true;
    }
    case 0x80173378u: // GXSetViewportJitter
    case 0x801733B4u: { // GXSetViewport
        gx_bridge::g_gx.vpLeft   = static_cast<float>(cpu->fpr[1].d);
        gx_bridge::g_gx.vpTop    = static_cast<float>(cpu->fpr[2].d);
        gx_bridge::g_gx.vpWidth  = static_cast<float>(cpu->fpr[3].d);
        gx_bridge::g_gx.vpHeight = static_cast<float>(cpu->fpr[4].d);
        gx_bridge::g_gx.vpNear   = static_cast<float>(cpu->fpr[5].d);
        gx_bridge::g_gx.vpFar    = static_cast<float>(cpu->fpr[6].d);
        return true;
    }
    case 0x8017277Cu: { // GXSetBlendMode(type, src_factor, dst_factor, op)
        gx_bridge::g_gx.blendMode = cpu->gpr[3];
        gx_bridge::g_gx.blendSrc  = cpu->gpr[4];
        gx_bridge::g_gx.blendDst  = cpu->gpr[5];
        return true;
    }
    case 0x80172824u: { // GXSetZMode(compare_enable, func, update_enable)
        gx_bridge::g_gx.depthCompare = (cpu->gpr[3] != 0);
        gx_bridge::g_gx.depthFunc    = cpu->gpr[4];
        gx_bridge::g_gx.depthUpdate  = (cpu->gpr[5] != 0);
        return true;
    }
    case 0x8016E164u: { // GXSetCullMode(mode)
        gx_bridge::g_gx.cullMode = cpu->gpr[3];
        return true;
    }
    case 0x80171E10u: { // GXSetTevColor(id, colorAddr)
        uint32_t id = cpu->gpr[3] & 3u;
        uint32_t addr = cpu->gpr[4];
        if (addr != 0 && Memory::Contains(addr, 4)) {
            gx_bridge::g_gx.tevColor[id][0] = Memory::Read8(addr);
            gx_bridge::g_gx.tevColor[id][1] = Memory::Read8(addr + 1);
            gx_bridge::g_gx.tevColor[id][2] = Memory::Read8(addr + 2);
            gx_bridge::g_gx.tevColor[id][3] = Memory::Read8(addr + 3);
        }
        return true;
    }
    case 0x80171E70u: { // GXSetTevColorS10(id, colorAddr)
        uint32_t id = cpu->gpr[3] & 3u;
        uint32_t addr = cpu->gpr[4];
        if (addr != 0 && Memory::Contains(addr, 8)) {
            auto clampU8 = [](int16_t v) -> uint8_t { return static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, v))); };
            gx_bridge::g_gx.tevColor[id][0] = clampU8(static_cast<int16_t>(Memory::Read16(addr)));
            gx_bridge::g_gx.tevColor[id][1] = clampU8(static_cast<int16_t>(Memory::Read16(addr + 2)));
            gx_bridge::g_gx.tevColor[id][2] = clampU8(static_cast<int16_t>(Memory::Read16(addr + 4)));
            gx_bridge::g_gx.tevColor[id][3] = clampU8(static_cast<int16_t>(Memory::Read16(addr + 6)));
        }
        return true;
    }
    case 0x80171ED4u: { // GXSetTevKColor(id, colorAddr)
        uint32_t id = cpu->gpr[3] & 3u;
        uint32_t addr = cpu->gpr[4];
        if (addr != 0 && Memory::Contains(addr, 4)) {
            gx_bridge::g_gx.tevKColor[id][0] = Memory::Read8(addr);
            gx_bridge::g_gx.tevKColor[id][1] = Memory::Read8(addr + 1);
            gx_bridge::g_gx.tevKColor[id][2] = Memory::Read8(addr + 2);
            gx_bridge::g_gx.tevKColor[id][3] = Memory::Read8(addr + 3);
        }
        return true;
    }
    case 0x8017214Cu: { // GXSetTevOrder(stage, coord, map, color)
        uint32_t stage = cpu->gpr[3];
        uint32_t map = cpu->gpr[5];
        if (stage < 16) {
            gx_bridge::g_gx.stageTexMap[stage] = static_cast<uint8_t>(map & 7u);
            gx_bridge::g_gx.texStageEnabled[stage] = (map < 8);
        }
        return true;
    }
    case 0x8017039Cu: { // GXSetChanAmbColor(chan, colorAddr)
        uint32_t ch = cpu->gpr[3] & 1u;
        uint32_t addr = cpu->gpr[4];
        if (addr != 0 && Memory::Contains(addr, 4)) {
            for (int i = 0; i < 4; ++i) gx_bridge::g_gx.chanAmbColor[ch][i] = Memory::Read8(addr + i);
        }
        return true;
    }
    case 0x80170474u: { // GXSetChanMatColor(chan, colorAddr)
        uint32_t ch = cpu->gpr[3] & 1u;
        uint32_t addr = cpu->gpr[4];
        if (addr != 0 && Memory::Contains(addr, 4)) {
            for (int i = 0; i < 4; ++i) gx_bridge::g_gx.chanMatColor[ch][i] = Memory::Read8(addr + i);
        }
        return true;
    }
    case 0x801707F8u: { // GXInitTexObj(obj, image_ptr, width, height, format, wrap_s, wrap_t, mipmap)
        uint32_t obj = cpu->gpr[3];
        if (obj != 0 && Memory::Contains(obj, 32)) {
            uint32_t imagePtr = cpu->gpr[4];
            uint16_t width    = static_cast<uint16_t>(cpu->gpr[5]);
            uint16_t height   = static_cast<uint16_t>(cpu->gpr[6]);
            uint32_t format   = cpu->gpr[7];
            uint32_t wrapS    = cpu->gpr[8];
            uint32_t wrapT    = cpu->gpr[9];
            uint32_t mode0    = (wrapS & 3u) | ((wrapT & 3u) << 2);
            uint32_t image0   = ((width - 1u) & 0x3FFu) | (((height - 1u) & 0x3FFu) << 10) | ((format & 0xFu) << 20);
            uint32_t image3   = (imagePtr & 0x1FFFFFFFu) >> 5;
            Memory::Write32(obj + 0,  mode0);
            Memory::Write32(obj + 8,  image0);
            Memory::Write32(obj + 12, imagePtr);
            Memory::Write32(obj + 20, format);
            Memory::Write16(obj + 24, width);
            Memory::Write16(obj + 26, height);
            Memory::Write8(obj + 28,  static_cast<uint8_t>(wrapS));
            Memory::Write8(obj + 29,  static_cast<uint8_t>(wrapT));
            Memory::Write8(obj + 30,  static_cast<uint8_t>(format));
            (void)image3;
        }
        return true;
    }
    case 0x80170A38u: { // GXInitTexObjCI(obj, image_ptr, width, height, format, wrap_s, wrap_t, mipmap, tlut_name)
        uint32_t obj = cpu->gpr[3];
        if (obj != 0 && Memory::Contains(obj, 32)) {
            uint32_t imagePtr = cpu->gpr[4];
            uint16_t width    = static_cast<uint16_t>(cpu->gpr[5]);
            uint16_t height   = static_cast<uint16_t>(cpu->gpr[6]);
            uint32_t format   = cpu->gpr[7];
            uint32_t wrapS    = cpu->gpr[8];
            uint32_t wrapT    = cpu->gpr[9];
            uint32_t mode0    = (wrapS & 3u) | ((wrapT & 3u) << 2);
            uint32_t image0   = ((width - 1u) & 0x3FFu) | (((height - 1u) & 0x3FFu) << 10) | ((format & 0xFu) << 20);
            Memory::Write32(obj + 0,  mode0);
            Memory::Write32(obj + 8,  image0);
            Memory::Write32(obj + 12, imagePtr);
            Memory::Write32(obj + 20, format);
            Memory::Write16(obj + 24, width);
            Memory::Write16(obj + 26, height);
            Memory::Write8(obj + 28,  static_cast<uint8_t>(wrapS));
            Memory::Write8(obj + 29,  static_cast<uint8_t>(wrapT));
            Memory::Write8(obj + 30,  static_cast<uint8_t>(format));
        }
        return true;
    }
    case 0x80170AA8u: { // GXInitTexObjLOD
        return true;
    }
    case 0x80170F2Cu: { // GXLoadTexObj(obj, id)
        uint32_t obj = cpu->gpr[3];
        uint32_t id  = cpu->gpr[4] & 7u;
        if (obj != 0 && Memory::Contains(obj, 32)) {
            auto& slot = gx_bridge::g_gx.texSlots[id];
            uint32_t mode0  = Memory::Read32(obj + 0);
            uint32_t image0 = Memory::Read32(obj + 8);
            uint32_t ptr12  = Memory::Read32(obj + 12);
            uint16_t w24    = Memory::Read16(obj + 24);
            uint16_t h26    = Memory::Read16(obj + 26);
            if (w24 > 0 && h26 > 0 && ptr12 != 0) {
                slot.dataAddr = ptr12;
                slot.width    = w24;
                slot.height   = h26;
                slot.wrapS    = Memory::Read8(obj + 28);
                slot.wrapT    = Memory::Read8(obj + 29);
                slot.format   = Memory::Read8(obj + 30);
                slot.valid    = true;
            } else {
                slot.wrapS    = static_cast<uint8_t>(mode0 & 3u);
                slot.wrapT    = static_cast<uint8_t>((mode0 >> 2) & 3u);
                slot.width    = static_cast<uint16_t>((image0 & 0x3FFu) + 1u);
                slot.height   = static_cast<uint16_t>(((image0 >> 10) & 0x3FFu) + 1u);
                slot.format   = static_cast<uint8_t>((image0 >> 20) & 0xFu);
                slot.dataAddr = (ptr12 & 0x00FFFFFFu) << 5;
                slot.valid    = (slot.dataAddr != 0 && slot.width > 0 && slot.height > 0);
            }
            gx_bridge::g_gx.activeTexSlot = static_cast<int>(id);
        }
        return true;
    }
    case 0x80172E00u: { // GXBeginDisplayList(list, size)
        gx_bridge::g_gx.inDisplayList = true;
        gx_bridge::g_gx.dlBaseAddr = cpu->gpr[3];
        gx_bridge::g_gx.dlMaxSize = cpu->gpr[4];
        gx_bridge::g_gx.dlWriteOffset = 0;
        return true;
    }
    case 0x80172EB4u: { // GXEndDisplayList()
        uint32_t written = gx_bridge::g_gx.dlWriteOffset;
        while ((written & 31u) != 0 && written < gx_bridge::g_gx.dlMaxSize) {
            Memory::Write8(gx_bridge::g_gx.dlBaseAddr + written, 0);
            ++written;
        }
        gx_bridge::g_gx.inDisplayList = false;
        cpu->gpr[3] = written;
        return true;
    }
    case 0x80172F64u: { // GXCallDisplayList(list, nbytes)
        if (gx_bridge::g_gxFrame == 279u) {
            static uint32_t s_dlLogs = 0;
            if (s_dlLogs < 12 && gx_bridge::g_gx.currentPnMtx != 0) {
                ++s_dlLogs;
                const float* M = gx_bridge::g_gx.pnMtx[gx_bridge::g_gx.currentPnMtx];
                AppLog("[DL] n=%u list=%08X len=%u lr=%08X pn=%u mvT=(%.1f,%.1f,%.1f)\n",
                    s_dlLogs, cpu->gpr[3], cpu->gpr[4], cpu->lr,
                    gx_bridge::g_gx.currentPnMtx, M[3], M[7], M[11]);
            }
        }
        gx_bridge::ParseDisplayList(cpu->gpr[3], cpu->gpr[4], 0);
        return true;
    }
    case 0x80173430u: { // GX::SetScissor(x, y, w, h)
        gx_bridge::g_gx.scissorX = cpu->gpr[3];
        gx_bridge::g_gx.scissorY = cpu->gpr[4];
        gx_bridge::g_gx.scissorW = cpu->gpr[5];
        gx_bridge::g_gx.scissorH = cpu->gpr[6];
        gx_bridge::g_gx.scissorSet = (gx_bridge::g_gx.scissorW != 0 && gx_bridge::g_gx.scissorH != 0);
        gx_bridge::UpdateScissorScreenRect();
        static uint32_t s_scissorLogs = 0;
        if (gx_bridge::g_gxFrame >= 80 && gx_bridge::g_gxFrame <= 110 && s_scissorLogs < 16) {
            ++s_scissorLogs;
            AppLog("[MenuScissor] frame=%u box=(%u,%u %ux%u) efb=%ux%u screen=(%d,%d)-(%d,%d) lr=%08X\n",
                gx_bridge::g_gxFrame, gx_bridge::g_gx.scissorX, gx_bridge::g_gx.scissorY,
                gx_bridge::g_gx.scissorW, gx_bridge::g_gx.scissorH,
                gx_bridge::g_gx.efbWidth, gx_bridge::g_gx.efbHeight,
                gx_bridge::g_gx.clipX0, gx_bridge::g_gx.clipY0,
                gx_bridge::g_gx.clipX1, gx_bridge::g_gx.clipY1, cpu->lr);
        }
        return true;
    }
    case 0x8016E37Cu: { // GX::SetTexCoordGen2
        static uint32_t s_logged = 0;
        if (gx_bridge::g_gxFrame >= 80 && gx_bridge::g_gxFrame <= 110 && s_logged < 24) {
            ++s_logged;
            AppLog("[MenuTexGen] frame=%u coord=%u type=%u src=%u mtx=%u normalize=%u post=%u lr=%08X\n",
                gx_bridge::g_gxFrame, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6],
                cpu->gpr[7], cpu->gpr[8], cpu->lr);
        }
        return true;
    }
    case 0x80173234u: { // GX::LoadTexMtxImm
        static uint32_t s_logged = 0;
        if (gx_bridge::g_gxFrame >= 80 && gx_bridge::g_gxFrame <= 110 && s_logged < 24) {
            ++s_logged;
            const uint32_t addr = cpu->gpr[3];
            if (addr && Memory::Contains(addr, 48)) {
                AppLog("[MenuTexMtx] frame=%u addr=%08X id=%u type=%u lr=%08X rows=(%.4g,%.4g,%.4g,%.4g)(%.4g,%.4g,%.4g,%.4g)(%.4g,%.4g,%.4g,%.4g)\n",
                    gx_bridge::g_gxFrame, addr, cpu->gpr[4], cpu->gpr[5], cpu->lr,
                    gx_bridge::ReadGuestFloat(addr), gx_bridge::ReadGuestFloat(addr + 4),
                    gx_bridge::ReadGuestFloat(addr + 8), gx_bridge::ReadGuestFloat(addr + 12),
                    gx_bridge::ReadGuestFloat(addr + 16), gx_bridge::ReadGuestFloat(addr + 20),
                    gx_bridge::ReadGuestFloat(addr + 24), gx_bridge::ReadGuestFloat(addr + 28),
                    gx_bridge::ReadGuestFloat(addr + 32), gx_bridge::ReadGuestFloat(addr + 36),
                    gx_bridge::ReadGuestFloat(addr + 40), gx_bridge::ReadGuestFloat(addr + 44));
            } else {
                AppLog("[MenuTexMtx] frame=%u invalid addr=%08X id=%u type=%u lr=%08X\n",
                    gx_bridge::g_gxFrame, addr, cpu->gpr[4], cpu->gpr[5], cpu->lr);
            }
        }
        return true;
    }
    case 0x80060AE0u: // nw4r::g3d::tmem::SetTMemLayout
    case 0x8016F4B8u: // GXSetDispCopyDst
    case 0x8016F478u: // GXSetTexCopySrc
    case 0x8016F4DCu: // GXSetTexCopyDst
    case 0x8016FA40u: // GXSetCopyFilter
    case 0x8016FC24u: // GXSetDispCopyGamma
    case 0x8016FC38u: // GXCopyDisp
    case 0x8016FD74u: // GXCopyTex
    case 0x8016F5F8u: // GXSetDispCopyFrame2Field
    case 0x8016F618u: // GXSetCopyClamp
    case 0x8016FECCu: // GXClearBoundingBox
    case 0x8016EE78u: // GXSetDirtyState
    case 0x8016E654u: // GXFlush
    case 0x8016E6B0u: // GXResetWriteGatherPipe
    case 0x8016EAB0u: // GX::DrawDone
    case 0x8016E9FCu: // GX::SetDrawSync
    case 0x8016EB70u: // GX::PixModeSync
    case 0x8016E848u: // GX::AbortFrame
    case 0x8016E36Cu: // GX::InvalidateVtxCache
    case 0x8016E5A4u: // GX::SetNumTexGens
    case 0x8016E5C8u: // GX::SetMisc
    case 0x8016EB94u: // GX::PokeAlphaMode
    case 0x8016EBA4u: // GX::PokeAlphaUpdate
    case 0x8016EBBCu: // GX::PokeAlphaUpdate
    case 0x8016EBD0u: // GX::PokeBlendMode
    case 0x8016EC2Cu: // GX::PokeColorUpdate
    case 0x8016EC40u: // GX::PokeDstAlpha
    case 0x8016EC58u: // GX::PokeDither
    case 0x8016EC6Cu: // GX::PokeZMode
    case 0x8016EC88u: // GX::SetDrawSyncCallback
    case 0x8016ED50u: // GX::SetDrawDoneCallback
    case 0x8016ED94u: // GX::FinishInterruptHandler
    case 0x80173188u: // GX::LoadNrmMtxImm
    case 0x801731E0u: // GX::LoadNrmMtxIndx3x3
    case 0x80173400u: // GX::SetZScaleOffset
    case 0x801734E0u: // GX::SetScissorBoxOffset
    case 0x8017351Cu: // GX::SetClipMode
    case 0x801735CCu: // GX::SetGPMetric
    case 0x80173DF8u: // GX::ClearGPMetric
    case 0x801727CCu: // GX::SetColorUpdate
    case 0x801727F8u: // GX::SetAlphaUpdate
    case 0x80172858u: // GX::SetZCompLoc
    case 0x80172888u: // GX::SetPixelFmt
    case 0x80172930u: // GX::SetDither
    case 0x8017295Cu: // GX::SetDstAlpha
    case 0x801773D0u: // HBM::Create
    case 0x80177908u: // homebutton::__ct::Create
    case 0x80177520u: // HBM::Init
    case 0x80177554u: // HBM::Calc
    case 0x801775A4u: // HBM::Draw
    case 0x80177740u: // HBM::CreateSound
    case 0x80177744u: // HBM::UpdateSound
        cpu->gpr[3] = 0;
        return true;
    case 0x8017298Cu: // GX::SetFieldMask
    case 0x801729C0u: // GX::SetFieldMode
    case 0x801717ACu: // GX::SetTevIndirect
    case 0x80171814u: // GX::SetIndTexMtx
    case 0x80171968u: // GX::SetIndTexCoordScale
    case 0x80171A6Cu: // GX::SetIndTexOrder
    case 0x80171B38u: // GX::SetNumIndStages
    case 0x80171B58u: // GX::SetTevDirect
    case 0x80171BA0u: // GX::SetTevIndWarp
        return true;
    case 0x801722A8u: { // GX::SetNumTevStages(n)
        gx_bridge::g_gx.tevNumStages = std::max<uint32_t>(1u, std::min<uint32_t>(cpu->gpr[3], 16u));
        return true;
    }
    case 0x80171C4Cu: { // GX::SetTevOp(stage, mode) - expand into explicit color/alpha in/op
        uint32_t s = cpu->gpr[3] & 15u;
        uint32_t mode = cpu->gpr[4];
        // GXTevMode: MODULATE=0, DECAL=1, BLEND=2, REPLACE=3, PASSCLR=4. Mirrors aurora's
        // GXSetTevOp expansion exactly (stage 0 reads RASC/RASA, later stages read CPREV/APREV).
        const uint32_t inColor = (s == 0) ? 10u : 0u;  // RASC or CPREV
        const uint32_t inAlpha = (s == 0) ? 5u : 0u;   // RASA or APREV
        auto setIn = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            gx_bridge::g_gx.tevColorIn[s][0] = static_cast<uint8_t>(a);
            gx_bridge::g_gx.tevColorIn[s][1] = static_cast<uint8_t>(b);
            gx_bridge::g_gx.tevColorIn[s][2] = static_cast<uint8_t>(c);
            gx_bridge::g_gx.tevColorIn[s][3] = static_cast<uint8_t>(d);
        };
        auto setAIn = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            gx_bridge::g_gx.tevAlphaIn[s][0] = static_cast<uint8_t>(a);
            gx_bridge::g_gx.tevAlphaIn[s][1] = static_cast<uint8_t>(b);
            gx_bridge::g_gx.tevAlphaIn[s][2] = static_cast<uint8_t>(c);
            gx_bridge::g_gx.tevAlphaIn[s][3] = static_cast<uint8_t>(d);
        };
        switch (mode) {
        case 1: // DECAL
            setIn(inColor, 8, 9, 15);
            setAIn(7, 7, 7, inAlpha);
            break;
        case 2: // BLEND
            setIn(inColor, 12, 8, 15);
            setAIn(7, 4, inAlpha, 7);
            break;
        case 3: // REPLACE
            setIn(15, 15, 15, 8);
            setAIn(7, 7, 7, 4);
            break;
        case 4: // PASSCLR
            setIn(15, 15, 15, inColor);
            setAIn(7, 7, 7, inAlpha);
            break;
        default: // MODULATE
            setIn(15, 8, inColor, 15);
            setAIn(7, 4, inAlpha, 7);
            break;
        }
        gx_bridge::g_gx.tevColorOp[s] = 0;
        gx_bridge::g_gx.tevAlphaOp[s] = 0;
        return true;
    }
    case 0x80171CE0u: { // GX::SetTevColorIn(stage,a,b,c,d)
        uint32_t s = cpu->gpr[3] & 15u;
        gx_bridge::g_gx.tevColorIn[s][0] = static_cast<uint8_t>(cpu->gpr[4] & 15u);
        gx_bridge::g_gx.tevColorIn[s][1] = static_cast<uint8_t>(cpu->gpr[5] & 15u);
        gx_bridge::g_gx.tevColorIn[s][2] = static_cast<uint8_t>(cpu->gpr[6] & 15u);
        gx_bridge::g_gx.tevColorIn[s][3] = static_cast<uint8_t>(cpu->gpr[7] & 15u);
        return true;
    }
    case 0x80171D20u: { // GX::SetTevAlphaIn(stage,a,b,c,d)
        uint32_t s = cpu->gpr[3] & 15u;
        gx_bridge::g_gx.tevAlphaIn[s][0] = static_cast<uint8_t>(cpu->gpr[4] & 7u);
        gx_bridge::g_gx.tevAlphaIn[s][1] = static_cast<uint8_t>(cpu->gpr[5] & 7u);
        gx_bridge::g_gx.tevAlphaIn[s][2] = static_cast<uint8_t>(cpu->gpr[6] & 7u);
        gx_bridge::g_gx.tevAlphaIn[s][3] = static_cast<uint8_t>(cpu->gpr[7] & 7u);
        return true;
    }
    case 0x80171D60u: { // GX::SetTevColorOp(stage,op,bias,scale,clamp,outReg)
        uint32_t s = cpu->gpr[3] & 15u;
        gx_bridge::g_gx.tevColorOp[s] = static_cast<uint8_t>(cpu->gpr[4] == 1u ? 1u : 0u);
        gx_bridge::g_gx.tevColorBias[s] = static_cast<uint8_t>(cpu->gpr[5] & 3u);
        gx_bridge::g_gx.tevColorScale[s] = static_cast<uint8_t>(cpu->gpr[6] & 3u);
        gx_bridge::g_gx.tevColorClamp[s] = cpu->gpr[7] != 0;
        gx_bridge::g_gx.tevColorOut[s] = static_cast<uint8_t>(cpu->gpr[8] & 3u);
        return true;
    }
    case 0x80171DB8u: { // GX::SetTevAlphaOp(stage,op,bias,scale,clamp,outReg)
        uint32_t s = cpu->gpr[3] & 15u;
        gx_bridge::g_gx.tevAlphaOp[s] = static_cast<uint8_t>(cpu->gpr[4] == 1u ? 1u : 0u);
        gx_bridge::g_gx.tevAlphaBias[s] = static_cast<uint8_t>(cpu->gpr[5] & 3u);
        gx_bridge::g_gx.tevAlphaScale[s] = static_cast<uint8_t>(cpu->gpr[6] & 3u);
        gx_bridge::g_gx.tevAlphaClamp[s] = cpu->gpr[7] != 0;
        gx_bridge::g_gx.tevAlphaOut[s] = static_cast<uint8_t>(cpu->gpr[8] & 3u);
        return true;
    }
    case 0x80171F30u: { // GX::SetTevKColorSel(stage,sel)
        uint32_t s = cpu->gpr[3] & 15u;
        uint32_t sel = cpu->gpr[4];
        // 0x1F = none; otherwise the low bits select one of the 4 konst colours.
        gx_bridge::g_gx.tevKColorSel[s] = static_cast<uint8_t>((sel == 0x1Fu) ? 0u : (sel & 3u));
        return true;
    }
    case 0x80171F80u: { // GX::SetTevKAlphaSel(stage,sel)
        uint32_t s = cpu->gpr[3] & 15u;
        uint32_t sel = cpu->gpr[4];
        gx_bridge::g_gx.tevKAlphaSel[s] = static_cast<uint8_t>((sel == 0x1Fu) ? 0u : (sel & 3u));
        return true;
    }
    case 0x80171FD0u: { // GX::SetTevSwapMode(stage,rasSel,texSel)
        uint32_t s = cpu->gpr[3] & 15u;
        gx_bridge::g_gx.tevSwapRas[s] = static_cast<uint8_t>(cpu->gpr[4] & 3u);
        gx_bridge::g_gx.tevSwapTex[s] = static_cast<uint8_t>(cpu->gpr[5] & 3u);
        return true;
    }
    case 0x8017200Cu: { // GX::SetTevSwapModeTable(id,r,g,b,a)
        uint32_t id = cpu->gpr[3] & 3u;
        gx_bridge::g_gx.tevSwapTable[id][0] = static_cast<uint8_t>(cpu->gpr[4] & 3u);
        gx_bridge::g_gx.tevSwapTable[id][1] = static_cast<uint8_t>(cpu->gpr[5] & 3u);
        gx_bridge::g_gx.tevSwapTable[id][2] = static_cast<uint8_t>(cpu->gpr[6] & 3u);
        gx_bridge::g_gx.tevSwapTable[id][3] = static_cast<uint8_t>(cpu->gpr[7] & 3u);
        return true;
    }
    case 0x80172088u: { // GX::SetAlphaCompare(comp0,ref0,op,comp1,ref1)
        gx_bridge::g_gx.alphaComp0 = cpu->gpr[3] & 7u;
        gx_bridge::g_gx.alphaRef0  = cpu->gpr[4] & 0xFFu;
        gx_bridge::g_gx.alphaCompOp = cpu->gpr[5] & 3u;
        gx_bridge::g_gx.alphaComp1 = cpu->gpr[6] & 7u;
        gx_bridge::g_gx.alphaRef1  = cpu->gpr[7] & 0xFFu;
        return true;
    }
    case 0x801720C0u: // GX::SetZTexture
    case 0x801722CCu: // GX::SetFog
    case 0x801724F8u: // GX::InitFogAdjTable
    case 0x80172658u: // GX::SetFogRangeAdj
    case 0x8017054Cu: // GX::SetNumChans
    case 0x80170570u: // GX::SetChanCtrl
    case 0x80171110u: // GX::InvalidateTexAll
        return true;
    case 0x8016D39Cu: // GX::GetCurrentGXThread
        cpu->gpr[3] = Memory::Read32(0x800000E4u);
        return true;
    case 0x801AE4A0u: // OSIsTitleInstalled
        cpu->gpr[3] = 1;
        return true;
    case 0x801AD1D4u: // OSCheckInstall
        if (cpu->gpr[6] != 0 && Memory::Contains(cpu->gpr[6], 4)) {
            Memory::Write32(cpu->gpr[6], 3);
        }
        cpu->gpr[3] = 0;
        return true;
    case 0x801A9E84u: { // OS::CreateThread
        const uint32_t threadPtr  = cpu->gpr[3];
        const uint32_t entryFunc  = cpu->gpr[4];
        const uint32_t entryArg   = cpu->gpr[5];
        const uint32_t stackTop   = cpu->gpr[6];
        const uint32_t stackSize  = cpu->gpr[7];
        const int32_t  priority   = static_cast<int32_t>(cpu->gpr[8]);
        const uint16_t attributes = static_cast<uint16_t>(cpu->gpr[9]);

        if (priority < 0 || priority > 31) {
            cpu->gpr[3] = 0;
            return true;
        }

        const uint32_t alignedStack = stackTop & ~7u;
        Memory::Write16(threadPtr + 0x2C8u, 1);              // state = READY
        Memory::Write16(threadPtr + 0x2CAu, attributes & 1); // attributes
        Memory::Write32(threadPtr + 0x2D4u, priority);       // base priority
        Memory::Write32(threadPtr + 0x2D0u, priority);       // effective priority
        Memory::Write32(threadPtr + 0x2CCu, 1);              // suspend count = 1
        Memory::Write32(threadPtr + 0x2D8u, 0xFFFFFFFFu);    // exit value

        Memory::Write32(threadPtr + 0x2F0u, 0);
        Memory::Write32(threadPtr + 0x2ECu, 0);
        Memory::Write32(threadPtr + 0x2E8u, 0);
        Memory::Write32(threadPtr + 0x2F8u, 0);
        Memory::Write32(threadPtr + 0x2F4u, 0);

        Memory::Write32(alignedStack - 8, 0);
        Memory::Write32(alignedStack - 4, 0);

        Memory::Write32(threadPtr + 0x08u, alignedStack - 8); // sp (r1)
        Memory::Write32(threadPtr + 0x0Cu, entryArg);          // r3
        Memory::Write32(threadPtr + 0x84u, 0x801AA0F0u);      // lr (OSExitThread)
        Memory::Write32(threadPtr + 0x88u, entryFunc);         // pc / srr0

        Memory::Write32(threadPtr + 0x304u, stackTop);
        Memory::Write32(threadPtr + 0x308u, stackTop - stackSize);
        if (Memory::Contains(stackTop - stackSize, 4)) {
            Memory::Write32(stackTop - stackSize, 0xDEADBABEu);
        }

        Memory::Write32(threadPtr + 0x30Cu, 0);
        Memory::Write32(threadPtr + 0x310u, 0);
        Memory::Write32(threadPtr + 0x314u, 0);

        constexpr uint32_t kThreadListHeadAddr = 0x800000DCu;
        constexpr uint32_t kThreadListTailAddr = 0x800000E0u;
        const uint32_t tailThread = Memory::Read32(kThreadListTailAddr);
        uint32_t newHead = threadPtr;
        if (tailThread != 0) {
            Memory::Write32(tailThread + 0x2FCu, threadPtr);
            newHead = Memory::Read32(kThreadListHeadAddr);
        }
        Memory::Write32(kThreadListHeadAddr, newHead);
        Memory::Write32(threadPtr + 0x300u, tailThread);
        Memory::Write32(threadPtr + 0x2FCu, 0);
        Memory::Write32(kThreadListTailAddr, threadPtr);

        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801AA58Cu: { // OSResumeThread
        const uint32_t threadPtr = cpu->gpr[3];
        if (threadPtr == 0) {
            cpu->gpr[3] = 0;
            return true;
        }
        const int32_t suspendCount = static_cast<int32_t>(Memory::Read32(threadPtr + 0x2CCu));
        if (suspendCount > 0) {
            Memory::Write32(threadPtr + 0x2CCu, suspendCount - 1);
        }
        cpu->gpr[3] = suspendCount;
        return true;
    }
    case 0x801AA3F8u: { // OSSuspendThread
        const uint32_t threadPtr = cpu->gpr[3];
        if (threadPtr == 0) {
            cpu->gpr[3] = 0;
            return true;
        }
        const int32_t suspendCount = static_cast<int32_t>(Memory::Read32(threadPtr + 0x2CCu));
        Memory::Write32(threadPtr + 0x2CCu, suspendCount + 1);
        cpu->gpr[3] = suspendCount;
        return true;
    }
    case 0x801AA768u: { // OSInitThreadQueue
        if (cpu->gpr[3] != 0 && Memory::Contains(cpu->gpr[3], 8)) {
            Memory::Write32(cpu->gpr[3], 0);
            Memory::Write32(cpu->gpr[3] + 4, 0);
        }
        return true;
    }
    case 0x801AA9B8u: { // OSSleepThread
        const uint32_t queuePtr = cpu->gpr[3];
        const uint32_t viRetrace = Memory::Read32(0x80386be4u) + 1;
        Memory::Write32(0x80386be4u, viRetrace);
        if (queuePtr >= 88 && Memory::Contains(queuePtr - 88, 120)) {
            const uint32_t displayObj = queuePtr - 88;
            uint32_t frameCount = Memory::Read32(displayObj + 108);
            Memory::Write32(displayObj + 108, frameCount + 1);
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801AAAA4u: // OS::WakeupThread
    case 0x801AACA8u: // OS::SleepTicks
    case 0x801AAB98u: // OS::SetThreadPriority
    case 0x801AA1D4u: // OS::CancelThread
    case 0x801AA3ACu: // OS::JoinThread
    case 0x801AA4ECu: // OS::DetachThread
        cpu->gpr[3] = 0;
        return true;
    case 0x801A72FCu: { // OSInitMessageQueue
        const uint32_t queuePtr = cpu->gpr[3];
        const uint32_t msgArrayPtr = cpu->gpr[4];
        const uint32_t msgCount = cpu->gpr[5];
        if (queuePtr != 0) {
            Memory::Write32(queuePtr + 0x00u, 0);
            Memory::Write32(queuePtr + 0x04u, 0);
            Memory::Write32(queuePtr + 0x08u, 0);
            Memory::Write32(queuePtr + 0x0Cu, 0);
            Memory::Write32(queuePtr + 0x10u, msgArrayPtr);
            Memory::Write32(queuePtr + 0x14u, msgCount);
            Memory::Write32(queuePtr + 0x18u, 0);
            Memory::Write32(queuePtr + 0x1Cu, 0);
        }
        return true;
    }
    case 0x801A735Cu: { // OSSendMessage
        const uint32_t queuePtr = cpu->gpr[3];
        const uint32_t msg = cpu->gpr[4];
        if (queuePtr == 0) { cpu->gpr[3] = 0; return true; }

        // Synchronously service task jobs if a task was queued to a worker thread
        if (msg != 0 && Memory::Contains(msg, 24)) {
            const uint32_t callback = Memory::Read32(msg);
            if (callback >= 0x80004000u && callback < 0x81000000u) {
                const uint32_t arg = Memory::Read32(msg + 4);
                const uint32_t onDone = Memory::Read32(msg + 0x14u);
                AppLog("[TaskJob] frame=%u Executing callback %08X(arg=%08X) onDone=%08X\n",
                    gx_bridge::g_gxFrame, callback, arg, onDone);
                std::fflush(stdout);
                CpuContext taskCtx = *cpu;
                taskCtx.gpr[3] = arg;
                CpuContextScope scope(&taskCtx);
                InvokeIndirectCpu(callback, &taskCtx);
                Memory::Write32(msg + 0, 0);
                if (onDone >= 0x80004000u && onDone < 0x81000000u) {
                    taskCtx.gpr[3] = arg;
                    InvokeIndirectCpu(onDone, &taskCtx);
                }
            }
        }

        const uint32_t count = Memory::Read32(queuePtr + 0x14u);
        const uint32_t used  = Memory::Read32(queuePtr + 0x1Cu);
        if (count == 0 || used >= count) {
            cpu->gpr[3] = 0;
            return true;
        }
        const uint32_t arrayPtr = Memory::Read32(queuePtr + 0x10u);
        const uint32_t first = Memory::Read32(queuePtr + 0x18u);
        const uint32_t index = (first + used) % count;
        Memory::Write32(arrayPtr + index * 4u, msg);
        Memory::Write32(queuePtr + 0x1Cu, used + 1u);
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801A7424u: { // OSReceiveMessage
        const uint32_t queuePtr = cpu->gpr[3];
        const uint32_t outMsgPtr = cpu->gpr[4];
        if (queuePtr == 0) { cpu->gpr[3] = 0; return true; }
        const uint32_t count = Memory::Read32(queuePtr + 0x14u);
        const uint32_t used  = Memory::Read32(queuePtr + 0x1Cu);
        if (count == 0 || used == 0) {
            cpu->gpr[3] = 0;
            return true;
        }
        const uint32_t arrayPtr = Memory::Read32(queuePtr + 0x10u);
        uint32_t first = Memory::Read32(queuePtr + 0x18u);
        const uint32_t msg = Memory::Read32(arrayPtr + first * 4u);
        first = (first + 1u) % count;
        Memory::Write32(queuePtr + 0x18u, first);
        Memory::Write32(queuePtr + 0x1Cu, used - 1u);
        if (outMsgPtr != 0) {
            Memory::Write32(outMsgPtr, msg);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801A7500u: { // OSJamMessage
        const uint32_t queuePtr = cpu->gpr[3];
        const uint32_t msg = cpu->gpr[4];
        if (queuePtr == 0) { cpu->gpr[3] = 0; return true; }
        const uint32_t count = Memory::Read32(queuePtr + 0x14u);
        const uint32_t used  = Memory::Read32(queuePtr + 0x1Cu);
        if (count == 0 || used >= count) {
            cpu->gpr[3] = 0;
            return true;
        }
        const uint32_t arrayPtr = Memory::Read32(queuePtr + 0x10u);
        uint32_t first = Memory::Read32(queuePtr + 0x18u);
        first = (first == 0) ? (count - 1u) : (first - 1u);
        Memory::Write32(arrayPtr + first * 4u, msg);
        Memory::Write32(queuePtr + 0x18u, first);
        Memory::Write32(queuePtr + 0x1Cu, used + 1u);
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x8012E630u:
        if (cpu->hid2 == 0) cpu->hid2 = 0x10000000u;
        cpu->gpr[3] = cpu->hid2;
        return true;
    case 0x8012E638u:
        cpu->hid2 = cpu->gpr[3];
        return true;
    case 0x801A7DFCu: // Desktop skips hardware memory protection.
    case 0x801671D0u: // ESP_InitLib mirrors desktop startup.
    case 0x80167224u: // ESP_CloseLib.
    case 0x801A8A50u: // Cold boot.
        cpu->gpr[3] = 0;
        return true;
    case 0x80194290u: // Match desktop IOS behavior for the startup STM request.
        AppLog("IOS ioctl fd=%ld cmd=%lu\n", static_cast<long>(static_cast<int32_t>(channel)), static_cast<unsigned long>(cpu->gpr[4]));
        if (channel == 0x00535401u && cpu->gpr[4] == 0x6002u) {
            cpu->gpr[3] = 0;
            return true;
        }
        if (static_cast<int32_t>(channel) >= 0) {
            cpu->gpr[3] = 0;
            return true;
        }
        cpu->gpr[3] = static_cast<uint32_t>(-101);
        return true;
    case 0x80194158u: { // IOS_IoctlAsync
        const uint32_t fd = cpu->gpr[3];
        const uint32_t cmd = cpu->gpr[4];
        const uint32_t callback = cpu->gpr[9];
        const uint32_t callbackArg = cpu->gpr[10];
        AppLog("IOS ioctlAsync fd=%ld cmd=%lu cb=0x%08X\n",
            static_cast<long>(static_cast<int32_t>(fd)), static_cast<unsigned long>(cmd), callback);
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = 0; // result = success
            cbCtx.gpr[4] = callbackArg;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 0; // Queued successfully
        return true;
    }
    case 0x801944FCu: { // IOS_IoctlvAsync
        const uint32_t callback = cpu->gpr[8];
        const uint32_t callbackArg = cpu->gpr[9];
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = 0;
            cbCtx.gpr[4] = callbackArg;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801937E0u: { // IOS_OpenAsync
        const uint32_t callback = cpu->gpr[5];
        const uint32_t callbackArg = cpu->gpr[6];
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = static_cast<uint32_t>(-106); // not found
            cbCtx.gpr[4] = callbackArg;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x80193A18u: { // IOS_CloseAsync
        const uint32_t callback = cpu->gpr[4];
        const uint32_t callbackArg = cpu->gpr[5];
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = 0;
            cbCtx.gpr[4] = callbackArg;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801938F8u: { // IOS::Open against the empty startup NAND.
        char path[64]{};
        size_t index = 0;
        for (; index < sizeof(path); ++index) {
            if (!Memory::Contains(channel + index)) return false;
            path[index] = static_cast<char>(Memory::Read8(channel + index));
            if (path[index] == 0) break;
        }
        if (index == sizeof(path)) return false;
        AppLog("IOS open: %s\n", path);
        cpu->gpr[3] = std::strcmp(path, "/dev/fs") == 0 ? 1u : static_cast<uint32_t>(-106);
        return true;
    }
    case 0x8019C918u: // The optional play record is absent on the empty NAND.
        if (channel != 0x8029C858u || cpu->gpr[5] != 3 || cpu->gpr[6] != 0x801ABD70u) return false;
        cpu->gpr[3] = nandMissing;
        return true;
    case 0x8019C990u: // The harness has an empty NAND image.
        if (cpu->gpr[5] != 1 || cpu->gpr[6] != configOpenCallback
            || pendingConfigCommand != 0 || cpu->gpr[7] == 0)
            return false;
        pendingConfigCommand = cpu->gpr[7];
        cpu->gpr[3] = nandMissing;
        return true;
    case 0x8019C800u: { // NANDOpen(path, NANDFileInfo* info, mode)
        const uint32_t pathPtr = cpu->gpr[3];
        std::string path;
        for (size_t i = 0; i < 256 && Memory::Contains(pathPtr + i); ++i) {
            char c = static_cast<char>(Memory::Read8(pathPtr + i));
            if (c == 0) break;
            path.push_back(c);
        }
        const uint32_t info = cpu->gpr[4];
        const bool exists = nandLoad(path);
        if (exists && Memory::Contains(info, 12)) {
            const uint32_t fd = s_nandNextFd++;
            s_nandOpen[fd] = NandFile{path, 0};
            Memory::Write32(info, fd);
            Memory::Write32(info + 4, 0);
            Memory::Write8(info + 8, 1);
            Memory::Write8(info + 9, 0);
            AppLog("[NAND] NANDOpen: '%s' mode=%u -> fd=%u size=%zu\n",
                path.c_str(), cpu->gpr[5], fd, nandFileSize(path));
            cpu->gpr[3] = 0;
        } else {
            AppLog("[NAND] NANDOpen: '%s' mode=%u -> missing\n", path.c_str(), cpu->gpr[5]);
            cpu->gpr[3] = nandMissing;
        }
        return true;
    }
    case 0x8019CA80u: // NANDClose(info)
        {
            static uint32_t s_closeLogs = 0;
            if (s_closeLogs < 6) {
                ++s_closeLogs;
                AppLog("[NAND] Close info=%08X fd=%u\n", cpu->gpr[3], nandInfoFd(cpu->gpr[3]));
            }
        }
        if (const uint32_t fd = nandInfoFd(cpu->gpr[3])) s_nandOpen.erase(fd);
        cpu->gpr[3] = 0;
        return true;
    case 0x8019B43Cu: { // NANDCreate(path, perm, attr)
        const uint32_t pathPtr = cpu->gpr[3];
        std::string path;
        for (size_t i = 0; i < 256 && Memory::Contains(pathPtr + i); ++i) {
            char c = static_cast<char>(Memory::Read8(pathPtr + i));
            if (c == 0) break;
            path.push_back(c);
        }
        s_nandLoaded[path] = true;
        s_nandStore[path];
        AppLog("[NAND] NANDCreate '%s'\n", path.c_str());
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x8019E770u: { // NANDGetType(path, u8* type)
        const uint32_t pathPtr = cpu->gpr[3];
        std::string path;
        for (size_t i = 0; i < 256 && Memory::Contains(pathPtr + i); ++i) {
            char c = static_cast<char>(Memory::Read8(pathPtr + i));
            if (c == 0) break;
            path.push_back(c);
        }
        if (nandLoad(path)) {
            if (Memory::Contains(cpu->gpr[4], 1)) Memory::Write8(cpu->gpr[4], 1); // file
            cpu->gpr[3] = 0;
            return true;
        }
        AppLog("[NAND] NANDGetType: '%s' -> missing\n", path.c_str());
        cpu->gpr[3] = nandMissing;
        return true;
    }
    case 0x8019EAD0u: // NANDCheck
        cpu->gpr[3] = 0;
        return true;
    case 0x8019E390u: // NANDGetCurrentDir
        cpu->gpr[3] = 0;
        return true;
    case 0x8019C380u: // NANDGetStatus
        cpu->gpr[3] = 0;
        return true;
    case 0x8019B7A4u: { // NANDRead(info, buf, len)
        const uint32_t fd = nandInfoFd(cpu->gpr[3]);
        auto mit = s_nandOpen.find(fd);
        static uint32_t s_nandReadLogs = 0;
        if (s_nandReadLogs < 12) {
            ++s_nandReadLogs;
            AppLog("[NAND] Read info=%08X fd=%u len=%u known=%d\n",
                cpu->gpr[3], fd, cpu->gpr[5], mit != s_nandOpen.end() ? 1 : 0);
        }
        if (mit == s_nandOpen.end()) { cpu->gpr[3] = nandMissing; return true; }
        const std::string& fpath = mit->second.path;
        const uint32_t buf = cpu->gpr[4];
        uint32_t len = cpu->gpr[5];
        const size_t pos = mit->second.pos;
        const size_t total = nandFileSize(fpath);
        if (pos >= total) { cpu->gpr[3] = 0; return true; }
        if (len > total - pos) len = static_cast<uint32_t>(total - pos);
        auto storeIt = s_nandStore.find(fpath);
        const bool inMemory = storeIt != s_nandStore.end() && !storeIt->second.empty();
        bool copied = false;
        uint32_t hdr = 0;
        int hdrN = 0;
        if (inMemory) {
            const std::vector<uint8_t>& data = storeIt->second;
            if (Memory::Contains(buf, len)) {
                for (uint32_t i = 0; i < len; ++i) Memory::Write8(buf + i, data[pos + i]);
                copied = true;
            }
            for (int i = 0; i < 4 && pos + static_cast<size_t>(i) < data.size(); ++i) {
                hdr = (hdr << 8) | data[pos + i];
            }
        } else {
            auto dsk = s_nandDisk.find(fpath);
            FILE* fp = dsk != s_nandDisk.end() ? std::fopen(dsk->second.c_str(), "rb") : nullptr;
            if (fp && Memory::Contains(buf, len)) {
                std::fseek(fp, static_cast<long>(pos), SEEK_SET);
                uint8_t chunk[4096];
                uint32_t done = 0;
                while (done < len) {
                    const uint32_t want = (len - done) < sizeof(chunk) ? (len - done) : static_cast<uint32_t>(sizeof(chunk));
                    const size_t got = std::fread(chunk, 1, want, fp);
                    if (got == 0) break;
                    for (size_t i = 0; i < got; ++i) {
                        const uint8_t byte = chunk[i];
                        Memory::Write8(buf + done + static_cast<uint32_t>(i), byte);
                        if (hdrN < 4) { hdr = (hdr << 8) | byte; ++hdrN; }
                    }
                    done += static_cast<uint32_t>(got);
                }
                len = done;
                copied = true;
            }
            if (fp) std::fclose(fp);
        }
        if (!copied) len = 0;
        mit->second.pos = pos + len;
        static uint32_t s_nandCopyLogs = 0;
        if (s_nandCopyLogs < 6) {
            ++s_nandCopyLogs;
            AppLog("[NAND] Read -> %u bytes (pos=%zu size=%zu) first4=%08X (%s)\n",
                len, pos, total, hdr, inMemory ? "mem" : "disk");
        }
        // The SDK returns the byte count; MKW_READ_RET0=1 tests the status-code hypothesis.
        cpu->gpr[3] = std::getenv("MKW_READ_RET0") ? 0u : len;
        return true;
    }
    case 0x8019B884u: { // NANDWrite(info, buf, len)
        const uint32_t fd = nandInfoFd(cpu->gpr[3]);
        auto mit = s_nandOpen.find(fd);
        if (mit == s_nandOpen.end()) { cpu->gpr[3] = nandMissing; return true; }
        std::vector<uint8_t>& data = s_nandStore[mit->second.path];
        const uint32_t buf = cpu->gpr[4];
        const uint32_t len = cpu->gpr[5];
        const size_t pos = mit->second.pos;
        if (data.empty()) {
            // First write to a disk-backed file: pull it into memory once (writes are rare).
            auto dsk = s_nandDisk.find(mit->second.path);
            if (dsk != s_nandDisk.end()) {
                FILE* fp = std::fopen(dsk->second.c_str(), "rb");
                if (fp) {
                    std::fseek(fp, 0, SEEK_END);
                    const long size = std::ftell(fp);
                    std::fseek(fp, 0, SEEK_SET);
                    if (size > 0) {
                        data.resize(static_cast<size_t>(size));
                        const size_t got = std::fread(data.data(), 1, data.size(), fp);
                        data.resize(got);
                    }
                    std::fclose(fp);
                }
            }
        }
        if (pos + len > data.size()) data.resize(pos + len, 0);
        if (Memory::Contains(buf, len)) {
            for (uint32_t i = 0; i < len; ++i) data[pos + i] = Memory::Read8(buf + i);
            mit->second.pos = pos + len;
            cpu->gpr[3] = len;
        } else {
            cpu->gpr[3] = 0;
        }
        return true;
    }
    case 0x8019B964u: { // NANDSeek(info, offset, whence)
        static uint32_t s_seekLogs = 0;
        if (s_seekLogs < 10) { ++s_seekLogs; AppLog("[NAND] Seek info=%08X off=%d whence=%u\n", cpu->gpr[3], (int32_t)cpu->gpr[4], cpu->gpr[5]); }
        const uint32_t fd = nandInfoFd(cpu->gpr[3]);
        auto mit = s_nandOpen.find(fd);
        if (mit == s_nandOpen.end()) { cpu->gpr[3] = nandMissing; return true; }
        const size_t totalSize = nandFileSize(mit->second.path);
        const int32_t offset = static_cast<int32_t>(cpu->gpr[4]);
        int64_t base = 0;
        if (cpu->gpr[5] == 1) base = static_cast<int64_t>(mit->second.pos);
        else if (cpu->gpr[5] == 2) base = static_cast<int64_t>(totalSize);
        int64_t next = base + offset;
        if (next < 0) next = 0;
        if (next > static_cast<int64_t>(totalSize)) next = static_cast<int64_t>(totalSize);
        mit->second.pos = static_cast<size_t>(next);
        cpu->gpr[3] = static_cast<uint32_t>(next);
        return true;
    }
    case 0x8019BF4Cu: { // NANDGetLength(info, u32* length)
        static uint32_t s_lenLogs = 0;
        if (s_lenLogs < 6) { ++s_lenLogs; AppLog("[NAND] GetLength info=%08X\n", cpu->gpr[3]); }
        const uint32_t fd = nandInfoFd(cpu->gpr[3]);
        auto mit = s_nandOpen.find(fd);
        if (mit == s_nandOpen.end()) { cpu->gpr[3] = nandMissing; return true; }
        if (Memory::Contains(cpu->gpr[4], 4)) {
            Memory::Write32(cpu->gpr[4], static_cast<uint32_t>(nandFileSize(mit->second.path)));
        }
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x8019B59Cu: // NANDDelete
    case 0x8019BBE0u: // NANDCreateDir
    case 0x8019BEE8u: // NANDMove
    case 0x8019E880u: // NANDInitBanner
    case 0x8019E95Cu: // NANDSecretGetUsage
        cpu->gpr[3] = 0;
        return true;
    case 0x8015BC80u: // contentFastOpenNAND
    case 0x8015BCF8u: // contentReadNAND
        cpu->gpr[3] = static_cast<uint32_t>(-106);
        return true;
    case 0x801B0220u:
        if (pendingConfigCommand != 0) {
            CpuContext callback = *cpu;
            callback.gpr[3] = nandMissing;
            callback.gpr[4] = pendingConfigCommand;
            pendingConfigCommand = 0;
            CpuContextScope scope(&callback);
            InvokeIndirectCpu(configOpenCallback, &callback);
        }
        cpu->gpr[3] = 0;
        return true;
    case 0x800077C8u: // StrapScene::CheckInput
        AppLog("[StrapScene] CheckInput accepted! Advancing past strap screen...\n");
        cpu->gpr[3] = 1;
        return true;
    case 0x80218C2Cu: { // EGG::Decomp::decodeSZS
        const uint32_t src = cpu->gpr[3];
        const uint32_t dst = cpu->gpr[4];
        {
            const uint32_t expSize = (src && Memory::Contains(src + 4, 4))
                ? ((static_cast<uint32_t>(Memory::Read8(src + 4)) << 24) |
                   (static_cast<uint32_t>(Memory::Read8(src + 5)) << 16) |
                   (static_cast<uint32_t>(Memory::Read8(src + 6)) << 8) |
                   static_cast<uint32_t>(Memory::Read8(src + 7)))
                : 0u;
            AppLog("[Decomp] decodeSZS src=%08X dst=%08X expand=%u lr=%08X r5=%08X r6=%08X r7=%08X frame=%u\n",
                src, dst, expSize, cpu->lr, cpu->gpr[5], cpu->gpr[6], cpu->gpr[7],
                gx_bridge::g_gxFrame);
        }
        if (dst == 0) {
            AppLog("[Decomp] Rejecting decodeSZS to null destination!\n");
            cpu->gpr[3] = 0;
            return true;
        }
        const uint32_t expandSize = (static_cast<uint32_t>(Memory::Read8(src + 4)) << 24) |
                                    (static_cast<uint32_t>(Memory::Read8(src + 5)) << 16) |
                                    (static_cast<uint32_t>(Memory::Read8(src + 6)) << 8) |
                                    static_cast<uint32_t>(Memory::Read8(src + 7));
        uint32_t srcIdx = 16;
        uint32_t dstIdx = 0;
        uint32_t mask = 0;
        uint32_t flags = 0;
        while (static_cast<int32_t>(dstIdx) < static_cast<int32_t>(expandSize)) {
            if (mask == 0) {
                flags = Memory::Read8(src + srcIdx++);
                mask = 0x80;
            }
            if ((flags & mask) != 0) {
                Memory::Write8(dst + dstIdx++, Memory::Read8(src + srcIdx++));
            } else {
                const uint32_t high = Memory::Read8(src + srcIdx);
                const uint32_t low = Memory::Read8(src + srcIdx + 1);
                srcIdx += 2;
                const uint32_t rep = (high << 8) | low;
                const uint32_t distance = (rep & 0xFFF) + 1;
                uint32_t copyIdx = dstIdx - distance;
                uint32_t count = rep >> 12;
                count = count != 0 ? count + 2 : static_cast<uint32_t>(Memory::Read8(src + srcIdx++)) + 18;
                for (uint32_t i = 0; i < count && dstIdx < expandSize; ++i) {
                    Memory::Write8(dst + dstIdx++, Memory::Read8(dst + copyIdx++));
                }
            }
            mask >>= 1;
        }
        AppLog("[Decomp] Decoded %u bytes SZS from 0x%08X to 0x%08X\n", expandSize, src, dst);
        if (g_decompressedArcs.size() < 64) {
            bool dup = false;
            for (uint32_t a : g_decompressedArcs) if (a == dst) { dup = true; break; }
            if (!dup) g_decompressedArcs.push_back(dst);
        }
        std::fflush(stdout);
        cpu->gpr[3] = expandSize;
        return true;
    }
    case 0x801643FCu: // The extracted disc filesystem is ready.
        cpu->gpr[3] = 1;
        return true;
    case 0x80166964u: // DVDLowClearCoverInterrupt
    case 0x80164848u: // DVDLowInit
    case 0x80166954u:
    case 0x8016695Cu:
        cpu->gpr[3] = 1;
        return true;
    case 0x80165A30u: { // DVDLowInquiry
        const uint32_t driveInfoPtr = cpu->gpr[3];
        const uint32_t callback = cpu->gpr[4];
        if (driveInfoPtr && Memory::Contains(driveInfoPtr, 0x20)) {
            Memory::Write32(driveInfoPtr + 0x0, 0x00000002);
            Memory::Write32(driveInfoPtr + 0x4, 0x20060526);
            Memory::Write32(driveInfoPtr + 0x8, 0x41000000);
        }
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = 1;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x80164AACu: { // DVDLowReadDiskID
        const uint32_t diskIdPtr = cpu->gpr[3];
        const uint32_t callback = cpu->gpr[4];
        if (diskIdPtr && Memory::Contains(diskIdPtr, 0x20)) {
            for (uint32_t i = 0x07; i < 0x20; ++i) Memory::Write8(diskIdPtr + i, 0);
            Memory::Write32(diskIdPtr + 0x00, 0x524D4350); // 'RMCP'
            Memory::Write16(diskIdPtr + 0x04, 0x3031);
            Memory::Write8 (diskIdPtr + 0x06, 0x01);
            Memory::Write32(diskIdPtr + 0x18, 0x5D1C9EA3);
        }
        if (callback != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = 1;
            cbCtx.lr = 0;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callback, &cbCtx);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x8015E834u: { // DVDReadPrio
        const uint32_t fileInfoPtr = cpu->gpr[3];
        const uint32_t bufferPtr   = cpu->gpr[4];
        const int32_t length       = static_cast<int32_t>(cpu->gpr[5]);
        const int32_t offset       = static_cast<int32_t>(cpu->gpr[6]);
        const int32_t bytesRead = HandleDvdRead(fileInfoPtr, bufferPtr, length, offset);
        cpu->gpr[3] = bytesRead;
        return true;
    }
    case 0x8015E74Cu: { // DVDReadAsyncPrio
        const uint32_t fileInfoPtr = cpu->gpr[3];
        const uint32_t bufferPtr   = cpu->gpr[4];
        const int32_t length       = static_cast<int32_t>(cpu->gpr[5]);
        const int32_t offset       = static_cast<int32_t>(cpu->gpr[6]);
        const uint32_t callbackPtr = cpu->gpr[7];
        const int32_t bytesRead = HandleDvdRead(fileInfoPtr, bufferPtr, length, offset);
        if (callbackPtr != 0) {
            CpuContext cbCtx = *cpu;
            cbCtx.gpr[3] = bytesRead;
            cbCtx.gpr[4] = fileInfoPtr;
            CpuContextScope scope(&cbCtx);
            InvokeIndirectCpu(callbackPtr, &cbCtx);
        }
        cpu->gpr[3] = bytesRead >= 0 ? 1 : 0;
        return true;
    }
    case 0x8015E568u: { // DVDClose
        if (cpu->gpr[3] != 0 && Memory::Contains(cpu->gpr[3], 0x38)) {
            Memory::Write32(cpu->gpr[3] + 0x0C, 0);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x80162A88u: { // DVDGetCommandBlockStatus
        if (cpu->gpr[3] != 0 && Memory::Contains(cpu->gpr[3], 0x10)) {
            cpu->gpr[3] = Memory::Read32(cpu->gpr[3] + 0x0C);
        } else {
            cpu->gpr[3] = 0;
        }
        return true;
    }
    case 0x801630C8u: { // DVDCancel
        if (cpu->gpr[3] != 0 && Memory::Contains(cpu->gpr[3], 0x10)) {
            Memory::Write32(cpu->gpr[3] + 0x0C, 0);
        }
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x801630D0u: // DVD::CancelAll
        cpu->gpr[3] = 1;
        return true;
    case 0x80577064u: { // kart/driver model loader: func_80577064(info, holder->124, holder+552)
        // info+0 must be an nw4r ResFile over a .brres, because the loader calls
        // ResFile::GetResMdl(info, "model_lod"). Section 32 runs at frame 84 with info+0 = NULL
        // (the ArchiveMgr groups only become usable at ~frame 100), so every lookup fails.
        // Supply the kart .brres straight out of the archives we have already decompressed.
        const uint32_t info = cpu ? cpu->gpr[3] : 0;
        if (info && Memory::Contains(info, 24) && Memory::Read32(info) == 0) {
            static bool s_loggedRescue = false;
            for (uint32_t arcData : g_decompressedArcs) {
                if (!arcData || !Memory::Contains(arcData, 4)) continue;
                uint32_t size = 0;
                const uint32_t brres = FindU8Resource(arcData, "kart_model.brres", &size);
                if (brres && Memory::Contains(brres, 4) && ReadTraceWord(brres) == 0x62726573u) {
                    Memory::Write32(info, brres);   // ResFile handle = .brres image
                    Memory::Write8(info + 17, 1);   // loader gate: "resource available"
                    if (!s_loggedRescue) {
                        s_loggedRescue = true;
                        AppLog("[Kart Rescue] info=%08X <- kart_model.brres %08X (arc=%08X size=%u) frame=%u\n",
                            info, brres, arcData, size, gx_bridge::g_gxFrame);
                    }
                    break;
                }
            }
        }
        func_80577064(cpu);
        return true;
    }
    case 0x80540038u: { // archive load job; arg indexes entries at *(0x809BD748) + arg*84
        const uint32_t argIn = cpu ? cpu->gpr[3] : 0;
        const uint32_t baseIn = Memory::Read32(0x809BD748u);
        static std::set<uint32_t> s_seenJobArgs;
        if (s_seenJobArgs.size() < 48 && s_seenJobArgs.insert(argIn).second) {
            const uint32_t e = baseIn + argIn * 84u;
            std::string nm;
            if (Memory::Contains(e + 836u, 1)) {
                nm = reinterpret_cast<const char*>(Memory::GetPointer(e + 836u, 1));
            }
            AppLog("[Arc Job] arg=%u base=%08X entry=%08X h824=%08X name836='%s' f900=%08X f904=%08X frame=%u\n",
                argIn, baseIn, e, Memory::Contains(e + 824u, 4) ? Memory::Read32(e + 824u) : 0xDEADBEEFu,
                nm.c_str(),
                Memory::Contains(e + 900u, 4) ? Memory::Read32(e + 900u) : 0,
                Memory::Contains(e + 904u, 4) ? Memory::Read32(e + 904u) : 0,
                gx_bridge::g_gxFrame);
        }
        func_80540038(cpu);
        return true;
    }
    case 0x8052A954u: { // archive loader step: (handle, namePtr, table, r6, r7)
        // Returns 4 when the archive group is ready. It advances roughly one step per frame, which
        // is fatal for karts: section 32 init creates karts at frame 84 while the archives only
        // become usable around frame 100. Our disc/decompression HLE is synchronous, so drive the
        // state machine to completion here instead of letting it creep forward frame by frame.
        const uint32_t a3 = cpu ? cpu->gpr[3] : 0;
        const uint32_t a4 = cpu ? cpu->gpr[4] : 0;
        const uint32_t a5 = cpu ? cpu->gpr[5] : 0;
        const uint32_t a6 = cpu ? cpu->gpr[6] : 0;
        const uint32_t a7 = cpu ? cpu->gpr[7] : 0;
        std::string nm;
        if (a4 && Memory::Contains(a4, 1)) {
            nm = reinterpret_cast<const char*>(Memory::GetPointer(a4, 1));
        }
        const uint32_t reqLr = cpu ? cpu->lr : 0;
        std::string seq;
        uint32_t ret = 0;
        int iter = 0;
        for (; iter < 16; ++iter) {
            cpu->gpr[3] = a3; cpu->gpr[4] = a4; cpu->gpr[5] = a5;
            cpu->gpr[6] = a6; cpu->gpr[7] = a7;
            func_8052A954(cpu);
            ret = cpu->gpr[3];
            seq += std::to_string(ret);
            seq += ' ';
            if (ret == 4 || ret == 0) break;
        }
        static std::set<std::string> s_seenArcLoads;
        std::string key = std::to_string(a3) + "|" + nm;
        if (s_seenArcLoads.size() < 48 && s_seenArcLoads.insert(key).second) {
            AppLog("[Arc Load] handle=%08X name='%s' steps=%d seq=%s frame=%u reqLr=%08X\n",
                a3, nm.c_str(), iter + 1, seq.c_str(), gx_bridge::g_gxFrame, reqLr);
        }
        return true;
    }
    case 0x805A08F0u: { // RaceCameraMgr constructor (linked 9,467th)
        // Forwarded here because handle_startup_hle runs before the dispatch switch; dispatch.cpp
        // also carries the case now that the address is in linked-functions.txt.
        func_805A08F0(cpu);
        return true;
    }
    case 0x805E889Cu: { // ControlLoader: bind a control's panes to its loaded ".brlyt"
        // The name in gpr[4] points into the resource image the caller just fetched, so an empty
        // name means the fetch returned NULL (the archive behind this accessor never finished
        // loading - see the "ARCInitHandle: bad archive format" mounts at the same frame).
        // func_805E889C then walks the layout's pane list at +0x14, which is still NULL, so nw4r
        // raises LinkList.h:573; because VPanic is downgraded to a warning the guest re-enters the
        // same NULL walk forever (measured: 5M panics per 10M dispatches, frame never completes).
        // Leaving the build out keeps the layout in the empty state its constructor produced,
        // where every sub-list is self-terminating.
        const uint32_t lay = cpu ? cpu->gpr[3] : 0;
        const uint32_t nm = cpu ? cpu->gpr[4] : 0;
        const bool hasName = nm && Memory::Contains(nm, 1) && *Memory::GetPointer(nm, 1) != 0;
        if (!hasName) {
            static std::set<uint32_t> s_skipped;
            if (s_skipped.size() < 8 && s_skipped.insert(lay).second) {
                AppLog("[ControlLoader] no resource for lay=%08X acc=%08X frame=%u - keeping the layout empty\n",
                    lay, (lay && Memory::Contains(lay + 40, 4)) ? Memory::Read32(lay + 36) : 0,
                    gx_bridge::g_gxFrame);
            }
            return true;
        }
        func_805E889C(cpu);
        return true;
    }
    case 0x805E78F0u: { // nw4r LinkList sort (recursive). A malformed list makes it recurse forever.
        // nw4r's sort panics with "Pointer must not be NULL" when the list linkage is broken, and
        // because we downgrade VPanic to a warning the guest keeps recursing into itself
        // (0x805E7A34 -> func_805E78F0) until the host stack overflows. Bound the recursion: past a
        // sane depth just return, leaving the list order untouched.
        static uint32_t s_sortDepth = 0;
        if (s_sortDepth > 32u) {
            static uint32_t s_skipped = 0;
            if (++s_skipped <= 4) {
                AppLog("[LinkList] sort recursion depth exceeded - skipping sort (frame=%u)\n",
                    gx_bridge::g_gxFrame);
            }
            return true;
        }
        ++s_sortDepth;
        func_805E78F0(cpu);
        --s_sortDepth;
        return true;
    }
    case 0x805B9344u: { // ArgumentParser::parse
        // BOOT PATH SWITCH.
        // false = let the game choose its own first section (intro -> menu -> character select ->
        //         track select -> race). This is the "proper" path: every field we have been
        //         patching by hand (racedata+0xB70, player counts, kart ids, camera mode) is
        //         produced by it.
        // true  = the old shortcut: force section 0x20 (kSectionP1Vs) and jump straight into a race.
        // Left false: the natural path now runs past the section-0x15 layout build (see the
        // 0x805E889C case) instead of stalling in nw4r's LinkList walk at frame 84.
        constexpr bool kForceRaceSection = false;
        bool forceRace = kForceRaceSection;
        if (const char* e = std::getenv("MKW_FORCE_RACE")) forceRace = (e[0] == '1');
        g_raceShortcut = forceRace;

        const uint32_t sectionMgr = Memory::Read32(0x809C1E38u);
        AppLog("[BootPath] ArgumentParser::parse frame=%u sm=%08X curSec=%d lr=%08X forceRace=%d\n",
            gx_bridge::g_gxFrame, sectionMgr,
            sectionMgr ? static_cast<int32_t>(Memory::Read32(sectionMgr + 4)) : -1,
            cpu ? cpu->lr : 0, forceRace ? 1 : 0);

        if (!forceRace) {
            // The real parser is a thunk that returns *(*(0x80386000)+152), the boot section
            // chosen by the launch-argument handler. Report it and allow an override.
            {
                const uint32_t bootArg = Memory::Read32(0x80386000u);
                const uint32_t bootSection = (bootArg && Memory::Contains(bootArg + 152, 4))
                    ? Memory::Read32(bootArg + 152) : 0xDEADBEEFu;
                AppLog("[BootPath] bootArg=%08X bootSection=%d (0x%X)\n",
                    bootArg, static_cast<int32_t>(bootSection), bootSection);
                if (const char* e = std::getenv("MKW_BOOT_ARG")) {
                    if (bootArg && Memory::Contains(bootArg + 152, 4)) {
                        // The parser expects a UTF-16 launch-argument string such as "-1".
                        constexpr uint32_t kArgScratch = 0x955F8000u;
                        const char* enc = std::getenv("MKW_ARG_ENC");
                        const bool utf16 = enc && (enc[0] == 'u');
                        size_t n = 0;
                        for (; e[n] && n < 32; ++n) {
                            if (utf16) {
                                Memory::Write8(kArgScratch + n * 2, 0);
                                Memory::Write8(kArgScratch + n * 2 + 1, static_cast<uint8_t>(e[n]));
                            } else {
                                Memory::Write8(kArgScratch + n, static_cast<uint8_t>(e[n]));
                            }
                        }
                        if (utf16) {
                            Memory::Write8(kArgScratch + n * 2, 0);
                            Memory::Write8(kArgScratch + n * 2 + 1, 0);
                        } else {
                            Memory::Write8(kArgScratch + n, 0);
                        }
                        Memory::Write32(bootArg + 152, kArgScratch);
                        AppLog("[BootPath] overriding launch arg -> '%s' at %08X\n", e, kArgScratch);
                    }
                }
            }
            // MKW_FORCE_SECTION: mirror the race shortcut's section switch at parse time.
            // Default to section 16 (the game's main menu). The natural parser result is -1
            // because the launch-argument string is NULL, which SectionMgr turns into the
            // Mario Kart Channel (section 21). MKW_NATURAL_BOOT=1 restores that path.
            if (std::getenv("MKW_FORCE_SECTION")) {
                const char* e = std::getenv("MKW_FORCE_SECTION");
                // Default to section 13 (the title screen) so the boot flow matches the real
                // game: strap -> online notice -> title -> main menu. Section 16 is the main
                // menu, 21/22 the Mario Kart Channel, 25+ race.
                const uint32_t sec = e ? static_cast<uint32_t>(std::atoi(e)) : 13u;
                if (sectionMgr) {
                    Memory::Write32(sectionMgr + 4, sec);
                    Memory::Write32(sectionMgr + 12, sec);
                    CpuContext padCtx = *cpu;
                    padCtx.gpr[3] = sectionMgr + 0x34u;
                    padCtx.gpr[4] = 0;
                    padCtx.gpr[5] = 0x124u;
                    CpuContextScope scope(&padCtx);
                    InvokeIndirectCpu(0x8061B490u, &padCtx);
                }
                cpu->gpr[3] = sec;
                AppLog("[BootPath] forced section -> %u\n", sec);
                return true;
            }
            const uint32_t bootArgStruct = Memory::Read32(0x80386000u);
            if (bootArgStruct && Memory::Contains(bootArgStruct + 152, 4)) {
                const uint32_t argPtr = Memory::Read32(bootArgStruct + 152);
                if (argPtr && Memory::Contains(argPtr, 32)) {
                    char buf[33] = {};
                    for (int i = 0; i < 32; ++i) buf[i] = static_cast<char>(Memory::Read8(argPtr + i));
                    AppLog("[BootArgString] ptr=%08X '%s'\n", argPtr, buf);
                    constexpr uint32_t kScratch = 0x955F8200u;
                    CpuContext findCtx = *cpu;
                    findCtx.gpr[3] = argPtr;
                    findCtx.gpr[4] = 0x808B73A0u;
                    CpuContextScope findScope(&findCtx);
                    func_800135F0(&findCtx);
                    const uint32_t found = findCtx.gpr[3];
                    AppLog("[ParseTest] strstr=%08X (arg+%d)\n", found, found ? static_cast<int>(found - argPtr) : -1);
                    Memory::Write8(0x955F8300u, '-');
                    Memory::Write8(0x955F8301u, 's');
                    Memory::Write8(0x955F8302u, 0);
                    CpuContext findSCtx = *cpu;
                    findSCtx.gpr[3] = argPtr;
                    findSCtx.gpr[4] = 0x955F8300u;
                    CpuContextScope findSScope(&findSCtx);
                    func_800135F0(&findSCtx);
                    const uint32_t foundS = findSCtx.gpr[3];
                    AppLog("[ParseTest2] strstrS=%08X (arg+%d)\n", foundS,
                        foundS ? static_cast<int>(foundS - argPtr) : -1);
                    if (foundS) {
                        Memory::Write32(0x955F8304u, 0xDEADBEEFu);
                        CpuContext psCtx = *cpu;
                        psCtx.gpr[3] = foundS + 2;
                        psCtx.gpr[4] = 0x808927CBu;
                        psCtx.gpr[5] = 0x955F8304u;
                        CpuContextScope psScope(&psCtx);
                        func_80013040(&psCtx);
                        const uint32_t sv = Memory::Read32(0x955F8304u);
                        CpuContext mapCtx = *cpu;
                        mapCtx.gpr[3] = sv;
                        CpuContextScope mapScope(&mapCtx);
                        func_80631C10(&mapCtx);
                        AppLog("[ParseTest2] sRet=%08X sVal=%d mapRet=%d\n",
                            psCtx.gpr[3], static_cast<int32_t>(sv),
                            static_cast<int32_t>(mapCtx.gpr[3]));
                    }
                    if (found) {
                        Memory::Write32(kScratch, 0xDEADBEEFu);
                        CpuContext parseCtx = *cpu;
                        parseCtx.gpr[3] = found + 2;
                        parseCtx.gpr[4] = 0x808927C8u;
                        parseCtx.gpr[5] = kScratch;
                        CpuContextScope parseScope(&parseCtx);
                        func_80013040(&parseCtx);
                        AppLog("[ParseTest] parse ret=%08X n=%d\n",
                            parseCtx.gpr[3], static_cast<int32_t>(Memory::Read32(kScratch)));
                    }
                }
            }
            // Run the real parser and report what section the game itself picked.
            func_805B9344(cpu);
            AppLog("[BootPath] real parser -> section %d (sm curSec=%d)\n",
                cpu ? static_cast<int32_t>(cpu->gpr[3]) : -1,
                sectionMgr ? static_cast<int32_t>(Memory::Read32(sectionMgr + 4)) : -1);
            // The real parser returns -1 when the launch arguments are not understood, and
            // SectionMgr::init maps -1 to section 63 (0x3F) = the Mario Kart Channel app.
            // MKW_BOOT_SECTION lets us boot a different section (the game's title/menu).
            if (cpu) {
                if (const char* e = std::getenv("MKW_BOOT_SECTION")) {
                    const int forced = std::atoi(e);
                    cpu->gpr[3] = static_cast<uint32_t>(forced);
                    AppLog("[BootPath] overriding boot section -> %d\n", forced);
                } else if (static_cast<int32_t>(cpu->gpr[3]) < 0 && std::getenv("MKW_NATURAL_BOOT") == nullptr) {
                    // The port boots without launch arguments, so the real parser returns -1 and
                    // SectionMgr falls back to the Mario Kart Channel (section 21) - which is why a
                    // bare boot lands on the channel page instead of the title screen.  The retail
                    // game starts on the title section (63) when the disc is launched from the Wii
                    // Menu, so boot that instead and let the game run its own title -> licence ->
                    // main-menu flow.  MKW_BOOT_SECTION / MKW_NATURAL_BOOT still override this.
                    cpu->gpr[3] = 63u;
                    if (sectionMgr) {
                        // Same recipe as the MKW_FORCE_SECTION path below/above: setting only the
                        // parser result leaves SectionMgr with a half-built page set (2 layers, no
                        // title page), while writing the manager fields and the pad ID gives the
                        // full title section (4 layers with Pages::Title on top).
                        Memory::Write32(sectionMgr + 4, 63u);
                        Memory::Write32(sectionMgr + 12, 63u);
                        CpuContext padCtx = *cpu;
                        padCtx.gpr[3] = sectionMgr + 0x34u;
                        padCtx.gpr[4] = 0;
                        padCtx.gpr[5] = 0x124u;
                        CpuContextScope scope(&padCtx);
                        InvokeIndirectCpu(0x8061B490u, &padCtx);
                    }
                    AppLog("[BootPath] no launch arguments -> booting section 63 (title)\n");
                }
            }
            std::fflush(stdout);
            return true;
        }

        SetUpBootRaceScenario(0x08);
        // PRELOAD: section 32 init creates karts immediately, but the ArchiveMgr groups do not
        // become usable until ~frame 100 (about 16 frames after the engine posts the archive
        // requests). Pump the archive-load jobs to completion here, before forcing the section,
        // so kart model lookups resolve against real archives instead of NULL.
        {
            const uint32_t arcBase = Memory::Read32(0x809BD748u);
            AppLog("[Arc Preload] begin frame=%u base=%08X ready=%d\n",
                gx_bridge::g_gxFrame, arcBase, gx_bridge::ArchivesReady() ? 1 : 0);
            uint32_t scanned = 0;
            if (arcBase && !gx_bridge::ArchivesReady()) {
                for (uint32_t k = 0; k < 24; ++k) {
                    const uint32_t e = arcBase + k * 84u;
                    if (!Memory::Contains(e + 904u, 4)) break;
                    const uint32_t h = Memory::Read32(e + 824u);
                    if (h < 0x80000000u || !Memory::Contains(h, 28)) break;
                    scanned = k + 1;
                }
                AppLog("[Arc Preload] %u plausible entries\n", scanned);
            }
        }
        if (sectionMgr != 0) {
            Memory::Write32(sectionMgr + 4, 0x20); // Force current = 0x20 (kSectionP1Vs)
            Memory::Write32(sectionMgr + 12, 0x20); // Force next = 0x20
            CpuContext padCtx = *cpu;
            padCtx.gpr[3] = sectionMgr + 0x34u;
            padCtx.gpr[4] = 0;
            padCtx.gpr[5] = 0x124u;
            CpuContextScope scope(&padCtx);
            InvokeIndirectCpu(0x8061B490u, &padCtx);
        }
        cpu->gpr[3] = 0x20;
        return true;
    }
    case 0x8072624Cu: // KartAIController::IsCPU
        cpu->gpr[3] = 1;
        return true;
    case 0x805A8FBCu: { // RaceCameraMgr::HasEveryOpeningPanEnded
        // NOTE: this used to force a "pan ended" result and write to GUESSED globals
        // (0x809C1874 / 0x809C1998 / 0x809C19A0). Those offsets were never verified and this
        // function is in fact never even called, so the hack gated nothing while corrupting real
        // globals. The real function is now executed normally.
        //
        // Real logic: count = *(mgr+32); for each cam = *(*(mgr+24) + i*4), test bit 0x8 of
        // *(uint16*)(*(cam+408) + 108).
        func_805A8FBC(cpu);
        return true;
    }
    case 0x805A0A1Cu: { // RaceCameraMgr ctor/init - this is what stores *(0x809C1998)
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            AppLog("[CamInit] RaceCameraMgr init CALLED frame=%u r3=%08X\n",
                gx_bridge::g_gxFrame, cpu ? cpu->gpr[3] : 0);
        }
        func_805A0A1C(cpu);
        return true;
    }
    case 0x805A8468u: { // caller of the camera-mgr init
        static bool s_logged2 = false;
        if (!s_logged2) {
            s_logged2 = true;
            const uint32_t rd = Memory::Read32(0x809BD748u);
            AppLog("[CamInit] camera-setup root CALLED frame=%u rd=%08X rd+2928=%u (+2932=%u)\n",
                gx_bridge::g_gxFrame, rd,
                (rd && Memory::Contains(rd + 2928, 4)) ? Memory::Read32(rd + 2928) : 0xFFFFFFFFu,
                (rd && Memory::Contains(rd + 2932, 4)) ? Memory::Read32(rd + 2932) : 0xFFFFFFFFu);
        }
        func_805A8468(cpu);
        return true;
    }
    case 0x805A900Cu: { // RaceCameraMgr::Update - must run every frame
        ++gx_bridge::g_gx.camUpdateCalls;
        // RaceCameraMgr::Update reads the camera-manager POINTER from *(0x809C1998) and a camera
        // MODE BYTE from 0x809C19A0, then calls func_805A0FA0(mgr, mode). Log both: the view has
        // never left the opening pan, so we need to see which mode the game thinks it is in.
        static uint32_t s_lastCamLog = 0;
        const uint32_t camFrame = gx_bridge::g_gxFrame;
        if (camFrame % 40 == 0 && s_lastCamLog != camFrame) {
            s_lastCamLog = camFrame;
            AppLog("[CamMode] frame=%u camMgr=%08X mode=%u r1874=%08X\n",
                camFrame, Memory::Read32(0x809C1998u),
                Memory::Contains(0x809C19A0u, 1) ? Memory::Read8(0x809C19A0u) : 0xFFFFu,
                Memory::Read32(0x809C1874u));
        }
        func_805A900C(cpu);
        return true;
    }
    case 0x80590664u: { // KartLink::IsCPU
        uint32_t obj = cpu->gpr[3];
        if (obj != 0 && Memory::Contains(obj, 8)) {
            uint32_t p = Memory::Read32(obj);
            if (p != 0 && Memory::Contains(p + 4, 4)) {
                uint32_t q = Memory::Read32(p + 4);
                if (q != 0 && Memory::Contains(q + 20, 4)) {
                    cpu->gpr[3] = Memory::Read32(q + 20) & 1u;
                    return true;
                }
            }
        }
        cpu->gpr[3] = 0; // Default to 0 = HUMAN!
        return true;
    }
    case 0x8061B9BCu: // SectionPad::GetFirstDeactivatedPad
        cpu->gpr[3] = 0xFFFFFFFFu;
        return true;
    case 0x801629B0u: { // Complete the boot drive inquiry with a synthetic drive status.
        const auto buffer = cpu->gpr[4];
        if (cpu->gpr[5] != 0x8019F980u || !Memory::Contains(channel, 16)
            || !Memory::Contains(buffer, 32))
            return false;
        std::memset(Memory::GetPointer(buffer, 32), 0, 32);
        Memory::Write32(channel + 12, 0);
        CpuContext callback = *cpu;
        callback.gpr[3] = 32;
        callback.gpr[4] = channel;
        CpuContextScope scope(&callback);
        InvokeIndirectCpu(0x8019F980u, &callback);
        cpu->gpr[3] = 1;
        return true;
    }
    case 0x8015EA1Cu: { // Publish the extracted PAL disc's original FST.
        if (Memory::Read8(0x803866A0u)) return true;
        const auto fstSize = reinterpret_cast<uintptr_t>(kStartupFstEnd) - reinterpret_cast<uintptr_t>(kStartupFst);
        const auto address = Memory::Read32(0x80003128u);
        if (fstSize < 12 || fstSize > 0x200000 || !Memory::Contains(address, fstSize))
            return false;
        std::memcpy(Memory::GetPointer(address, fstSize), kStartupFst, fstSize);
        Memory::Write32(0x80000038u, address);
        Memory::Write32(0x8000003Cu, fstSize);
        Memory::Write32(0x80386720u, 0);
        Memory::Write8(0x80386724u, 1);
        Memory::Write8(0x80386725u, 1);
        for (uint32_t index = 0; index < 4; ++index) {
            const auto context = 0x803434E0u + index * 0x20;
            Memory::Write32(context + 0x0C, 0xFEEBDAEDu);
            Memory::Write32(context + 0x10, index);
        }
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t q = 0x80343230u + i * 8u;
            Memory::Write32(q, q);
            Memory::Write32(q + 4, q);
        }
        InvokeIndirectCpu(0x8015DF1Cu, cpu);
        Memory::Write8(0x803866A0u, 1);
        return true;
    }
    case 0x8019E18Cu: { // NAND initialization and its guest IPC heap.
        if (Memory::Read32(0x80386848u) == 2) {
            cpu->gpr[3] = 0;
            return true;
        }
        const uint32_t lo = Memory::Read32(0x803867ECu);
        const uint32_t hi = Memory::Read32(0x803867F0u);
        const uint32_t heap = (lo + 31u) & ~31u;
        constexpr uint32_t heapSize = 5440u;
        if (lo == 0 || heap > hi || hi - heap < heapSize || !Memory::Contains(heap, heapSize))
            return false;
        constexpr char device[] = "/dev/fs";
        constexpr char home[] = "/title/00010004/524d4350/data";
        std::memcpy(Memory::GetPointer(0x80385928u, sizeof(device)), device, sizeof(device));
        Memory::Write32(0x80385920u, 1);
        Memory::Write32(0x8038677Cu, lo);
        Memory::Write32(0x80386780u, hi);
        Memory::Write32(0x80386774u, heap);
        const auto savedR4 = cpu->gpr[4];
        const auto savedR5 = cpu->gpr[5];
        const auto savedLr = cpu->lr;
        cpu->gpr[3] = heap;
        cpu->gpr[4] = heapSize;
        cpu->lr = 0x80169BCCu;
        InvokeIndirectCpu(0x801949B8u, cpu);
        const auto heapHandle = cpu->gpr[3];
        cpu->gpr[3] = heap + heapSize;
        InvokeIndirectCpu(0x80193040u, cpu);
        cpu->gpr[4] = savedR4;
        cpu->gpr[5] = savedR5;
        cpu->lr = savedLr;
        Memory::Write32(0x80386784u, heapHandle);
        Memory::Write32(0x80386770u, 1);
        std::memcpy(Memory::GetPointer(0x80346D20u, sizeof(home)), home, sizeof(home));
        Memory::Write32(0x80386848u, 2);
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801AB848u: // Match the desktop STM initialization state.
        Memory::Write32(0x80386934u, 1);
        Memory::Write32(0x80386938u, 0x00535401u);
        Memory::Write32(0x8038693Cu, 0x00535402u);
        cpu->gpr[3] = 1;
        return true;
    case 0x80193478u: // Seed the IPC client buffer and reserve its 4 KiB heap.
        Memory::Write32(0x803867ECu, Memory::Read32(0x80003130u) + 0x1000u);
        Memory::Write32(0x803867F0u, Memory::Read32(0x80003134u));
        Memory::Write32(0x803867E8u, 1);
        cpu->gpr[3] = 0;
        return true;
    case 0x801A7EE4u:
    case 0x801A7FC0u: { // Uncontended mutex operations on the startup thread.
        const auto thread = Memory::Read32(0x800000E4u);
        if (channel == 0 || thread == 0) return true;
        const auto owner = Memory::Read32(channel + 8);
        const auto count = Memory::Read32(channel + 12);
        if (target == 0x801A7EE4u) {
            if ((owner != 0 && owner != thread) || count == UINT32_MAX) return false;
            Memory::Write32(channel + 8, thread);
            Memory::Write32(channel + 12, count + 1);
        } else if (owner == thread && count != 0) {
            Memory::Write32(channel + 12, count - 1);
            if (count == 1) Memory::Write32(channel + 8, 0);
        }
        return true;
    }
    case 0x801A98B0u: // OS::GetCurrentThread
        cpu->gpr[3] = Memory::Read32(0x800000E4u);
        return true;
    case 0x801A2098u: // OS::ClearContext
        if (channel != 0) {
            Memory::Write16(channel + 0x1A0u, 0);
            Memory::Write16(channel + 0x1A2u, 0);
            if (Memory::Read32(0x800000D8u) == channel)
                Memory::Write32(0x800000D8u, 0);
        }
        return true;
    case 0x801A1E70u: { // OS::SetCurrentContext
        Memory::Write32(0x800000C0u, channel & 0x3FFFFFFFu);
        Memory::Write32(0x800000D4u, channel);
        if (channel == 0) return true;
        const auto flags = Memory::Read32(channel + 0x19Cu) & ~0x2000u;
        const auto mode = Memory::Read16(channel + 0x1A2u) & ~2u;
        Memory::Write32(channel + 0x19Cu, flags | (Memory::Read32(0x800000D8u) == channel ? 0x2000u : 0u));
        Memory::Write16(channel + 0x1A2u, mode | (interruptsEnabled ? 2u : 0u));
        return true;
    }
    case 0x801A65C0u: {
        const bool previous = interruptsEnabled;
        interruptsEnabled = true;
        cpu->gpr[3] = previous;
        return true;
    }
    case 0x8020FF9Cu: { // EGG::AsyncDisplay::endRender
        static uint32_t s_frameCount = 0;
        ++s_frameCount;
        gx_bridge::PresentFrameTo3DS(s_frameCount);
        // Heartbeat: if the device stops producing frames, the last Beat line shows the frame, the
        // section being booted and how much host heap was left - a frozen screen otherwise says
        // nothing at all.
        if (s_frameCount == 200) {   // one-off: what is actually in the texture cache?
            size_t mipBytes = 0;
            size_t baseBytes = 0;
            for (const auto& kv : gx_bridge::g_texCache) {
                const auto& t = kv.second;
                baseBytes += static_cast<size_t>(t.width) * t.height * 4;
                for (const auto& m : t.mips) mipBytes += m.size() * 4;
            }
            AppLog("[TexDump] entries=%zu base=%zuKB mips=%zuKB accounted=%zuKB cap=%zuKB\n",
                gx_bridge::g_texCache.size(), baseBytes / 1024, mipBytes / 1024,
                gx_bridge::g_texCacheBytes / 1024, gx_bridge::kMaxTexCacheBytes / 1024);
        }
#ifdef __3DS__
        if (s_frameCount % 30 == 0) AppLogFlush();
#endif
#ifdef __3DS__
        if (gx_bridge::g_audioEnabled && s_frameCount == 70u) {
            gx_bridge::AudioStart(gx_bridge::g_audioPathBuf);
        }
#endif
        if (s_frameCount % 20 == 0) {
#ifdef __3DS__
            static u64 s_lastTick = 0;
            const u64 nowTick = osGetTime();
            const double msPerFrame = s_lastTick ? static_cast<double>(nowTick - s_lastTick) / 20.0 : 0.0;
            s_lastTick = nowTick;
            AppLog("[Beat] f=%u fps=%.1f msPerFrame=%.1f", s_frameCount,
                msPerFrame > 0.0 ? 1000.0 / msPerFrame : 0.0, msPerFrame);
#else
            AppLog("[Beat] f=%u", s_frameCount);
#endif
            const uint32_t smBeat = Memory::Read32(0x809C1E38u);
            const uint32_t nextSec = (smBeat && Memory::Contains(smBeat + 16, 4))
                ? Memory::Read32(smBeat + 12) : 0u;
            const uint32_t rootSys = Memory::Read32(0x80385FC8u);
            const uint32_t sceneMgr = (rootSys && Memory::Contains(rootSys + 88, 4))
                ? Memory::Read32(rootSys + 84) : 0u;
            const uint32_t scene = (sceneMgr && Memory::Contains(sceneMgr + 40, 4))
                ? Memory::Read32(sceneMgr + 36) : 0u;
            const uint32_t sceneState = (scene && Memory::Contains(scene + 8, 4))
                ? Memory::Read32(scene + 4) : 0u;
            AppLog(" nextSec=%u sceneState=%u hostHeapFree=%zuKB texCache=%zuKB"
                   " texDecode=%u texHit=%u texEvict=%u texTexels=%u gpu2d=%u quads=%u drawn=%u"
                   " vram=%zuKB up=%u gev=%u base=%u\n",
                nextSec, sceneState, HostHeapFreeKb(), gx_bridge::g_texCacheBytes / 1024,
                gx_bridge::g_texStatDecodes, gx_bridge::g_texStatHits,
                gx_bridge::g_texStatEvictions, static_cast<uint32_t>(gx_bridge::g_texStatTexels),
                gx_bridge::Gpu2dActive() ? 1u : 0u, gx_bridge::g_gpuQuadsRecorded,
                gx_bridge::g_gpuQuadsDrawn, gx_bridge::g_gpuTexBytes / 1024,
                gx_bridge::g_gpuTexUploads, gx_bridge::g_gpuTexEvictions,
                gx_bridge::g_gpuBaseUploads);
            gx_bridge::g_gpuQuadsRecorded = 0;
            gx_bridge::g_gpuBaseUploads = 0;
            gx_bridge::g_texStatDecodes = 0;
            gx_bridge::g_texStatHits = 0;
            gx_bridge::g_texStatEvictions = 0;
            gx_bridge::g_texStatTexels = 0;
        }
        // --- Controller registration ------------------------------------------------
        // MKWii binds a pad inside SectionPad::Update, but only while the current Scene is in
        // its "running" state (Scene+4 == 1) and only when the pad slot is in the *requested*
        // state (SectionPad entry +12 == 1).  Pages::Title::OnActivate opens that window while
        // the section transition is still running (scene state 2), so the request is always
        // dropped there.  On hardware the "press A to connect your controller" page
        // (Pages::RegisterPad / RecognizePadMenu / BlinkingPressA) re-issues
        // SectionPad::RequestPad every frame after that, and the first A press binds the pad.
        // That page is reached through an audio-timed title branch, and this port has no audio
        // clock, so nothing ever re-asks - Pages::Title::AfterControlUpdate then never sees
        // SectionPad::HasAController() == true and the title never advances.
        // Re-issue the request ourselves for the menu path; it is exactly what
        // Pages::RegisterPad::AfterControlUpdate does (state 1, pending 0, holder 0).
        if (!g_raceShortcut) {
            static uint32_t s_lastPadNudge = 0xFFFFFFFFu;
            const uint32_t sectionMgr = Memory::Read32(0x809C1E38u);
            if (sectionMgr != 0u && Memory::Contains(sectionMgr + 52, 20) && s_lastPadNudge != s_frameCount) {
                const uint32_t pad = sectionMgr + 52;
                const uint32_t rootSys = Memory::Read32(0x80385FC8u);
                const uint32_t sceneMgr = (rootSys && Memory::Contains(rootSys + 88, 4))
                    ? Memory::Read32(rootSys + 84) : 0u;
                const uint32_t scene = (sceneMgr && Memory::Contains(sceneMgr + 40, 4))
                    ? Memory::Read32(sceneMgr + 36) : 0u;
                const uint32_t sceneState = (scene && Memory::Contains(scene + 8, 4))
                    ? Memory::Read32(scene + 4) : 0u;
                const uint32_t id = Memory::Read32(pad + 4);
                const uint32_t pending = Memory::Read32(pad + 8);
                const uint32_t state = Memory::Read32(pad + 12);
                // A pad counts as registered only once it has a real ID that matches the pending
                // one.  (The guest's own HasAController also accepts state=1 with id=pending=0,
                // which is the "requested but nothing bound yet" state - that must keep nudging.)
                const bool bound = id != 0u && id == pending;
                (void)state;
                if (bound) {
                    g_padHoldAUntilBound = 0;
                    g_padAPressSeen = false;
                } else {
                    // The registration window only opens once the scene is running (~1s after the
                    // title appears), so watch for the A press on *every* frame and remember it.
                    const uint32_t mgrNow = Memory::Read32(0x809BD70Cu);
                    if (mgrNow != 0u && Memory::Contains(mgrNow, 20000) && !g_padAPressSeen) {
                        for (int i = 0; i < 4 && !g_padAPressSeen; ++i) {
                            const uint32_t cand = mgrNow + 15264u + static_cast<uint32_t>(i) * 176u;
                            if (!Memory::Contains(cand + 100, 4)) continue;
                            if (Memory::Read8(cand + 80) == 0u) continue;
                            if ((Memory::Read16(cand + 32) & 1u) != 0u) g_padAPressSeen = true;
                        }
                    }
                }
                if (sceneState == 1u && !bound) {
                    s_lastPadNudge = s_frameCount;
                    // If A is already held when the registration window opens, mask it for one frame
                    // so the guest sees a fresh press edge and Input::Manager::TrySetController binds
                    // the pad (this is the only path that updates the holder bookkeeping correctly).
                    // A press seen before this window opened is re-armed: one frame with A
                    // released (g_padEdgeMaskFrame) and then A held for a while, which the guest
                    // reads as a fresh press edge and uses to bind the pad.
                    {
                        const uint32_t mgr = Memory::Read32(0x809BD70Cu);
                        if (mgr != 0u && Memory::Contains(mgr, 20000) && g_padHoldAUntilBound == 0) {
                            bool aPressed = g_padAPressSeen;
                            for (int i = 0; i < 4 && !aPressed; ++i) {
                                const uint32_t cand = mgr + 15264u + static_cast<uint32_t>(i) * 176u;
                                if (!Memory::Contains(cand + 100, 4)) continue;
                                if (Memory::Read8(cand + 80) == 0u) continue;
                                if ((Memory::Read16(cand + 32) & 1u) != 0u) aPressed = true;
                            }
                            if (aPressed) {
                                g_padHoldAUntilBound = 90;
                                g_padEdgeMaskFrame = gx_bridge::g_gxFrame + 1u;
                                g_padAPressSeen = false;
                                AppLog("[PadNudge] A seen while unregistered (f=%u) -> re-arming its edge\n",
                                    s_frameCount);
                            }
                        }
                    }
                    // SectionPad::Update only binds on a *fresh* A edge, so a press made while the
                    // section was still transitioning (i.e. before this window opened) is lost and
                    // the player would have to release and press again.  Treat "A is held now" as
                    // the registration press instead: bind the first connected real pad exactly the
                    // way Input::Manager::TrySetController would (RealControllerHolder::
                    // SetController(holder, controller, 0)); the next SectionPad::Update then picks
                    // it up, computes the matching ID and the pad is registered.
                    const uint32_t inputMgr = Memory::Read32(0x809BD70Cu);
                    if (inputMgr != 0u && Memory::Contains(inputMgr, 20000)) {
                        uint32_t candidates[8];
                        for (int i = 0; i < 4; ++i) {
                            candidates[i] = inputMgr + 5920u + static_cast<uint32_t>(i) * 2336u;
                            candidates[4 + i] = inputMgr + 15264u + static_cast<uint32_t>(i) * 176u;
                        }
                        uint32_t chosen = 0;
                        for (const uint32_t cand : candidates) {
                            if (!Memory::Contains(cand + 100, 4)) continue;
                            if (Memory::Read8(cand + 80) == 0u) continue;          // not connected
                            if ((Memory::Read16(cand + 32) & 1u) == 0u) continue;  // A not held
                            const uint32_t vtbl = Memory::Read32(cand);
                            const uint32_t getType = (vtbl && Memory::Contains(vtbl + 20, 4))
                                ? Memory::Read32(vtbl + 16) : 0u;
                            // Only real controllers (GCN 0x8052286C, Wii 0x8052292C); the base
                            // Input::Controller placeholder reports type -1 and is no use here.
                            if (getType != 0x8052286Cu && getType != 0x8052292Cu) continue;
                            chosen = cand;
                            break;
                        }
                        {
                            const uint32_t holderCtrl = Memory::Contains(inputMgr + 12, 4)
                                ? Memory::Read32(inputMgr + 12) : 0u;
                            const uint32_t holderVt = (holderCtrl && Memory::Contains(holderCtrl, 4))
                                ? Memory::Read32(holderCtrl) : 0u;
                            static int s_bindLogs = 0;
                            if (s_bindLogs < 8) {
                                ++s_bindLogs;
                                AppLog("[PadNudge] f=%u gcn0 btn=%04X conn=%u gt=%08X chosen=%08X holderCtrl=%08X vtbl=%08X\n",
                                    s_frameCount,
                                    static_cast<uint16_t>(Memory::Read16(candidates[4] + 32)),
                                    Memory::Read8(candidates[4] + 80),
                                    (Memory::Read32(candidates[4]) && Memory::Contains(Memory::Read32(candidates[4]) + 20, 4))
                                        ? Memory::Read32(Memory::Read32(candidates[4]) + 16) : 0u,
                                    chosen, holderCtrl, holderVt);
                            }
                        }
                        // Kept behind an env flag: calling ControllerParams::Fill from here is not
                        // safe yet (it aborted at frame 106), and the edge-refresh below binds the
                        // pad through the guest's own TrySetController path instead.
                        static const bool s_directBind = std::getenv("MKW_PAD_DIRECTBIND") != nullptr;
                        if (s_directBind && chosen != 0u) {
                            CpuContext bindCtx = *cpu;
                            bindCtx.gpr[3] = inputMgr + 4u;   // pad 0 -> Input::Manager holder 0
                            bindCtx.gpr[4] = chosen;
                            bindCtx.gpr[5] = 0;
                            bindCtx.lr = 0;
                            CpuContextScope bindScope(&bindCtx);
                            InvokeIndirectCpu(0x80521554u, &bindCtx);  // RealControllerHolder::SetController
                            // Finish what Input::Manager::TrySetController's success path does, so
                            // RealControllerHolder::Update does not undo the assignment on the next
                            // frame: mirror the controller pointer into holder+16 and refresh the
                            // ControllerParams copy at holder+204.
                            const uint32_t holderBase = inputMgr + 4u;
                            const uint32_t ctrl = Memory::Contains(holderBase + 12, 4)
                                ? Memory::Read32(holderBase + 8) : 0u;
                            if (ctrl != 0u && Memory::Contains(holderBase + 208, 4)) {
                                Memory::Write32(holderBase + 16, ctrl);
                                CpuContext fillCtx = *cpu;
                                fillCtx.gpr[3] = holderBase + 204u;
                                fillCtx.gpr[4] = ctrl;
                                fillCtx.lr = 0;
                                CpuContextScope fillScope(&fillCtx);
                                InvokeIndirectCpu(0x80522364u, &fillCtx);  // ControllerParams::Fill
                            }
                        }
                    }
                    CpuContext padCtx = *cpu;
                    padCtx.gpr[3] = pad;
                    padCtx.gpr[4] = 0;
                    padCtx.lr = 0;
                    CpuContextScope scope(&padCtx);
                    InvokeIndirectCpu(0x8061B6ECu, &padCtx);   // SectionPad::RequestPad(pad, 0)
                }
            }
        }
        static bool s_profiled100 = false;
        if (!s_profiled100 && s_frameCount >= 100) {
            s_profiled100 = true;
            ReportWorkingSet("Frame 100 (title/menu page creation)");
        }
        if (s_frameCount == 200 || s_frameCount == 400) {
            const uint32_t pm = 0x80AB3A18u;
            const uint32_t page = 0x80AB5EFCu;
            if (page && Memory::Contains(page, 0x44)) {
                std::string dump;
                for (int i = 0; i < 17; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(page + i * 4));
                    dump += buf;
                }
                {
                    const uint32_t vt = (pm && Memory::Contains(pm, 4)) ? Memory::Read32(pm) : 0;
                    std::string vts;
                    if (vt && Memory::Contains(vt, 48)) {
                        for (int i = 0; i < 12; ++i) {
                            char vb[16]; std::snprintf(vb, sizeof(vb), " %08X", Memory::Read32(vt + i * 4));
                            vts += vb;
                        }
                    }
                    AppLog("[SecObj] frame=%u obj=%08X vt=%08X%s\n", s_frameCount, pm, vt, vts.c_str());
                }
                AppLog("[PageFinal] frame=%u pm=%08X page=%08X cgData=%08X cgSorted=%08X cgParent=%08X cgPage=%08X cgSize=%d%s\n",
                    s_frameCount, pm, page,
                    Memory::Read32(page + 0x24), Memory::Read32(page + 0x28),
                    Memory::Read32(page + 0x2C), Memory::Read32(page + 0x30),
                    static_cast<int32_t>(Memory::Read32(page + 0x34)), dump.c_str());
            }
        }
        if (s_frameCount >= 140 && s_frameCount <= 145) {
            const uint32_t page = 0x80AB5EFCu;
            if (Memory::Contains(page, 0x44)) {
                CpuContext c = *cpu;
                c.gpr[3] = page;
                c.gpr[4] = 0;
                const uint32_t qBefore = gx_bridge::g_gx.quads2DThisFrame;
                const uint32_t dBefore = gx_bridge::g_gx.drawCallsThisFrame;
                {
                    CpuContextScope sc(&c);
                    func_80602530(&c);
                }
                AppLog("[PageDrawTest] frame=%u page=%08X q %u->%u draws %u->%u\n",
                    s_frameCount, page, qBefore, gx_bridge::g_gx.quads2DThisFrame,
                    dBefore, gx_bridge::g_gx.drawCallsThisFrame);
            }
        }
        if (s_frameCount >= 120 && s_frameCount <= 125) {
            const uint32_t sys = Memory::Read32(0x80385FC8u);
            const uint32_t scnMgr = (sys && Memory::Contains(sys + 0x54, 4)) ? Memory::Read32(sys + 0x54) : 0;
            const uint32_t scene = (scnMgr && Memory::Contains(scnMgr + 0x0C, 4)) ? Memory::Read32(scnMgr + 0x0C) : 0;
            if (scene && Memory::Contains(scene, 16)) {
                CpuContext drawCtx = *cpu;
                drawCtx.gpr[3] = scene;
                CpuContextScope drawScope(&drawCtx);
                func_8051BC78(&drawCtx);
                AppLog("[SceneDrawTest] frame=%u scene=%08X\n", s_frameCount, scene);
            }
        }
        if (s_frameCount == 100) {
            const uint32_t sys = Memory::Read32(0x80385FC8u);
            const uint32_t scnMgr = (sys && Memory::Contains(sys + 0x54, 4)) ? Memory::Read32(sys + 0x54) : 0;
            const uint32_t scene = (scnMgr && Memory::Contains(scnMgr + 0x0C, 4)) ? Memory::Read32(scnMgr + 0x0C) : 0;
            if (scene && Memory::Contains(scene, 32)) {
                AppLog("[SceneDump] scene=%08X vt=%08X vt0=%08X vt1=%08X vt2=%08X vt3=%08X vt4=%08X vt5=%08X\n",
                    scene, Memory::Read32(scene),
                    Memory::Read32(scene), Memory::Read32(scene + 4), Memory::Read32(scene + 8),
                    Memory::Read32(scene + 12), Memory::Read32(scene + 16), Memory::Read32(scene + 20));
                const uint32_t vt = Memory::Read32(scene);
                if (vt && Memory::Contains(vt, 32)) {
                    AppLog("[SceneDump] vtslots=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                        Memory::Read32(vt), Memory::Read32(vt + 4), Memory::Read32(vt + 8), Memory::Read32(vt + 12),
                        Memory::Read32(vt + 16), Memory::Read32(vt + 20), Memory::Read32(vt + 24), Memory::Read32(vt + 28));
                }
            }
        }
        if (s_frameCount == 100) {
            const uint32_t sys = Memory::Read32(0x80386000u);
            if (sys && Memory::Contains(sys, 128)) {
                std::string dump;
                for (int i = 0; i < 32; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(sys + i * 4));
                    dump += buf;
                }
                AppLog("[SysDump] sys=%08X%s\n", sys, dump.c_str());
            }
        }
        if (s_frameCount >= 84 && s_frameCount <= 130) {
            const uint32_t dvd = Memory::Read32(0x80385FC0u);
            const uint32_t fPtr = (dvd && Memory::Contains(dvd + 0x51, 1)) ? Memory::Read8(dvd + 0x51) : 0xFFu;
            const uint32_t fInline = Memory::Contains(0x80385FC0u + 0x51, 1) ? Memory::Read8(0x80385FC0u + 0x51) : 0xFFu;
            AppLog("[DvdStatus] frame=%u dvd=%08X flagAtPtr=%u flagInline=%u\n", s_frameCount, dvd, fPtr, fInline);
        }
        if (s_frameCount == 90) {
            const uint32_t sys = Memory::Read32(0x80385FC8u);
            const uint32_t scnMgr = (sys && Memory::Contains(sys + 0x54, 4)) ? Memory::Read32(sys + 0x54) : 0;
            const uint32_t scene = (scnMgr && Memory::Contains(scnMgr + 0x0C, 4)) ? Memory::Read32(scnMgr + 0x0C) : 0;
            const uint32_t parent = (scene && Memory::Contains(scene + 0x20, 4)) ? Memory::Read32(scene + 0x20) : 0;
            for (int which = 0; which < 2; ++which) {
                const uint32_t obj = which == 0 ? scene : parent;
                if (!obj || !Memory::Contains(obj, 4)) continue;
                const uint32_t vt = Memory::Read32(obj);
                std::string vts;
                if (vt && Memory::Contains(vt, 64)) {
                    for (int i = 0; i < 16; ++i) {
                        char vb[16]; std::snprintf(vb, sizeof(vb), " %08X", Memory::Read32(vt + i * 4));
                        vts += vb;
                    }
                }
                AppLog("[SceneVT] %s obj=%08X id=%d vt=%08X%s\n", which == 0 ? "child" : "parent",
                    obj, static_cast<int32_t>(Memory::Read32(obj + 0x28)), vt, vts.c_str());
            }
        }
        if (s_frameCount == 60) {
            const uint32_t sys = Memory::Read32(0x80385FC8u);
            const uint32_t scnMgr = (sys && Memory::Contains(sys + 0x54, 4)) ? Memory::Read32(sys + 0x54) : 0;
            const uint32_t scene = (scnMgr && Memory::Contains(scnMgr + 0x0C, 4)) ? Memory::Read32(scnMgr + 0x0C) : 0;
            if (scene && Memory::Contains(scene, 4)) {
                const uint32_t vt = Memory::Read32(scene);
                std::string vts;
                if (vt && Memory::Contains(vt, 64)) {
                    for (int i = 0; i < 16; ++i) {
                        char vb[16]; std::snprintf(vb, sizeof(vb), " %08X", Memory::Read32(vt + i * 4));
                        vts += vb;
                    }
                }
                AppLog("[BootScene] scene=%08X vt=%08X%s\n", scene, vt, vts.c_str());
            }
        }
        if (s_frameCount == 84) {
            const uint32_t sm = Memory::Read32(0x80386000u);
            if (sm && Memory::Contains(sm + 0x70, 0x40)) {
                AppLog("[LaunchType] sm=%08X launchType=%u lang=%u aspect=%u prog=%u rgb60=%u b50=%d ndev=%d\n",
                    sm, Memory::Read32(sm + 0x70), Memory::Read32(sm + 0x5C),
                    Memory::Read32(sm + 0x58), Memory::Read32(sm + 0x64),
                    Memory::Read32(sm + 0x68), static_cast<int32_t>(Memory::Read32(sm + 0x6C)),
                    Memory::Read8(sm + 0x80));
            }
        }
        if (s_frameCount >= 60 && s_frameCount <= 100) {
            const uint32_t sys = Memory::Read32(0x80385FC8u);
            const uint32_t scnMgr = (sys && Memory::Contains(sys + 0x54, 4)) ? Memory::Read32(sys + 0x54) : 0;
            const int32_t curScene = (scnMgr && Memory::Contains(scnMgr + 0x18, 4))
                ? static_cast<int32_t>(Memory::Read32(scnMgr + 0x18)) : -999;
            const int32_t nextScene = (scnMgr && Memory::Contains(scnMgr + 0x14, 4))
                ? static_cast<int32_t>(Memory::Read32(scnMgr + 0x14)) : -999;
            const uint32_t scene = (scnMgr && Memory::Contains(scnMgr + 0x0C, 4)) ? Memory::Read32(scnMgr + 0x0C) : 0;
            const int32_t sceneId = (scene && Memory::Contains(scene + 0x28, 4)) ? static_cast<int32_t>(Memory::Read32(scene + 0x28)) : -999;
            const uint32_t parent = (scene && Memory::Contains(scene + 0x20, 4)) ? Memory::Read32(scene + 0x20) : 0;
            const int32_t parentId = (parent && Memory::Contains(parent + 0x28, 4)) ? static_cast<int32_t>(Memory::Read32(parent + 0x28)) : -999;
            AppLog("[SceneMgr] frame=%u sys=%08X scnMgr=%08X curScene=%d nextScene=%d scene=%08X sceneId=%d parent=%08X parentId=%d\n",
                s_frameCount, sys, scnMgr, curScene, nextScene, scene, sceneId, parent, parentId);
        }
        if (s_frameCount == 100) {
            const uint32_t gm = Memory::Read32(0x809BD6F8u);
            if (gm && Memory::Contains(gm, 160)) {
                std::string dump;
                for (int i = 0; i < 40; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(gm + i * 4));
                    dump += buf;
                }
                AppLog("[GateDump] gm=%08X%s\n", gm, dump.c_str());
            }
        }
        if (s_frameCount == 90) {
            const uint32_t mgr = Memory::Read32(0x809BD748u);
            if (mgr && Memory::Contains(mgr, 96)) {
                std::string dump;
                for (int i = 0; i < 24; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(mgr + i * 4));
                    dump += buf;
                }
                AppLog("[ArcMgrDump] mgr=%08X%s\n", mgr, dump.c_str());
            }
        }
        if (s_frameCount >= 82 && s_frameCount <= 92) {
#ifdef __3DS__
            AppLog("[FrameMarker] frame=%u appFree=%zuKB hostHeapFree=%zuKB texCache=%zuKB\n", s_frameCount,
                static_cast<size_t>(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024),
                HostHeapFreeKb(), gx_bridge::g_texCacheBytes / 1024);
#else
            AppLog("[FrameMarker] frame=%u\n", s_frameCount);
#endif
            std::fflush(stdout);
        }
        // Experiment: re-open the SectionPad request window ("RequestPad" semantics:
        // pending=0, state=1, holder=0) at a chosen frame, so the guest's own
        // TrySetController bind path can run once its gate reads 1 (~f120).
        if (const char* poke = std::getenv("MKW_POKE_PADREQ")) {
            const long at = std::strtol(poke, nullptr, 10);
            if (at >= 0 && s_frameCount == static_cast<uint32_t>(at)) {
                const uint32_t smp2 = Memory::Read32(0x809C1E38u);
                if (smp2 && Memory::Contains(smp2 + 52, 32)) {
                    const uint32_t p0 = smp2 + 52;
                    AppLog("[PadPoke] f=%u re-request pad0 (was id=%08X pend=%08X state=%u)\n",
                        s_frameCount, Memory::Read32(p0 + 4), Memory::Read32(p0 + 8),
                        Memory::Read32(p0 + 12));
                    Memory::Write32(p0 + 8, 0);
                    Memory::Write32(p0 + 12, 1);
                    Memory::Write32(p0 + 16, 0);
                }
            }
        }
        {
            static uint32_t s_lastG1 = 0xFFFFFFFFu, s_lastF1 = 0xFFFFFFFFu;
            const uint32_t g1b = Memory::Read32(0x809C2328u);
            const uint32_t f1b = (g1b && Memory::Contains(g1b + 60, 1)) ? Memory::Read8(g1b + 56) : 0xEE;
            if (g1b != s_lastG1 || f1b != s_lastF1) {
                s_lastG1 = g1b; s_lastF1 = f1b;
                const uint32_t vt = (g1b && Memory::Contains(g1b, 4)) ? Memory::Read32(g1b) : 0;
                AppLog("[G2328] f=%u g1=%08X flag56=%u vtbl=%08X\n", s_frameCount, g1b, f1b, vt);
                if (vt && Memory::Contains(vt, 32)) {
                    for (int i = 0; i < 8; ++i) {
                        AppLog("[G2328]   vt[%d]=%08X\n", i, Memory::Read32(vt + i * 4));
                    }
                }
                if (g1b && Memory::Contains(g1b, 128)) {
                    for (int i = 0; i < 128; i += 16) {
                        AppLog("[G2328]   +%02X: %08X %08X %08X %08X\n", i,
                            Memory::Read32(g1b + i), Memory::Read32(g1b + i + 4),
                            Memory::Read32(g1b + i + 8), Memory::Read32(g1b + i + 12));
                    }
                }
            }
        }
        if (s_frameCount >= 60 && s_frameCount <= 400) {
            static uint32_t s_lastFlags = 0xFFFFFFFFu;
            const uint32_t g1 = Memory::Read32(0x809C2328u);
            const uint32_t sm3 = Memory::Read32(0x809C1E38u);
            const uint32_t o2 = (sm3 && Memory::Contains(sm3 + 156, 4)) ? Memory::Read32(sm3 + 152) : 0;
            const uint32_t f1 = (g1 && Memory::Contains(g1 + 60, 1)) ? Memory::Read8(g1 + 56) : 0xEE;
            const uint32_t f2 = (o2 && Memory::Contains(o2 + 1232, 1)) ? Memory::Read8(o2 + 1228) : 0xEE;
            const uint32_t flags = (f1 << 8) | f2 | ((sm3 && Memory::Contains(sm3 + 52, 4))
                ? (Memory::Read32(sm3 + 48) << 16) : 0);
            if (flags != s_lastFlags) {
                s_lastFlags = flags;
                AppLog("[TitleGate] f=%u flag1(audio+56)=%u flag2(sm+152->+1228)=%u sm+48=%d obj2=%08X\n",
                    s_frameCount, f1, f2,
                    (sm3 && Memory::Contains(sm3 + 52, 4)) ? static_cast<int32_t>(Memory::Read32(sm3 + 48)) : -999,
                    o2);
            }
        }
        if (s_frameCount == 150) {
            const uint32_t o1 = 0x8042AA6Cu;
            const uint32_t o2 = 0x8042AAACu;
            if (Memory::Contains(o1, 64) && Memory::Contains(o2, 64)) {
                AppLog("[PadSys] objA=%08X x(A+4)=%08X\n", o1, Memory::Read32(o1 + 4));
                for (int i = 0; i < 12; ++i) {
                    AppLog("[PadSys]   A+%02X=%08X\n", i * 4, Memory::Read32(o1 + i * 4));
                }
                for (int i = 0; i < 12; ++i) {
                    AppLog("[PadSys]   B+%02X=%08X\n", i * 4, Memory::Read32(o2 + i * 4));
                }
            }
        }
        if (s_frameCount == 92) {
            const uint32_t M = Memory::Read32(0x809BD70Cu);
            const uint32_t S = Memory::Read32(0x809C1E38u);
            if (M && S && Memory::Contains(M, 20000) && Memory::Contains(S + 52, 32)) {
                const uint32_t sp = S + 52;
                AppLog("[PadMap] M=%08X S=%08X S+16(holder)=%08X\n", M, sp, Memory::Read32(sp + 16));
                for (int k = 0; k < 2; ++k) {
                    for (int off = 0; off <= 16; off += 4) {
                        const uint32_t c = M + 236 * k + off;
                        const uint32_t v = Memory::Contains(c, 4) ? Memory::Read32(c) : 0;
                        AppLog("[PadMap]   M+%d*236+%02d = %08X vtbl=%08X\n", k, off, v,
                            (v && Memory::Contains(v, 4)) ? Memory::Read32(v) : 0);
                    }
                }
            }
        }
        if (s_frameCount >= 83 && s_frameCount <= 95) {
            const uint32_t mgr2 = Memory::Read32(0x809BD70Cu);
            const uint32_t pad2 = Memory::Read32(0x809C1E38u);
            if (mgr2 && Memory::Contains(mgr2, 20000) && pad2 && Memory::Contains(pad2 + 52, 32)) {
                const uint32_t g0 = mgr2 + 15264;
                const uint32_t hold2 = Memory::Read32(pad2 + 52 + 16);
                const uint32_t ctrl2 = (hold2 && Memory::Contains(hold2 + 12, 4)) ? Memory::Read32(hold2 + 8) : 0;
                AppLog("[PadEdge] f=%u gcn0.btn=%04X gcn0.prev=%04X gcn0.conn=%u wii0.btn=%04X "
                       "pad.state=%u holder=%08X ctrl=%08X\n",
                    s_frameCount, Memory::Read16(g0 + 32), Memory::Read16(g0 + 96), Memory::Read8(g0 + 80),
                    Memory::Read16(mgr2 + 5920 + 32), Memory::Read32(pad2 + 52 + 12), hold2, ctrl2);
            }
        }
        if ((s_frameCount == 200)) {
            const uint32_t mgr = Memory::Read32(0x809BD70Cu);
            if (mgr && Memory::Contains(mgr, 20000)) {
                AppLog("[PadCand] f=%u mgr=%08X\n", s_frameCount, mgr);
                for (int i = 0; i < 4; ++i) {
                    const uint32_t c = mgr + i * 2336 + 5920;
                    if (!Memory::Contains(c + 100, 4)) continue;
                    const uint32_t vt = Memory::Read32(c);
                    AppLog("[PadCand]   wii%d obj=%08X vtbl=%08X getType=%08X btn=%04X prev=%04X conn=%u\n",
                        i, c, vt, (vt && Memory::Contains(vt + 20, 4)) ? Memory::Read32(vt + 16) : 0,
                        Memory::Read16(c + 32), Memory::Read16(c + 96), Memory::Read8(c + 80));
                }
                for (int i = 0; i < 4; ++i) {
                    const uint32_t c = mgr + i * 176 + 15264;
                    if (!Memory::Contains(c + 100, 4)) continue;
                    const uint32_t vt = Memory::Read32(c);
                    AppLog("[PadCand]   gcn%d obj=%08X vtbl=%08X getType=%08X btn=%04X prev=%04X conn=%u\n",
                        i, c, vt, (vt && Memory::Contains(vt + 20, 4)) ? Memory::Read32(vt + 16) : 0,
                        Memory::Read16(c + 32), Memory::Read16(c + 96), Memory::Read8(c + 80));
                }
            }
        }
        if (s_frameCount >= 84 && s_frameCount <= 220 &&
            (s_frameCount % 20 == 0 || s_frameCount == 85 || s_frameCount == 91)) {
            {
                const uint32_t g = Memory::Read32(0x80385FC8u);
                const uint32_t ga = (g && Memory::Contains(g + 88, 4)) ? Memory::Read32(g + 84) : 0;
                const uint32_t gb = (ga && Memory::Contains(ga + 40, 4)) ? Memory::Read32(ga + 36) : 0;
                const uint32_t gv = (gb && Memory::Contains(gb, 4)) ? Memory::Read32(gb) : 0;
                AppLog("[PadGate] f=%u obj=%08X gate(*(obj+4))=%08X vtbl=%08X slot3=%08X\n",
                    s_frameCount, gb, (gb && Memory::Contains(gb + 8, 4)) ? Memory::Read32(gb + 4) : 0,
                    gv, (gv && Memory::Contains(gv + 16, 4)) ? Memory::Read32(gv + 12) : 0);
            }
        }
        if (g_padCallCounts && (s_frameCount == 90 || s_frameCount == 100 || s_frameCount == 150 ||
                                s_frameCount == 200 || s_frameCount == 260)) {
            static const char* const kNm[] = {
                "RequestPad", "ResetAllPads", "SetID", "Update", "HasAController",
                "GetCtrlHolder", "ResetPadsStatus", "ResetPadsStatusAndSet54", "Mgr::ResetRealCtrl",
                "ResetPad", "HasCtrlChanged", "RecognizePad::ct", "Mgr::Update",
                "Mgr::UpdateControllers", "Mgr::Init", "Mgr::Prepare", "WiiCtrl::UpdateImpl",
                "GCNCtrl::UpdateImpl", "Mgr::ResetKPAD", "Mgr::CopyPADStatus", "Mgr::SyncWiimotes",
                "Mgr::ResetSelfWiiGCN", "Ctrl::Init", "WiiCtrl::Init", "GCNCtrl::Init",
                "Holder::Init", "RealHolder::Update", "Mgr::InitCtrlHolders", "Mgr::TrySetCtrl",
                "RealHolder::SetCtrl"};
            AppLog("[PadCalls] frame=%u", s_frameCount);
            for (int i = 0; i < 30; ++i) {
                if (g_padCallCounts[i]) AppLog(" %s=%u", kNm[i], g_padCallCounts[i]);
            }
            AppLog("\n");
        }
        if (s_frameCount >= 84 && s_frameCount <= 260) {
            static uint32_t s_lastCtrl[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
            const uint32_t smh = Memory::Read32(0x809C1E38u);
            if (smh && Memory::Contains(smh + 52, 32)) {
                const uint32_t hold = Memory::Read32(smh + 52 + 16);
                const uint32_t ctrl = (hold && Memory::Contains(hold + 12, 4)) ? Memory::Read32(hold + 8) : 0;
                const uint32_t vt = (ctrl && Memory::Contains(ctrl, 4)) ? Memory::Read32(ctrl) : 0;
                if (ctrl != s_lastCtrl[0] || vt != s_lastCtrl[1]) {
                    s_lastCtrl[0] = ctrl; s_lastCtrl[1] = vt;
                    AppLog("[Holder] frame=%u holder=%08X ctrl=%08X vtbl=%08X\n",
                        s_frameCount, hold, ctrl, vt);
                }
            }
        }
        if (s_frameCount >= 84 && s_frameCount <= 260) {
            static uint32_t s_lastPad[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
            const uint32_t smq = Memory::Read32(0x809C1E38u);
            if (smq && Memory::Contains(smq + 52, 32)) {
                const uint32_t pad = smq + 52;
                const uint32_t id0 = Memory::Read32(pad + 4);
                const uint32_t pend0 = Memory::Read32(pad + 8);
                const uint32_t st0 = Memory::Read32(pad + 12);
                const uint32_t hold0 = Memory::Read32(pad + 16);
                if (id0 != s_lastPad[0] || pend0 != s_lastPad[1] ||
                    st0 != s_lastPad[2] || hold0 != s_lastPad[3]) {
                    s_lastPad[0] = id0; s_lastPad[1] = pend0;
                    s_lastPad[2] = st0; s_lastPad[3] = hold0;
                    AppLog("[PadFlow] frame=%u id0=%08X pend0=%08X state0=%u holder0=%08X\n",
                        s_frameCount, id0, pend0, st0, hold0);
                }
            }
        }
        if (s_frameCount >= 190 && s_frameCount <= 240 && s_frameCount % 2 == 0) {
            const uint32_t smp = Memory::Read32(0x809C1E38u);
            if (smp && Memory::Contains(smp + 52, 96)) {
                const uint32_t pad = smp + 52;
                AppLog("[PadState] frame=%u cur0=%08X new0=%08X st0=%08X\n", s_frameCount,
                    Memory::Read32(pad + 4), Memory::Read32(pad + 8), Memory::Read32(pad + 12));
                // SectionPad entry layout: holder@+16 (== entry[1].+0), id@+4, pending@+8, state@+12.
                for (int k = 0; k < 2; ++k) {
                    const uint32_t e = pad + 16 * k;
                    const uint32_t holder = Memory::Read32(e + 16);
                    AppLog("[PadState]   e%d id=%08X pend=%08X state=%u holder=%08X\n", k,
                        Memory::Read32(e + 4), Memory::Read32(e + 8), Memory::Read32(e + 12), holder);
                    if (holder && Memory::Contains(holder + 12, 4)) {
                        const uint32_t dev = Memory::Read32(holder + 8);
                        const uint32_t vtbl = (dev && Memory::Contains(dev, 4)) ? Memory::Read32(dev) : 0;
                        AppLog("[PadState]     holder+8(dev)=%08X vtbl=%08X slot4=%08X slot8=%08X\n",
                            dev, vtbl,
                            (vtbl && Memory::Contains(vtbl + 16, 4)) ? Memory::Read32(vtbl + 16) : 0,
                            (vtbl && Memory::Contains(vtbl + 32, 4)) ? Memory::Read32(vtbl + 32) : 0);
                    }
                }
            }
        }
        if (s_frameCount >= 80 && s_frameCount <= 400 && s_frameCount % 20 == 0) {
            // func_80554E14 reads *(0x809C0000 - 10504) = *(0x809BD6F8), then +76.
            const uint32_t gateMgr = Memory::Read32(0x809BD6F8u);
            const uint32_t gateFlag = (gateMgr && Memory::Contains(gateMgr + 76, 1)) ? Memory::Read8(gateMgr + 76) : 0xFFu;
            const uint32_t arcMgr = Memory::Read32(0x809BD748u);
            AppLog("[PageGate] frame=%u gateMgr=%08X flag=%u arcMgr=%08X\n", s_frameCount, gateMgr, gateFlag, arcMgr);
            // Section page-layer stack: count at section+892, stack at section+852.
            // Section::IsPageTopLayer() gates Pages::Title::AfterControlUpdate's input check on it,
            // so an empty stack means the title page can never acknowledge a button press.
            const uint32_t smp = Memory::Read32(0x809C1E38u);
            const uint32_t sec = (smp && Memory::Contains(smp, 4)) ? Memory::Read32(smp) : 0;
            const uint32_t layerCount = (sec && Memory::Contains(sec + 892, 4)) ? Memory::Read32(sec + 892) : 0xFFFFFFFFu;
            const uint32_t topPage = (layerCount != 0xFFFFFFFFu && layerCount >= 1 && layerCount <= 32 &&
                                      Memory::Contains(sec + 852 + 4 * (layerCount - 1), 4))
                ? Memory::Read32(sec + 852 + 4 * (layerCount - 1)) : 0;
            AppLog("[PageLayer] frame=%u sec=%08X count=%u top=%08X\n", s_frameCount, sec, layerCount, topPage);
            if (smp && Memory::Contains(smp + 52, 96)) {
                const uint32_t pad = smp + 52;
                for (int i = 0; i < 4; ++i) {
                    const uint32_t e = pad + i * 16;
                    AppLog("[PageLayer]   pad[%d] +0=%08X +4=%08X +8=%08X +C=%08X\n", i,
                        Memory::Read32(e), Memory::Read32(e + 4),
                        Memory::Read32(e + 8), Memory::Read32(e + 12));
                }
                AppLog("[PageLayer]   pad+84=%08X pad+88=%08X pad+92=%08X\n",
                    Memory::Read32(pad + 84), Memory::Read32(pad + 88), Memory::Read32(pad + 92));
            }
            if (topPage && Memory::Contains(topPage, 848)) {
                AppLog("[PageLayer]   top+8=%08X top+828=%u top+832=%d top+840=%u top+844=%u\n",
                    Memory::Read32(topPage + 8), Memory::Read8(topPage + 828),
                    static_cast<int32_t>(Memory::Read32(topPage + 832)),
                    Memory::Read8(topPage + 840), Memory::Read32(topPage + 844));
            }
        }
        if (s_frameCount <= 20 || s_frameCount % 40 == 0) {
            const uint32_t sm = Memory::Read32(0x809C1E38u);
            const uint32_t curSecId = sm ? Memory::Read32(sm + 4) : 0;
            const uint32_t prevSecId = sm ? Memory::Read32(sm + 8) : 0;
            const uint32_t nextSecId = sm ? Memory::Read32(sm + 12) : 0;
            const uint32_t transState = sm ? Memory::Read32(sm + 48) : 0;
            const uint32_t sm2 = 0x908C3ED0u;
            AppLog("[Game Frame %u] sm=%08X curSec=%u nextSec=%u state=%u (retrace=%u) sm2=%08X sec2=%d next2=%d\n",
                s_frameCount, sm, curSecId, nextSecId, transState, Memory::Read32(0x80386be4u),
                sm2, static_cast<int32_t>(Memory::Read32(sm2 + 4)), static_cast<int32_t>(Memory::Read32(sm2 + 12)));
            std::fflush(stdout);
        }
        // Diagnostic: dump UTF-16BE ASCII strings from guest RAM once the menu is up. MKWii's
        // TextBox panes store their text as UTF-16, so this reveals the message-window text and
        // any page labels even when glyph drawing is broken. Host-only (MKW_SCAN_TEXT=1).
        static bool s_textScanned = false;
        if (!s_textScanned && s_frameCount >= 130 && std::getenv("MKW_SCAN_TEXT")) {
            s_textScanned = true;
            std::set<std::string> seen;
            const uint32_t ranges[2][2] = {{0x80000000u, 0x81800000u}, {0x90000000u, 0x92000000u}};
            for (const auto& range : ranges) {
                uint32_t addr = range[0];
                while (addr + 2 < range[1]) {
                    if (!Memory::Contains(addr, 2)) { addr += 0x1000; continue; }
                    const uint32_t hi = Memory::Read8(addr);
                    const uint32_t lo = Memory::Read8(addr + 1);
                    if (hi == 0 && lo >= 0x20 && lo < 0x7F) {
                        std::string text;
                        uint32_t cursor = addr;
                        while (cursor + 1 < range[1] && Memory::Contains(cursor, 2) &&
                               Memory::Read8(cursor) == 0) {
                            const uint32_t c = Memory::Read8(cursor + 1);
                            if (c < 0x20 || c >= 0x7F) break;
                            text.push_back(static_cast<char>(c));
                            cursor += 2;
                        }
                        if (text.size() >= 8 && seen.size() < 400 && seen.insert(text).second) {
                            AppLog("[GuestText] %08X '%s'\n", addr, text.c_str());
                        }
                        addr = cursor > addr ? cursor : addr + 2;
                        continue;
                    }
                    addr += 2;
                }
            }
            std::fflush(stdout);
        }
        if (s_frameCount == 120) {
            const uint32_t sm = Memory::Read32(0x809C1E38u);
            if (sm && Memory::Contains(sm, 128)) {
                std::string dump;
                for (int i = 0; i < 24; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), " %08X", Memory::Read32(sm + i * 4));
                    dump += buf;
                }
                AppLog("[SectionMgrDump] sm=%08X%s\n", sm, dump.c_str());
                // Follow the first few pointers that look like MEM2 objects and show their vtable.
                for (int i = 0; i < 24; ++i) {
                    const uint32_t v = Memory::Read32(sm + i * 4);
                    if (v >= 0x90000000u && v < 0x92000000u && Memory::Contains(v, 8)) {
                        AppLog("[SectionMgrDump] +%02X -> %08X vt=%08X\n", i * 4, v, Memory::Read32(v));
                    }
                }
            }
        }
        // MKW_FORCE_SECTION: switch the section manager to an arbitrary section id using the
        // same path as the race shortcut, to find the game's title/menu section.
        static bool s_forcedSection = false;
        if (!s_forcedSection && s_frameCount >= 90) {
            if (const char* e = std::getenv("MKW_FORCE_SECTION")) {
                s_forcedSection = true;
                const uint32_t sec = static_cast<uint32_t>(std::atoi(e));
                const uint32_t sm = Memory::Read32(0x809C1E38u);
                if (sm) {
                    Memory::Write32(sm + 4, sec);
                    Memory::Write32(sm + 12, sec);
                    CpuContext padCtx = *cpu;
                    padCtx.gpr[3] = sm + 0x34u;
                    padCtx.gpr[4] = 0;
                    padCtx.gpr[5] = 0x124u;
                    CpuContextScope scope(&padCtx);
                    InvokeIndirectCpu(0x8061B490u, &padCtx);
                    AppLog("[SectionForce] frame=%u -> section %u (sm=%08X)\n", s_frameCount, sec, sm);
                }
            }
        }
        if (s_frameCount == 280) {
            const auto& P = gx_bridge::g_gx.proj;
            AppLog("[TitleProj] type=%u P=(%g,%g,%g,%g)(%g,%g,%g,%g)(%g,%g,%g,%g)(%g,%g,%g,%g)\n",
                gx_bridge::g_gx.projType,
                P[0][0], P[0][1], P[0][2], P[0][3], P[1][0], P[1][1], P[1][2], P[1][3],
                P[2][0], P[2][1], P[2][2], P[2][3], P[3][0], P[3][1], P[3][2], P[3][3]);
            for (int m = 0; m < 10; ++m) {
                const float* M = gx_bridge::g_gx.pnMtx[m];
                AppLog("[TitleMtx %d] r0=(%g,%g,%g,%g) r1=(%g,%g,%g,%g) r2=(%g,%g,%g,%g)\n",
                    m, M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11]);
            }
        }
        static bool s_reported600 = false;
        if (!s_reported600 && s_frameCount >= 600) {
            s_reported600 = true;
            AppLog("[Frame 600 Checkpoint Reached] Running uncapped continuously\n");
            ReportWorkingSet("Frame 600 Stable");
            std::fflush(stdout);
        }
        // The RaceCameraMgr is never allocated in this boot path: func_805A8468 only creates it
        // when *(racedata+0xB70) == 11, and even with that satisfied the call is not reached.
        // func_805A0A1C allocates 44 bytes and installs itself at *(0x809C1998), so create it here
        // once, then let RaceCameraMgr::Update drive it.
        static bool s_camBootTried = false;
        // Only create the race camera manager in the race section. Doing it in the title/menu
        // path installs a race camera that overrides the scene's own camera.
        const uint32_t camSecMgr = Memory::Read32(0x809C1E38u);
        const bool camRaceSection = camSecMgr && Memory::Read32(camSecMgr + 4) == 0x20u;
        if (camRaceSection && !s_camBootTried && s_frameCount >= 88 && Memory::Read32(0x809C1998u) == 0) {
            s_camBootTried = true;
            CpuContext camCtx = *cpu;
            camCtx.gpr[3] = 0;
            {
                CpuContextScope camScope(&camCtx);
                func_805A0A1C(&camCtx);
            }
            AppLog("[CamBoot] frame=%u created camMgr=%08X\n",
                s_frameCount, Memory::Read32(0x809C1998u));
            std::fflush(stdout);
        }

        // FINDING (documented in docs/CLAUDE_FABLE_52_TASK.md O2): section 32 init runs at frame ~84
        // but the ArchiveMgr groups only become usable at ~frame 100, so kart creation resolves
        // every model against a NULL archive and is never retried.
        // Re-entering section 32 once the archives were ready was tried here and REGRESSED the run
        // (stopped advancing at ~frame 100), so it has been removed. The fix has to make either
        // (a) the archive load complete before section init, or (b) kart creation retry later.
        cpu->gpr[3] = 0;
        return true;
    }
    case 0x801A65D4u: {
        const bool previous = interruptsEnabled;
        interruptsEnabled = channel != 0;
        cpu->gpr[3] = previous;
        return true;
    }
    default:
        if (gx_bridge::g_gxFrame >= 100 && gx_bridge::g_gxFrame <= 110) {
            static std::set<uint32_t> s_seenRangeTargets;
            if (s_seenRangeTargets.size() < 70 && s_seenRangeTargets.insert(target).second) {
                AppLog("[HleRange] frame=%u target=%08X r3=%08X r4=%08X r5=%08X r6=%08X lr=%08X\n",
                    gx_bridge::g_gxFrame, target, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->lr);
            }
        }
        // --- Range-based HLE for hardware/SDK subsystems ---
        // GX: Graphics subsystem (0x8016B000 - 0x80174000)
        if (target >= 0x8016B000u && target < 0x80174000u) {
            if (target == 0x8016F640u) { // GXGetNumXfbLines
                return true; // efbHeight already in gpr[3]
            }
            if (target == 0x8016F6CCu) { // GXGetYScaleFactor
                cpu->gpr[3] = 0x3F800000u; // 1.0f
                cpu->fpr[1].d = 1.0;
                return true;
            }
            if (target == 0x80170614u) { // GXGetTexBufferSize
                cpu->gpr[3] = 0x20000u;
                return true;
            }
            cpu->gpr[3] = 0;
            return true;
        }
        // WPAD: Wii Remote subsystem (0x801BC000 - 0x801C5000)
        if (target >= 0x801BC000u && target < 0x801C5000u) {
            cpu->gpr[3] = 0;
            return true;
        }
        // PAD: GameCube Controller subsystem (0x801AE600 - 0x801B0200)
        if (target >= 0x801AE600u && target < 0x801B0200u) {
            cpu->gpr[3] = 0;
            return true;
        }
        // AX / AI: Audio DSP / Interface subsystem (0x80123000 - 0x8012F000)
        if (target >= 0x80123000u && target < 0x8012F000u) {
            if (target == 0x8012E5A4u) { // PPCHalt
                std::fprintf(stderr, "[PPCHalt called at pc=%08X lr=%08X]\n", cpu ? cpu->pc : 0, cpu ? cpu->lr : 0);
                return false;
            }
            cpu->gpr[3] = 0;
            return true;
        }
        // SC: System Configuration (0x801B0180 - 0x801B2500)
        if (target >= 0x801B0180u && target < 0x801B2500u) {
            cpu->gpr[3] = 0;
            return true;
        }
        return false;
    }
    cpu->gpr[3] = 1;
    return true;
}

bool handle_unlinked_dispatch(uint32_t target, CpuContext* cpu)
{
    const uint32_t pc = cpu ? cpu->pc : 0;
    const uint32_t lr = cpu ? cpu->lr : 0;

    // 1. UIControl::Calc / OnUpdate / ForEach delegates
    if (pc == 0x8063D0CCu || (lr >= 0x805B0000u && lr <= 0x805D0000u)) {
        static uint32_t s_lastLogTarget = 0;
        if (s_lastLogTarget != target) {
            s_lastLogTarget = target;
            AppLog("[Batch HLE] UIControl::Calc bypassed target=%08X (pc=%08X lr=%08X)\n", target, pc, lr);
        }
        return true;
    }

    // 2. All UI Controls, Message Windows, Dialogs, Wipes, & Pages (0x805B0000 - 0x80680000)
    if (target >= 0x805B0000u && target < 0x80680000u) {
        static uint32_t s_lastUiTarget = 0;
        if (s_lastUiTarget != target) {
            s_lastUiTarget = target;
            AppLog("[Batch HLE] UI Subsystem bypassed target=%08X (pc=%08X lr=%08X r3=%08X)\n",
                target, pc, lr, cpu ? cpu->gpr[3] : 0);
        }
        // If it's IsVisible for RecognizePad or dialogs, return false (0)
        if (target == 0x8061C518u) {
            if (cpu) cpu->gpr[3] = 0;
        } else {
            if (cpu) cpu->gpr[3] = 1; // Default true for actions/status queries
        }
        return true;
    }

    // 3. Audio / Sound DSP / AI / AX / nw4r::snd (0x80080000 - 0x800E0000, 0x80123000 - 0x80130000)
    if ((target >= 0x80080000u && target < 0x800E0000u) ||
        (target >= 0x80123000u && target < 0x80130000u)) {
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }

    // 4. Network / DWC / NHTTP / Socket (0x800CC000 - 0x800F0000, 0x801D0000 - 0x801F0000)
    if ((target >= 0x800CC000u && target < 0x800F0000u) ||
        (target >= 0x801D0000u && target < 0x801F0000u)) {
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }

    // 5. System / OS / SC / WPAD (0x801A0000 - 0x801C0000)
    if (target >= 0x801A0000u && target < 0x801C0000u) {
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }

    // 6. Relocated Course Objects / Effects / Particles (0x80680000 - 0x808B0000)
    //    This range contains REAL recompiled object/kart code that is simply not in the legacy
    //    `linked` set. Returning 0 here no-ops it. Every unique target is logged so one run yields
    //    the complete "needs linking" set; link them instead of widening this bypass.
    if (target >= 0x80680000u && target < 0x808B0000u) {
        static std::set<uint32_t> s_relBypassSeen;
        static uint32_t s_relBypassCalls = 0;
        if (s_relBypassSeen.size() < 512 && s_relBypassSeen.insert(target).second) {
            AppLog("[Batch HLE] Object/Effect bypassed target=%08X (pc=%08X lr=%08X unique=%zu calls=%u)\n",
                target, pc, lr, s_relBypassSeen.size(), s_relBypassCalls);
        }
        ++s_relBypassCalls;
        if (cpu) cpu->gpr[3] = 0;
        return true;
    }

    // 7. Page method caller fallback (pc = 0x805B0000 - 0x80680000)
    if (pc >= 0x805B0000u && pc <= 0x80680000u) {
        static uint32_t s_lastPageTarget = 0;
        if (s_lastPageTarget != target) {
            s_lastPageTarget = target;
            AppLog("[Batch HLE] Page caller bypassed target=%08X (pc=%08X lr=%08X)\n", target, pc, lr);
        }
        if (cpu) cpu->gpr[3] = 1;
        return true;
    }

    // 8. Universal safety fallback: log and return true so guest code NEVER crashes on missing dispatch
    static uint32_t s_lastAnyTarget = 0;
    if (s_lastAnyTarget != target) {
        s_lastAnyTarget = target;
        AppLog("[Batch HLE] Generic unlinked target=%08X (pc=%08X lr=%08X r3=%08X)\n",
            target, pc, lr, cpu ? cpu->gpr[3] : 0);
    }
    if (cpu) cpu->gpr[3] = 0;
    return true;
}

namespace MemoryInline {
void WriteFloat64Slow(uint32_t address, double)
{
    if (address < 0x10000) return;
    std::fprintf(stderr, "Unmapped float64 write: %08lX at guest PC %08lX\n",
        static_cast<unsigned long>(address), static_cast<unsigned long>(g_currentCpuContext->pc));
    std::abort();
}

void WriteResolvedFallbackFloat64(uint32_t address, double value)
{
    Memory::WriteFloat64(address, value);
}

template <typename T>
void WriteResolvedFallback(uint32_t address, T value)
{
    if constexpr (std::is_same_v<T, uint8_t>) Memory::Write8(address, value);
    else if constexpr (std::is_same_v<T, uint16_t>) Memory::Write16(address, value);
    else if constexpr (std::is_same_v<T, uint32_t>) Memory::Write32(address, value);
    else std::abort();
}
template void WriteResolvedFallback<uint8_t>(uint32_t, uint8_t);
template void WriteResolvedFallback<uint16_t>(uint32_t, uint16_t);
template void WriteResolvedFallback<uint32_t>(uint32_t, uint32_t);
}

namespace MemoryInline {
template <typename T>
T ReadResolvedFallback(uint32_t address)
{
    if constexpr (std::is_same_v<T, uint8_t>) return Memory::Read8(address);
    else if constexpr (std::is_same_v<T, uint32_t>) return Memory::Read32(address);
    else if constexpr (!std::is_same_v<T, uint16_t>) std::abort();
    return Memory::Read16(address);
}
template uint8_t ReadResolvedFallback<uint8_t>(uint32_t);
template uint16_t ReadResolvedFallback<uint16_t>(uint32_t);
template uint32_t ReadResolvedFallback<uint32_t>(uint32_t);
}

namespace MemoryInline {
float ReadResolvedFallbackFloat32(uint32_t address)
{
    return std::bit_cast<float>(Memory::Read32(address));
}

void WriteResolvedFallbackFloat32(uint32_t address, double value)
{
    Memory::WriteFloat32(address, value);
}

void WriteFloat32Slow(uint32_t address, double)
{
    if (address < 0x10000) return;
    // Unmapped 32-bit writes elsewhere in the memory model are ignored, so an abort here was
    // inconsistent: a guest store through a garbage pointer (title-page teardown feeds a
    // double's bit pattern into an AnimationGroup transform pointer) killed the whole run and
    // stopped the link loop.  Report the first few and carry on.
    static uint32_t s_reported = 0;
    static uint32_t s_seen[8] = {};
    for (uint32_t i = 0; i < s_reported; ++i) {
        if (s_seen[i] == address) return;
    }
    if (s_reported < 8) {
        s_seen[s_reported++] = address;
        const CpuContext* cpu = TryGetCpuContext();
        std::fprintf(stderr, "Unmapped float32 write: %08lX pc=%08X lr=%08X (ignored)\n",
            static_cast<unsigned long>(address), cpu ? cpu->pc : 0, cpu ? cpu->lr : 0);
    }
}
}

namespace MemoryInline {
double ReadResolvedFallbackFloat64(uint32_t address)
{
    return std::bit_cast<double>(Memory::Read64(address));
}
}
