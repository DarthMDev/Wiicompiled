#include "raytracing_scene.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace aurora::gx::raytracing {
namespace {
constexpr size_t kMaxDraws = 4096;
constexpr size_t kMaxVertexBytes = 16 * 1024 * 1024;
constexpr size_t kMaxIndexBytes = 8 * 1024 * 1024;

std::atomic_bool g_captureEnabled{false};
std::unique_ptr<SceneSnapshot> g_recording;

bool valid(const CaptureInput& input) noexcept {
  return input.vertices != nullptr && input.vertexCount != 0 && input.vertexStride != 0 &&
         input.vertexBytes == static_cast<size_t>(input.vertexCount) * input.vertexStride && input.indices != nullptr &&
         input.indexCount >= 3 && input.indexCount % 3 == 0 && input.matrices != nullptr &&
         input.projection != nullptr && input.position.attrType == GX_DIRECT &&
         input.positionOffset < input.vertexStride;
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
} // namespace aurora::gx::raytracing
