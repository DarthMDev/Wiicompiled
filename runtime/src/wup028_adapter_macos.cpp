#include "wup028_adapter.h"

namespace Wup028Adapter {

void Initialize() {}
void Shutdown() {}

bool Read(std::array<PADStatus, 4>&) { return false; }
void SetPortAssignment(uint32_t, int) {}
int GetPortAssignment(uint32_t) { return -1; }
bool SetRumble(uint32_t, bool) { return false; }

AdapterInfo GetInfo()
{
    AdapterInfo info{};
    info.state = ConnectionState::DriverError;
    info.detail = "Official Wii U GameCube adapter support is not available on macOS yet.";
    return info;
}

} // namespace Wup028Adapter
