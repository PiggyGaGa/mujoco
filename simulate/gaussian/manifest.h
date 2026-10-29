#pragma once
#include <array>
#include <string>
#include <mujoco/mujoco.h>
namespace mjgs {
struct Manifest {
  std::string visual, collision;
  int geom_group = 3;
  std::array<double, 3> position;
  std::array<double, 4> quaternion;
  std::array<double, 3> spawn_position;
  std::array<double, 4> spawn_quaternion;
};
Manifest LoadManifest(const std::string& path, const std::string& spawn = "");
// Empty base creates a collision-only scene. Caller owns the returned model.
mjModel* CompileManifest(const Manifest& manifest, const std::string& base = "");
}
