#include "gx/metal_raytracing.hpp"
#include "gx/raytracing_scene.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace aurora::gx::metal_raytracing {
namespace {
class RecordingBuilder final : public Builder {
  std::vector<float> m_vertices;
  BuildStats m_stats{};
  std::string m_error;

  bool build_packed_triangles(const float* vertices, size_t triangleCount) override {
    m_vertices.assign(vertices, vertices + triangleCount * 9);
    m_stats = {.triangleCount = triangleCount, .vertexBytes = triangleCount * 9 * sizeof(float)};
    return true;
  }

public:
  const BuildStats& stats() const noexcept override { return m_stats; }
  const std::string& error() const noexcept override { return m_error; }
  const std::vector<float>& vertices() const noexcept { return m_vertices; }
};

TEST(MetalRaytracing, PacksDecodedTrianglesForNativeBuilder) {
  raytracing::DecodedScene scene{};
  scene.triangles.push_back({.a = {1.0f, 2.0f, 3.0f},
                             .b = {4.0f, 5.0f, 6.0f},
                             .c = {7.0f, 8.0f, 9.0f}});
  RecordingBuilder builder;

  ASSERT_TRUE(builder.build(scene));
  EXPECT_EQ(builder.stats().triangleCount, 1u);
  EXPECT_EQ(builder.stats().vertexBytes, 9 * sizeof(float));
  EXPECT_EQ(builder.vertices(), (std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
                                                     6.0f, 7.0f, 8.0f, 9.0f}));
}
} // namespace
} // namespace aurora::gx::metal_raytracing
