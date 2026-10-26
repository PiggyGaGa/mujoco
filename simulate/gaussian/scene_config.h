#pragma once
#include <string>
#include <glm/glm.hpp>
namespace mjgs {
struct SceneConfig {
  std::string ply;
  float scale = 1;
  glm::vec3 translation{0};
  glm::vec3 rotation_degrees{0}; // intrinsic XYZ: Rz * Ry * Rx
  bool full_sh = true;
  bool exit_after_capture = false;
  int capture_frame = 0;
  std::string screenshot;
};
glm::mat4 WorldFromGaussian(const SceneConfig& config);
void ValidatePly(const std::string& path);
}
