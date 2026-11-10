#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <mujoco/mujoco.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "viewer_bridge.h"

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
using Pixels = std::array<unsigned char, 256 * 256 * 3>;
std::array<int, 3> Centre(const Pixels& pixels) {
  int offset = (128 * 256 + 128) * 3;
  return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
}
}  // namespace

int main() {
  GLFWwindow* window = nullptr;
  mjModel* model = nullptr;
  mjData* data = nullptr;
  mjvScene scene;
  mjv_defaultScene(&scene);
  mjrContext context;
  mjr_defaultContext(&context);
  auto root = std::filesystem::temp_directory_path() /
      ("mjgs-alpha-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  auto ply = root.string() + ".ply";
  auto screenshot = root.string() + ".ppm";
  int result = 1;
  try {
    std::ofstream file(ply, std::ios::binary);
    file << "ply\nformat binary_little_endian 1.0\nelement vertex 1\n";
    for (auto name : {"x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2",
                      "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2"}) {
      file << "property float " << name << "\n";
    }
    file << "end_header\n";
    float row[14] = {0, 0, -3, 8, -.3f, -.3f, -.3f, 1, 0, 0, 0,
                     1.772454f, -1.772454f, -1.772454f};
    file.write(reinterpret_cast<char*>(row), sizeof(row));
    file.close();

    Require(glfwInit(), "glfw init failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(320, 320, "3DGS transparency test", nullptr, nullptr);
    Require(window != nullptr, "window creation failed");
    glfwMakeContextCurrent(window);
    std::printf("GL: %s\n", glGetString(GL_VERSION));
    const char* xml = R"(<mujoco><asset><mesh name="overlay" vertex="
      -.6 -.6 -.05  .6 -.6 -.05  .6 .6 -.05  -.6 .6 -.05
      -.6 -.6  .05  .6 -.6  .05  .6 .6  .05  -.6 .6  .05"/></asset><visual>
      <quality offsamples="0"/>
      <headlight ambient="1 1 1" diffuse="0 0 0"/></visual><worldbody>
      <camera pos="0 0 0" fovy="60"/>
      <geom type="mesh" mesh="overlay" pos="0 0 -2" rgba="0 1 0 .1"/>
      </worldbody></mujoco>)";
    auto* spec = mj_parseXMLString(xml, nullptr, nullptr, 0);
    Require(spec != nullptr, "XML parse failed");
    model = mj_compile(spec, nullptr);
    mj_deleteSpec(spec);
    Require(model != nullptr, "model compile failed");
    data = mj_makeData(model);
    mj_forward(model, data);
    mjv_makeScene(model, &scene, 100);
    mjr_makeContext(model, &context, mjFONTSCALE_100);
    mjr_setBuffer(mjFB_OFFSCREEN, &context);
    mjvCamera camera;
    mjv_defaultCamera(&camera);
    camera.type = mjCAMERA_FIXED;
    camera.fixedcamid = 0;
    mjvOption options;
    mjv_defaultOption(&options);
    mjrRect rect{17, 23, 256, 256};
    auto render = [&](float alpha, bool gaussian) {
      model->geom_rgba[3] = alpha;
      mjv_updateScene(model, data, &options, nullptr, &camera, mjCAT_ALL, &scene);
      if (gaussian) mjgs::RenderScene(scene, rect, &context);
      else mjr_render(rect, &scene, &context);
      Pixels pixels;
      mjr_readPixels(pixels.data(), nullptr, rect, &context);
      return pixels;
    };
    // No asset and hidden splats must preserve the upstream image exactly.
    mjgs::Shutdown();
    Require(render(.5f, false) == render(.5f, true), "no-asset image changed");
    char program[] = "test", flag[] = "--gs-ply";
    char* argv[] = {program, flag, ply.data(), nullptr};
    int argc = 3;
    Require(mjgs::Configure(argc, argv), "configure failed");
    auto low = Centre(render(.1f, true));
    Require(mjgs::Status() == "ready", "Gaussian renderer unavailable");
    auto half = Centre(render(.5f, true));
    auto opaque = Centre(render(1, true));
    std::printf("alpha .1 RGB=%d,%d,%d; .5=%d,%d,%d; 1=%d,%d,%d\n",
                low[0], low[1], low[2], half[0], half[1], half[2],
                opaque[0], opaque[1], opaque[2]);
    Require(low[0] > 60 && low[1] > 5, "low-alpha mesh or Gaussian background lost");
    Require(half[1] > low[1] + 30 && half[0] > 20, "transparent blending/order failed");
    Require(opaque[1] > 150 && opaque[0] < 20, "opaque depth occlusion regressed");
    mjgs::ToggleVisible();
    Require(render(.5f, false) == render(.5f, true), "hidden image changed");
    mjgs::ToggleVisible();

    // Segmentation must never invoke the external colour pass.
    scene.flags[mjRND_SEGMENT] = 1;
    int calls = 0;
    mjr_renderWithCallback(rect, &scene, &context,
      [](mjrRect, const mjvScene*, void* count) { ++*static_cast<int*>(count); }, &calls);
    Require(calls == 0, "callback contaminated segmentation");
    scene.flags[mjRND_SEGMENT] = 0;

    // Capture belongs after the complete frame, including transparent geoms.
    mjgs::Shutdown();
    char capture[] = "--gs-screenshot", frame[] = "--gs-capture-frame", one[] = "1";
    char* capture_argv[] = {program, flag, ply.data(), capture, screenshot.data(), frame, one, nullptr};
    argc = 7;
    Require(mjgs::Configure(argc, capture_argv), "capture configure failed");
    auto final = render(.5f, true);
    std::ifstream image(screenshot, std::ios::binary);
    std::string magic;
    int width, height, maximum;
    image >> magic >> width >> height >> maximum;
    image.get();
    Require(magic == "P6" && width == 256 && height == 256 && maximum == 255,
            "capture header invalid");
    Pixels captured;
    image.read(reinterpret_cast<char*>(captured.data()), captured.size());
    Require(image.gcount() == captured.size(), "capture incomplete");
    for (int y = 0; y < 256; ++y) {
      for (int x = 0; x < 256 * 3; ++x) {
        Require(captured[y * 256 * 3 + x] == final[(255 - y) * 256 * 3 + x],
                "capture omitted transparent stage");
      }
    }
    Require(mjr_getError() == 0, "GL state/error regression");
    std::puts("PASS: transparent order, opaque depth, hidden/no-asset parity, segmentation, capture");
    result = 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
  }
  mjgs::Shutdown();
  mjr_freeContext(&context);
  mjv_freeScene(&scene);
  if (data) mj_deleteData(data);
  if (model) mj_deleteModel(model);
  if (window) glfwDestroyWindow(window);
  glfwTerminate();
  std::filesystem::remove(ply);
  std::filesystem::remove(screenshot);
  return result;
}
