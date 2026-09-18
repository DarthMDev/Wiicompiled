#pragma once

#include "gx.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace aurora::gx::raytracing {

// CPU-owned input for a future native Metal acceleration-structure build. The
// first capture path deliberately accepts only direct-position opaque triangle
// draws; indexed position arrays and alpha-tested geometry need their own
// immutable source-data rules before they can become RT occluders.
struct Draw {
  GXPrimitive primitive{};
  GXVtxFmt format{};
  uint16_t vertexCount = 0;
  uint32_t vertexStride = 0;
  uint32_t positionOffset = 0;
  uint32_t currentPnMtx = 0;
  AttrConfig position{};
  // FIFO command draws are big-endian; raw bridge draws are native-endian.
  bool bigEndian = false;
  bool perVertexPnMtx = false;
  std::array<Mat3x4<float>, MaxPnMtx> positionMatrices{};
  Mat4x4<float> projection{};
  std::vector<uint8_t> vertices;
  std::vector<uint16_t> indices;
};

// Triangles use GX's model-view coordinate system. Keeping the build inputs in
// view space avoids a second transform during a same-frame native RT build.
struct Triangle {
  Vec3<float> a{};
  Vec3<float> b{};
  Vec3<float> c{};
};

struct DecodedScene {
  std::vector<Triangle> triangles;
  uint32_t rejectedDraws = 0;
  uint32_t rejectedTriangles = 0;
  uint32_t cappedTriangles = 0;
};

struct SceneSnapshot {
  std::vector<Draw> draws;
  size_t vertexBytes = 0;
  size_t indexBytes = 0;
  uint32_t rejectedDraws = 0;
  uint32_t cappedDraws = 0;
};

struct CaptureInput {
  GXPrimitive primitive{};
  GXVtxFmt format{};
  uint16_t vertexCount = 0;
  uint32_t vertexStride = 0;
  uint32_t positionOffset = 0;
  AttrConfig position{};
  bool bigEndian = false;
  bool perVertexPnMtx = false;
  const uint8_t* vertices = nullptr;
  size_t vertexBytes = 0;
  const uint16_t* indices = nullptr;
  size_t indexCount = 0;
  const std::array<PnMtx, MaxPnMtx>* matrices = nullptr;
  uint32_t currentPnMtx = 0;
  const Mat4x4<float>* projection = nullptr;
};

// Disabled by default. A later graphics setting enables capture only for a
// supported native Metal RT renderer; tests may enable it directly.
void set_capture_enabled(bool enabled) noexcept;
bool capture_enabled() noexcept;
void begin_frame() noexcept;
void discard_frame() noexcept;
bool record_opaque_draw(const CaptureInput& input) noexcept;
std::unique_ptr<SceneSnapshot> seal_frame() noexcept;
std::unique_ptr<DecodedScene> decode_view_space_triangles(const SceneSnapshot& snapshot) noexcept;
} // namespace aurora::gx::raytracing
