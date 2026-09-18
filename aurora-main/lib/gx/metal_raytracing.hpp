#pragma once

#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <memory>
#include <string>

namespace aurora::gx::raytracing {
struct DecodedScene;
}

namespace aurora::gx::metal_raytracing {

struct BuildStats {
  size_t triangleCount = 0;
  size_t vertexBytes = 0;
  size_t accelerationStructureBytes = 0;
};

// Owns native Metal resources only. It does not affect raster rendering until
// a later RT effect explicitly asks it to build a sealed scene.
class Builder {
public:
  virtual ~Builder() = default;
  bool build(const raytracing::DecodedScene& scene);
  virtual const BuildStats& stats() const noexcept = 0;
  virtual const std::string& error() const noexcept = 0;

private:
  virtual bool build_packed_triangles(const float* vertices, size_t triangleCount) = 0;
};

bool supported(const wgpu::Device& device, wgpu::BackendType backend) noexcept;
std::unique_ptr<Builder> create(const wgpu::Device& device, wgpu::BackendType backend,
                                std::string& error);
} // namespace aurora::gx::metal_raytracing
