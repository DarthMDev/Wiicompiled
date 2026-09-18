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

TEST(RaytracingScene, DecodesBigEndianFixedPointPositionsAndPaletteTransforms) {
  SceneSnapshot snapshot{};
  Draw draw{};
  draw.primitive = GX_TRIANGLES;
  draw.vertexCount = 3;
  draw.vertexStride = 7;
  draw.positionOffset = 1;
  draw.position = {.attrType = GX_DIRECT, .cnt = 3, .compType = GX_U16, .frac = 1};
  draw.bigEndian = true;
  draw.perVertexPnMtx = true;
  draw.vertices = {
      0, 0, 2, 0, 0, 0, 0,
      0, 0, 0, 0, 2, 0, 0,
      0, 0, 0, 0, 0, 0, 2,
  };
  draw.indices = {0, 1, 2};
  auto& matrix = draw.positionMatrices[0];
  matrix.m0.x() = 1.0f;
  matrix.m1.y() = 1.0f;
  matrix.m2.z() = 1.0f;
  matrix.m0.w() = 10.0f;
  matrix.m1.w() = 20.0f;
  matrix.m2.w() = 30.0f;
  snapshot.draws.emplace_back(std::move(draw));

  const auto decoded = decode_view_space_triangles(snapshot);
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(decoded->triangles.size(), 1u);
  const Triangle& triangle = decoded->triangles.front();
  EXPECT_EQ(triangle.a, (Vec3<float>{11.0f, 20.0f, 30.0f}));
  EXPECT_EQ(triangle.b, (Vec3<float>{10.0f, 21.0f, 30.0f}));
  EXPECT_EQ(triangle.c, (Vec3<float>{10.0f, 20.0f, 31.0f}));
  EXPECT_EQ(decoded->rejectedTriangles, 0u);
}
} // namespace
} // namespace aurora::gx::raytracing
