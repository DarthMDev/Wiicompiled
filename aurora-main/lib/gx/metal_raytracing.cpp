#include "metal_raytracing.hpp"

#include "raytracing_scene.hpp"

#include <limits>
#include <vector>

namespace aurora::gx::metal_raytracing {
bool Builder::build(const raytracing::DecodedScene& scene) {
  if (scene.triangles.size() > std::numeric_limits<size_t>::max() / 9) {
    return false;
  }
  std::vector<float> vertices;
  vertices.reserve(scene.triangles.size() * 9);
  for (const raytracing::Triangle& triangle : scene.triangles) {
    vertices.insert(vertices.end(), {triangle.a.x, triangle.a.y, triangle.a.z,
                                     triangle.b.x, triangle.b.y, triangle.b.z,
                                     triangle.c.x, triangle.c.y, triangle.c.z});
  }
  return build_packed_triangles(vertices.data(), scene.triangles.size());
}
} // namespace aurora::gx::metal_raytracing
