#include "gx/raytracing_scene.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace aurora::gx::raytracing {
namespace {

TEST(RaytracingScene, SealsImmutableDirectTriangleData) {
  set_capture_enabled(true);
  begin_frame();

  std::array<uint8_t, 36> vertices{};
  vertices[0] = 0x11;
  const std::array<uint16_t, 3> indices{0, 1, 2};
  std::array<PnMtx, MaxPnMtx> matrices{};
  matrices[0].pos.m0.x() = 42.0f;
  Mat4x4<float> projection{};
  projection.m0.x() = 7.0f;
  const CaptureInput input{
      .primitive = GX_TRIANGLES,
      .format = GX_VTXFMT0,
      .vertexCount = 3,
      .vertexStride = 12,
      .position = {.attrType = GX_DIRECT, .cnt = 3, .compType = GX_F32},
      .vertices = vertices.data(),
      .vertexBytes = vertices.size(),
      .indices = indices.data(),
      .indexCount = indices.size(),
      .matrices = &matrices,
      .projection = &projection,
  };
  ASSERT_TRUE(record_opaque_draw(input));

  // The renderer begins recording the next frame as soon as the worker owns
  // the sealed one, so every byte and transform must be copied here.
  vertices[0] = 0xFF;
  matrices[0].pos.m0.x() = -1.0f;
  projection.m0.x() = -1.0f;
  auto snapshot = seal_frame();
  ASSERT_NE(snapshot, nullptr);
  ASSERT_EQ(snapshot->draws.size(), 1u);
  const auto& draw = snapshot->draws.front();
  EXPECT_EQ(draw.vertices[0], 0x11);
  EXPECT_EQ(draw.indices[0], indices[0]);
  EXPECT_EQ(draw.indices[1], indices[1]);
  EXPECT_EQ(draw.indices[2], indices[2]);
  EXPECT_FLOAT_EQ(draw.positionMatrices[0].m0.x(), 42.0f);
  EXPECT_FLOAT_EQ(draw.projection.m0.x(), 7.0f);
  EXPECT_EQ(snapshot->vertexBytes, vertices.size());
  EXPECT_EQ(snapshot->indexBytes, indices.size() * sizeof(uint16_t));

  set_capture_enabled(false);
}

TEST(RaytracingScene, RejectsMalformedOrDisabledInput) {
  CaptureInput input{};
  set_capture_enabled(false);
  EXPECT_FALSE(record_opaque_draw(input));
  set_capture_enabled(true);
  begin_frame();
  EXPECT_FALSE(record_opaque_draw(input));
  auto snapshot = seal_frame();
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(snapshot->rejectedDraws, 1u);
  set_capture_enabled(false);
}
} // namespace
} // namespace aurora::gx::raytracing
