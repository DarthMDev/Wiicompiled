#include "metal_raytracing.hpp"

namespace aurora::gx::metal_raytracing {
bool supported(const wgpu::Device&, wgpu::BackendType) noexcept { return false; }

std::unique_ptr<Builder> create(const wgpu::Device&, wgpu::BackendType, std::string& error) {
  error = "Metal hardware ray tracing is not available in this build";
  return {};
}
} // namespace aurora::gx::metal_raytracing
