#include "scene_config.h"
#include <glm/gtc/matrix_transform.hpp>
namespace mjgs {
glm::mat4 WorldFromGaussian(const SceneConfig& c) {
  glm::mat4 m = glm::translate(glm::mat4(1), c.translation);
  const glm::vec3 angles = glm::radians(c.rotation_degrees);
  m = glm::rotate(m, angles.z, {0, 0, 1});
  m = glm::rotate(m, angles.y, {0, 1, 0});
  m = glm::rotate(m, angles.x, {1, 0, 0});
  return glm::scale(m, glm::vec3(c.scale));
}
}
