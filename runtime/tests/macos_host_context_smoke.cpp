#include "host_context.h"

#include <cstdlib>

namespace {
HostContext::Handle g_scheduler = nullptr;
HostContext::Handle g_worker = nullptr;
int g_steps = 0;

void Worker(void*)
{
    if (!HostContext::IsCurrent(g_worker)) {
        std::abort();
    }
    ++g_steps;
    HostContext::Switch(g_scheduler);
    if (!HostContext::IsCurrent(g_worker)) {
        std::abort();
    }
    ++g_steps;
    HostContext::Switch(g_scheduler);
}
} // namespace

int main()
{
    if (!HostContext::InitializeScheduler(&g_scheduler) ||
        !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }
    g_worker = HostContext::Create(64 * 1024, Worker, nullptr);
    if (!g_worker) {
        return 1;
    }

    HostContext::Switch(g_worker);
    if (g_steps != 1 || !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }
    HostContext::Switch(g_worker);
    if (g_steps != 2 || !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }

    HostContext::Destroy(g_worker);
    HostContext::ShutdownScheduler(g_scheduler);
}
