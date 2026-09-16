// Native Metal hardware-ray-tracing feasibility probe.
//
// This uses the MTLDevice owned by Dawn, so success proves the renderer can use
// ray tracing on the same GPU as its WebGPU frame. It builds a one-triangle
// acceleration structure, traces a compute ray, then verifies the hit.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <simd/simd.h>

#include <dawn/native/MetalBackend.h>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
constexpr uint64_t kTimeoutNs = 10'000'000'000;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

void wait(const wgpu::Instance &instance, wgpu::Future future) {
  require(instance.WaitAny(future, kTimeoutNs) == wgpu::WaitStatus::Success, "Dawn request timed out");
}

id<MTLComputePipelineState> make_pipeline(id<MTLDevice> device) {
  static NSString *const source = @R"(
    #include <metal_stdlib>
    using namespace metal;
    using namespace metal::raytracing;

    kernel void trace_one_triangle(device uint* result [[buffer(0)]],
                                   acceleration_structure<> scene [[buffer(1)]]) {
      intersector<triangle_data> rays;
      rays.assume_geometry_type(geometry_type::triangle);
      ray query;
      query.origin = float3(-0.25, -0.25, 1.0);
      query.direction = float3(0.0, 0.0, -1.0);
      query.min_distance = 0.0;
      query.max_distance = 2.0;
      const auto hit = rays.intersect(query, scene);
      result[0] = hit.type == intersection_type::triangle ? 1u : 0u;
    }
  )";
  NSError *error = nil;
  id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
  if (!library) {
    throw std::runtime_error(std::string("Metal shader compilation failed: ") +
                             (error.localizedDescription.UTF8String ?: "unknown error"));
  }
  id<MTLFunction> function = [library newFunctionWithName:@"trace_one_triangle"];
  require(function != nil, "Ray-tracing Metal kernel is missing");
  id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
  if (!pipeline) {
    throw std::runtime_error(std::string("Metal compute pipeline creation failed: ") +
                             (error.localizedDescription.UTF8String ?: "unknown error"));
  }
  return pipeline;
}

void run_probe(id<MTLDevice> device) {
  const simd_float3 vertices[] = {
      {-1.0f, -1.0f, 0.0f},
      {1.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f},
  };
  id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:vertices
                                                   length:sizeof(vertices)
                                                  options:MTLResourceStorageModeShared];
  require(vertexBuffer != nil, "Could not allocate ray-tracing vertex buffer");

  auto *triangle = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
  triangle.vertexBuffer = vertexBuffer;
  triangle.vertexFormat = MTLAttributeFormatFloat3;
  triangle.vertexStride = sizeof(simd_float3);
  triangle.triangleCount = 1;
  triangle.opaque = YES;
  auto *descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
  descriptor.geometryDescriptors = @[ triangle ];
  const auto sizes = [device accelerationStructureSizesWithDescriptor:descriptor];
  require(sizes.accelerationStructureSize != 0 && sizes.buildScratchBufferSize != 0,
          "Metal returned invalid acceleration-structure sizes");
  id<MTLAccelerationStructure> structure = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
  id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize
                                              options:MTLResourceStorageModePrivate];
  require(structure != nil && scratch != nil, "Could not allocate acceleration-structure resources");

  id<MTLCommandQueue> queue = [device newCommandQueue];
  id<MTLCommandBuffer> build = [queue commandBuffer];
  id<MTLAccelerationStructureCommandEncoder> encoder = [build accelerationStructureCommandEncoder];
  require(encoder != nil, "Could not create acceleration-structure command encoder");
  [encoder buildAccelerationStructure:structure descriptor:descriptor scratchBuffer:scratch scratchBufferOffset:0];
  [encoder endEncoding];
  [build commit];
  [build waitUntilCompleted];
  require(build.status == MTLCommandBufferStatusCompleted, "Acceleration-structure build failed");

  id<MTLComputePipelineState> pipeline = make_pipeline(device);
  uint32_t result = 0;
  id<MTLBuffer> output = [device newBufferWithBytes:&result length:sizeof(result) options:MTLResourceStorageModeShared];
  require(output != nil, "Could not allocate ray-tracing result buffer");
  id<MTLCommandBuffer> trace = [queue commandBuffer];
  id<MTLComputeCommandEncoder> compute = [trace computeCommandEncoder];
  [compute setComputePipelineState:pipeline];
  [compute setBuffer:output offset:0 atIndex:0];
  [compute setAccelerationStructure:structure atBufferIndex:1];
  [compute dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
  [compute endEncoding];
  [trace commit];
  [trace waitUntilCompleted];
  require(trace.status == MTLCommandBufferStatusCompleted, "Ray-tracing dispatch failed");
  require(*static_cast<const uint32_t *>(output.contents) == 1u, "Ray did not intersect the known triangle");
  std::cout << "PASS: " << device.name.UTF8String
            << " built and traced a hardware-ray-tracing triangle using Dawn's Metal device\n";
}

int run() {
  const wgpu::InstanceFeatureName timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor instanceDescriptor{};
  instanceDescriptor.requiredFeatureCount = 1;
  instanceDescriptor.requiredFeatures = &timedWait;
  auto instance = wgpu::CreateInstance(&instanceDescriptor);
  require(instance != nullptr, "Could not create Dawn instance");
  wgpu::Adapter adapter;
  wgpu::RequestAdapterOptions options{};
  options.backendType = wgpu::BackendType::Metal;
  wait(instance, instance.RequestAdapter(
                     &options, wgpu::CallbackMode::WaitAnyOnly,
                     [&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
                       if (status == wgpu::RequestAdapterStatus::Success)
                         adapter = std::move(result);
                       else
                         std::cerr << "Dawn adapter request failed: " << std::string_view(message) << '\n';
                     }));
  if (!adapter) {
    std::cout << "SKIP: no Dawn Metal adapter\n";
    return 77;
  }
  wgpu::Device device;
  wait(instance, adapter.RequestDevice(
                     nullptr, wgpu::CallbackMode::WaitAnyOnly,
                     [&device](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
                       if (status == wgpu::RequestDeviceStatus::Success)
                         device = std::move(result);
                       else
                         std::cerr << "Dawn device request failed: " << std::string_view(message) << '\n';
                     }));
  require(device != nullptr, "Could not create Dawn Metal device");
  id<MTLDevice> native = dawn::native::metal::GetMTLDevice(device.Get());
  require(native != nil, "Dawn did not expose its MTLDevice");
  if (!native.supportsRaytracing || ![native supportsFamily:MTLGPUFamilyApple9]) {
    std::cout << "SKIP: " << native.name.UTF8String << " has no Apple-family-9 hardware ray tracing\n";
    return 77;
  }
  run_probe(native);
  return 0;
}
} // namespace

int main() {
  @autoreleasepool {
    try {
      return run();
    } catch (const std::exception &error) {
      std::cerr << "FAIL: " << error.what() << '\n';
      return 1;
    }
  }
}
