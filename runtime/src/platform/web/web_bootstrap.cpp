#include "platform/web_bootstrap.h"

extern "C" uint32_t MkwWebPublicRuntimeAbiVersion() noexcept
{
    return RuntimePlatform::Web::kPublicRuntimeAbiVersion;
}
