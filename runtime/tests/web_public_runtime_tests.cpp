#include "platform/web_bootstrap.h"

#include <cstdio>

int main()
{
    const uint32_t actual = MkwWebPublicRuntimeAbiVersion();
    if (actual != RuntimePlatform::Web::kPublicRuntimeAbiVersion) {
        std::fprintf(stderr, "web public runtime ABI mismatch: expected %u, got %u\\n",
                     RuntimePlatform::Web::kPublicRuntimeAbiVersion, actual);
        return 1;
    }

    std::puts("web public runtime ABI test passed");
    return 0;
}
