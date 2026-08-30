// Feasibility prototype only. It validates fixed reservation, aliased backing,
// and recoverable access faults. It deliberately uses mprotect in a SIGSEGV
// handler solely to demonstrate the mechanism: that is not a signal-safe
// production fault-dispatch design.
#include "guest_flat_memory.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr mach_vm_address_t kGuestBase = GuestFlat::kFixedFlatGuestBase;
constexpr mach_vm_size_t kGuestSpaceSize = 0x1'0000'0000ull;
constexpr size_t kGuestPageSize = 4096;

uint8_t* g_guestPage = nullptr;
size_t g_hostPageSize = 0;
volatile sig_atomic_t g_faultCount = 0;

void FaultHandler(int signal, siginfo_t* info, void*) {
    const auto address = reinterpret_cast<uintptr_t>(info->si_addr);
    const auto first = reinterpret_cast<uintptr_t>(g_guestPage);
    if ((signal == SIGSEGV || signal == SIGBUS) && g_guestPage != nullptr &&
        address >= first && address < first + g_hostPageSize) {
        ++g_faultCount;
        // Prototype-only recovery. The full runtime must use a signal-safe
        // design and cannot perform its current mutex/allocation/logging work
        // from this handler.
        (void)mprotect(g_guestPage, g_hostPageSize, PROT_READ | PROT_WRITE);
        return;
    }
    std::_Exit(128 + signal);
}

class ScopedSignalHandler {
public:
    bool Install() {
        struct sigaction action {};
        action.sa_sigaction = FaultHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        if (sigaction(SIGSEGV, &action, &previousSegv_) != 0) {
            return false;
        }
        if (sigaction(SIGBUS, &action, &previousBus_) != 0) {
            (void)sigaction(SIGSEGV, &previousSegv_, nullptr);
            return false;
        }
        installed_ = true;
        return true;
    }

    ~ScopedSignalHandler() {
        if (installed_) {
            (void)sigaction(SIGSEGV, &previousSegv_, nullptr);
            (void)sigaction(SIGBUS, &previousBus_, nullptr);
        }
    }

private:
    struct sigaction previousSegv_ {};
    struct sigaction previousBus_ {};
    bool installed_ = false;
};

} // namespace

int main() {
    g_hostPageSize = static_cast<size_t>(getpagesize());
    if (g_hostPageSize < kGuestPageSize || g_hostPageSize % kGuestPageSize != 0) {
        std::cerr << "unexpected host page size: " << g_hostPageSize << '\n';
        return 1;
    }

    mach_vm_address_t reservation = kGuestBase;
    const kern_return_t reserveResult = mach_vm_allocate(mach_task_self(), &reservation, kGuestSpaceSize, VM_FLAGS_FIXED);
    if (reserveResult != KERN_SUCCESS || reservation != kGuestBase) {
        std::cerr << "cannot reserve 4 GiB guest space at 0x" << std::hex << kGuestBase
                  << ": " << mach_error_string(reserveResult) << '\n';
        return 1;
    }

    const auto cleanupReservation = [&] {
        (void)mach_vm_deallocate(mach_task_self(), kGuestBase, kGuestSpaceSize);
    };
    if (mach_vm_deallocate(mach_task_self(), kGuestBase, g_hostPageSize) != KERN_SUCCESS) {
        std::cerr << "cannot carve a host page from the fixed reservation\n";
        cleanupReservation();
        return 1;
    }

    // An unlinked file supplies the same MAP_SHARED aliasing behavior as a
    // Mach memory entry while working in sandboxed developer environments
    // where the POSIX shared-memory namespace may be unavailable.
    char backingPath[] = "/tmp/wiicompiled-macos-memory-XXXXXX";
    const int fd = mkstemp(backingPath);
    if (fd < 0 || unlink(backingPath) != 0 || ftruncate(fd, static_cast<off_t>(g_hostPageSize)) != 0) {
        std::perror("mkstemp/unlink/ftruncate");
        if (fd >= 0) close(fd);
        cleanupReservation();
        return 1;
    }
    const auto cleanupShared = [&] {
        close(fd);
    };

    auto* host = static_cast<uint8_t*>(mmap(nullptr, g_hostPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    g_guestPage = static_cast<uint8_t*>(mmap(reinterpret_cast<void*>(kGuestBase), g_hostPageSize,
                                              PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0));
    if (host == MAP_FAILED || g_guestPage == MAP_FAILED || g_guestPage != reinterpret_cast<uint8_t*>(kGuestBase)) {
        std::cerr << "cannot create fixed aliased guest mapping\n";
        if (host != MAP_FAILED) munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }

    host[0] = 0x5a;
    if (g_guestPage[0] != 0x5a) {
        std::cerr << "host-to-guest alias write was not visible\n";
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }
    g_guestPage[1] = 0xa5;
    if (host[1] != 0xa5) {
        std::cerr << "guest-to-host alias write was not visible\n";
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }

    ScopedSignalHandler handler;
    if (!handler.Install() || mprotect(g_guestPage, g_hostPageSize, PROT_NONE) != 0) {
        std::perror("sigaction/mprotect");
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }
    volatile uint8_t deferredRead = g_guestPage[0];
    (void)deferredRead;
    if (g_faultCount != 1) {
        std::cerr << "deferred read did not produce one recoverable fault\n";
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }

    if (mprotect(g_guestPage, g_hostPageSize, PROT_READ) != 0) {
        std::perror("mprotect readonly");
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }
    g_guestPage[2] = 0x33;
    if (g_faultCount != 2 || host[2] != 0x33) {
        std::cerr << "executable-write-style fault did not recover through the alias\n";
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }

    errno = 0;
    const int guestGranularProtection = mprotect(g_guestPage + kGuestPageSize, kGuestPageSize, PROT_NONE);
    if (g_hostPageSize > kGuestPageSize && (guestGranularProtection == 0 || errno != EINVAL)) {
        std::cerr << "host unexpectedly accepted a 4 KiB subpage protection request\n";
        munmap(host, g_hostPageSize);
        cleanupShared();
        cleanupReservation();
        return 1;
    }

    std::cout << "host page size: " << g_hostPageSize
              << "; 4 KiB guest traps cannot be independently protected within one host page\n";
    munmap(host, g_hostPageSize);
    cleanupShared();
    cleanupReservation();
    return 0;
}
