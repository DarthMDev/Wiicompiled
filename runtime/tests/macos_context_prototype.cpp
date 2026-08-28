// Feasibility prototype only. ucontext is deprecated on macOS and is not a
// production scheduler choice; this test establishes the cooperative switch
// semantics a replacement must preserve.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include <ucontext.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

ucontext_t g_scheduler{};
ucontext_t g_worker{};
std::array<char, 64 * 1024> g_workerStack{};
std::vector<int> g_events;

void Worker() {
    g_events.push_back(1);
    if (swapcontext(&g_worker, &g_scheduler) != 0) {
        std::abort();
    }
    g_events.push_back(2);
}

bool Matches(std::initializer_list<int> expected) {
    return g_events == std::vector<int>(expected);
}

} // namespace

int main() {
    if (getcontext(&g_worker) != 0) {
        std::perror("getcontext");
        return 1;
    }
    g_worker.uc_stack.ss_sp = g_workerStack.data();
    g_worker.uc_stack.ss_size = g_workerStack.size();
    g_worker.uc_link = &g_scheduler;
    makecontext(&g_worker, Worker, 0);

    if (swapcontext(&g_scheduler, &g_worker) != 0 || !Matches({1})) {
        std::cerr << "worker did not yield back to the scheduler\n";
        return 1;
    }
    if (swapcontext(&g_scheduler, &g_worker) != 0 || !Matches({1, 2})) {
        std::cerr << "worker did not resume and return to the scheduler\n";
        return 1;
    }
    return 0;
}
