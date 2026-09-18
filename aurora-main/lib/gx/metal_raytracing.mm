#include "metal_raytracing.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <dawn/native/MetalBackend.h>

#include <cstring>
#include <limits>

namespace aurora::gx::metal_raytracing {
namespace {
struct PackedVertex {
  float x;
  float y;
  float z;
};
static_assert(sizeof(PackedVertex) == 12);

class NativeBuilder final : public Builder {
  id<MTLDevice> m_device;
  id<MTLCommandQueue> m_queue;
  id<MTLBuffer> m_vertices;
  id<MTLBuffer> m_scratch;
  id<MTLAccelerationStructure> m_structure;
  BuildStats m_stats;
  std::string m_error;

  bool fail(const char* message) {
    m_error = message;
    return false;
  }

public:
  NativeBuilder(id<MTLDevice> device, id<MTLCommandQueue> queue) : m_device(device), m_queue(queue) {}

private:
  bool build_packed_triangles(const float* vertices, size_t triangleCount) override {
    @autoreleasepool {
      m_error.clear();
      m_stats = {};
      m_vertices = nil;
      m_scratch = nil;
      m_structure = nil;
      if (triangleCount == 0) {
        return true;
      }
      if (triangleCount > std::numeric_limits<size_t>::max() / (3 * sizeof(PackedVertex))) {
        return fail("Ray-tracing vertex buffer size overflow");
      }

      const size_t vertexBytes = triangleCount * 3 * sizeof(PackedVertex);
      m_vertices = [m_device newBufferWithLength:vertexBytes options:MTLResourceStorageModeShared];
      if (!m_vertices) {
        return fail("Could not allocate ray-tracing vertex buffer");
      }
      std::memcpy(m_vertices.contents, vertices, vertexBytes);

      auto* geometry = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
      geometry.vertexBuffer = m_vertices;
      geometry.vertexFormat = MTLAttributeFormatFloat3;
      geometry.vertexStride = sizeof(PackedVertex);
      geometry.triangleCount = triangleCount;
      geometry.opaque = YES;
      auto* descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
      descriptor.geometryDescriptors = @[geometry];
      const MTLAccelerationStructureSizes sizes = [m_device accelerationStructureSizesWithDescriptor:descriptor];
      if (sizes.accelerationStructureSize == 0 || sizes.buildScratchBufferSize == 0) {
        return fail("Metal returned invalid acceleration-structure sizes");
      }
      m_structure = [m_device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
      m_scratch = [m_device newBufferWithLength:sizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
      if (!m_structure || !m_scratch) {
        return fail("Could not allocate ray-tracing acceleration-structure resources");
      }

      id<MTLCommandBuffer> commands = [m_queue commandBuffer];
      id<MTLAccelerationStructureCommandEncoder> encoder = [commands accelerationStructureCommandEncoder];
      if (!commands || !encoder) {
        return fail("Could not create the ray-tracing build command encoder");
      }
      [encoder buildAccelerationStructure:m_structure descriptor:descriptor scratchBuffer:m_scratch scratchBufferOffset:0];
      [encoder endEncoding];
      [commands commit];
      // The first integration is intentionally synchronous. A later effect will
      // replace this boundary with shared-event handoff to Dawn's frame queue.
      [commands waitUntilCompleted];
      if (commands.status != MTLCommandBufferStatusCompleted) {
        return fail("Metal acceleration-structure build failed");
      }
      m_stats = {
          .triangleCount = triangleCount,
          .vertexBytes = vertexBytes,
          .accelerationStructureBytes = sizes.accelerationStructureSize,
      };
      return true;
    }
  }

public:
  const BuildStats& stats() const noexcept override { return m_stats; }
  const std::string& error() const noexcept override { return m_error; }
};

id<MTLDevice> native_device(const wgpu::Device& device) noexcept {
  return device ? dawn::native::metal::GetMTLDevice(device.Get()) : nil;
}
} // namespace

bool supported(const wgpu::Device& device, wgpu::BackendType backend) noexcept {
  if (backend != wgpu::BackendType::Metal) {
    return false;
  }
  @autoreleasepool {
    id<MTLDevice> native = native_device(device);
    return native != nil && native.supportsRaytracing;
  }
}

std::unique_ptr<Builder> create(const wgpu::Device& device, wgpu::BackendType backend,
                                std::string& error) {
  error.clear();
  @autoreleasepool {
    if (!supported(device, backend)) {
      error = "Metal hardware ray tracing is unsupported";
      return {};
    }
    id<MTLDevice> native = native_device(device);
    id<MTLCommandQueue> queue = [native newCommandQueue];
    if (!queue) {
      error = "Could not create the ray-tracing Metal command queue";
      return {};
    }
    return std::make_unique<NativeBuilder>(native, queue);
  }
}
} // namespace aurora::gx::metal_raytracing
