#include "raytracing_scene.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace aurora::gx::raytracing {
namespace {
constexpr size_t kMaxDraws = 4096;
constexpr size_t kMaxVertexBytes = 16 * 1024 * 1024;
constexpr size_t kMaxIndexBytes = 8 * 1024 * 1024;
constexpr size_t kMaxTriangles = 524'288;

std::atomic_bool g_captureEnabled{false};
std::unique_ptr<SceneSnapshot> g_recording;

bool valid(const CaptureInput& input) noexcept {
  return input.vertices != nullptr && input.vertexCount != 0 && input.vertexStride != 0 &&
         input.vertexBytes == static_cast<size_t>(input.vertexCount) * input.vertexStride && input.indices != nullptr &&
         input.indexCount >= 3 && input.indexCount % 3 == 0 && input.matrices != nullptr &&
         input.projection != nullptr && input.position.attrType == GX_DIRECT &&
         input.positionOffset < input.vertexStride;
}

size_t component_size(uint8_t type) noexcept {
  switch (static_cast<GXCompType>(type)) {
  case GX_U8:
  case GX_S8:
    return 1;
  case GX_U16:
  case GX_S16:
    return 2;
  case GX_F32:
    return 4;
  default:
    return 0;
  }
}

uint32_t read_u32(const uint8_t* source, bool bigEndian) noexcept {
  if (bigEndian) {
    return (static_cast<uint32_t>(source[0]) << 24) | (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) | source[3];
  }
  return (static_cast<uint32_t>(source[3]) << 24) | (static_cast<uint32_t>(source[2]) << 16) |
         (static_cast<uint32_t>(source[1]) << 8) | source[0];
}

float decode_component(const uint8_t* source, GXCompType type, uint8_t frac, bool bigEndian) noexcept {
  const float scale = std::ldexp(1.0f, -static_cast<int>(frac));
  switch (type) {
  case GX_U8:
    return static_cast<float>(source[0]) * scale;
  case GX_S8:
    return static_cast<float>(static_cast<int8_t>(source[0])) * scale;
  case GX_U16: {
    const uint16_t value = bigEndian ? static_cast<uint16_t>((source[0] << 8) | source[1])
                                     : static_cast<uint16_t>((source[1] << 8) | source[0]);
    return static_cast<float>(value) * scale;
  }
  case GX_S16: {
    const uint16_t bits = bigEndian ? static_cast<uint16_t>((source[0] << 8) | source[1])
                                    : static_cast<uint16_t>((source[1] << 8) | source[0]);
    return static_cast<float>(static_cast<int16_t>(bits)) * scale;
  }
  case GX_F32: {
    const uint32_t bits = read_u32(source, bigEndian);
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  default:
    return NAN;
  }
}

bool decode_position(const Draw& draw, uint16_t index, Vec3<float>& position) noexcept {
  const size_t componentBytes = component_size(draw.position.compType);
  if (index >= draw.vertexCount || componentBytes == 0 || (draw.position.cnt != 2 && draw.position.cnt != 3) ||
      draw.positionOffset + componentBytes * draw.position.cnt > draw.vertexStride) {
    return false;
  }
  const uint8_t* source = draw.vertices.data() + static_cast<size_t>(index) * draw.vertexStride + draw.positionOffset;
  const GXCompType type = static_cast<GXCompType>(draw.position.compType);
  position.x = decode_component(source, type, draw.position.frac, draw.bigEndian);
  position.y = decode_component(source + componentBytes, type, draw.position.frac, draw.bigEndian);
  position.z = draw.position.cnt == 3
                   ? decode_component(source + componentBytes * 2, type, draw.position.frac, draw.bigEndian)
                   : 0.0f;
  return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

bool transform_position(const Draw& draw, uint16_t index, Vec3<float>& result) noexcept {
  Vec3<float> position{};
  if (!decode_position(draw, index, position)) {
    return false;
  }
  uint32_t matrixIndex = draw.currentPnMtx;
  if (draw.perVertexPnMtx) {
    matrixIndex = draw.vertices[static_cast<size_t>(index) * draw.vertexStride] / 3u;
  }
  if (matrixIndex >= MaxPnMtx) {
    return false;
  }
  const Mat3x4<float>& matrix = draw.positionMatrices[matrixIndex];
  result = {
      position.x * matrix.m0.x() + position.y * matrix.m1.x() + position.z * matrix.m2.x() + matrix.m0.w(),
      position.x * matrix.m0.y() + position.y * matrix.m1.y() + position.z * matrix.m2.y() + matrix.m1.w(),
      position.x * matrix.m0.z() + position.y * matrix.m1.z() + position.z * matrix.m2.z() + matrix.m2.w(),
  };
  return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
}

bool degenerate(const Triangle& triangle) noexcept {
  const float abx = triangle.b.x - triangle.a.x;
  const float aby = triangle.b.y - triangle.a.y;
  const float abz = triangle.b.z - triangle.a.z;
  const float acx = triangle.c.x - triangle.a.x;
  const float acy = triangle.c.y - triangle.a.y;
  const float acz = triangle.c.z - triangle.a.z;
  const float crossX = aby * acz - abz * acy;
  const float crossY = abz * acx - abx * acz;
  const float crossZ = abx * acy - aby * acx;
  return crossX * crossX + crossY * crossY + crossZ * crossZ <= 1.0e-12f;
}
} // namespace

void set_capture_enabled(bool enabled) noexcept {
  g_captureEnabled.store(enabled, std::memory_order_release);
  if (!enabled) {
    g_recording.reset();
  }
}

bool capture_enabled() noexcept { return g_captureEnabled.load(std::memory_order_acquire); }

void begin_frame() noexcept {
  if (capture_enabled()) {
    g_recording = std::make_unique<SceneSnapshot>();
  } else {
    g_recording.reset();
  }
}

void discard_frame() noexcept { g_recording.reset(); }

bool record_opaque_draw(const CaptureInput& input) noexcept {
  if (!capture_enabled()) {
    return false;
  }
  if (!valid(input)) {
    if (g_recording) {
      ++g_recording->rejectedDraws;
    }
    return false;
  }
  if (!g_recording) {
    // The renderer calls begin_frame before draw decoding. Rejecting instead
    // of lazily allocating keeps malformed frame ordering from retaining data.
    return false;
  }
  const size_t indexBytes = input.indexCount * sizeof(uint16_t);
  if (g_recording->draws.size() >= kMaxDraws || input.vertexBytes > kMaxVertexBytes - g_recording->vertexBytes ||
      indexBytes > kMaxIndexBytes - g_recording->indexBytes) {
    ++g_recording->cappedDraws;
    return false;
  }

  Draw draw{};
  draw.primitive = input.primitive;
  draw.format = input.format;
  draw.vertexCount = input.vertexCount;
  draw.vertexStride = input.vertexStride;
  draw.positionOffset = input.positionOffset;
  draw.bigEndian = input.bigEndian;
  draw.currentPnMtx = std::min<uint32_t>(input.currentPnMtx, MaxPnMtx - 1);
  draw.position = input.position;
  draw.perVertexPnMtx = input.perVertexPnMtx;
  for (size_t i = 0; i < draw.positionMatrices.size(); ++i) {
    draw.positionMatrices[i] = (*input.matrices)[i].pos;
  }
  draw.projection = *input.projection;
  draw.vertices.assign(input.vertices, input.vertices + input.vertexBytes);
  draw.indices.assign(input.indices, input.indices + input.indexCount);
  g_recording->vertexBytes += input.vertexBytes;
  g_recording->indexBytes += indexBytes;
  g_recording->draws.emplace_back(std::move(draw));
  return true;
}

std::unique_ptr<SceneSnapshot> seal_frame() noexcept { return std::move(g_recording); }

std::unique_ptr<DecodedScene> decode_view_space_triangles(const SceneSnapshot& snapshot) noexcept {
  auto decoded = std::make_unique<DecodedScene>();
  for (const Draw& draw : snapshot.draws) {
    if (draw.vertices.size() != static_cast<size_t>(draw.vertexCount) * draw.vertexStride ||
        draw.indices.size() % 3 != 0) {
      ++decoded->rejectedDraws;
      continue;
    }
    for (size_t i = 0; i < draw.indices.size(); i += 3) {
      if (decoded->triangles.size() == kMaxTriangles) {
        decoded->cappedTriangles += static_cast<uint32_t>((draw.indices.size() - i) / 3);
        break;
      }
      Triangle triangle{};
      if (!transform_position(draw, draw.indices[i], triangle.a) ||
          !transform_position(draw, draw.indices[i + 1], triangle.b) ||
          !transform_position(draw, draw.indices[i + 2], triangle.c) || degenerate(triangle)) {
        ++decoded->rejectedTriangles;
        continue;
      }
      decoded->triangles.emplace_back(triangle);
    }
    if (decoded->triangles.size() == kMaxTriangles) {
      break;
    }
  }
  return decoded;
}
} // namespace aurora::gx::raytracing
