#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include "splatrenderer.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

class CheckedRenderer : public SplatRenderer {
 public:
  double Benchmark(bool batched, const glm::mat4& projection) {
    preSortProg = std::make_shared<Program>();
    if (batched) preSortProg->AddMacro("DEFINES", "#define MJGS_SUBGROUP\n");
    if (!preSortProg->LoadCompute("shader/presort_compute.glsl"))
      throw std::runtime_error("benchmark shader compilation failed");
    GLuint query;
    glGenQueries(1, &query);
    std::vector<double> samples;
    for (int i = 0; i < 15; ++i) {
      glBeginQuery(GL_TIME_ELAPSED, query);
      Sort(glm::mat4(1), projection, glm::vec4(0, 0, 256, 256), glm::vec2(.1f, 100));
      glEndQuery(GL_TIME_ELAPSED);
      GLuint64 elapsed;
      glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
      if (i >= 3) samples.push_back(elapsed / 1e6);
    }
    glDeleteQueries(1, &query);
    std::sort(samples.begin(), samples.end());
    return (samples[5] + samples[6]) * .5;
  }
  void SetBlocks(uint32_t blocks) {
    numBlocksPerWorkgroup = blocks;
    const size_t groups = (posVec.size() + 256 * blocks - 1) / (256 * blocks);
    std::vector<uint32_t> histogram(groups * 256);
    histogramBuffer = std::make_shared<BufferObject>(GL_SHADER_STORAGE_BUFFER, histogram, GL_DYNAMIC_STORAGE_BIT);
  }
  void Check(size_t expected) {
    // Readback is intentional here: production rendering never needs it.
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    atomicCounterBuffer->Read(atomicCounterVec);
    if (atomicCounterVec[0] != expected || atomicCounterVec[1] != 1)
      throw std::runtime_error("Indirect draw command mismatch");
    std::vector<uint32_t> keys(posVec.size()), indices(posVec.size());
    glGetNamedBufferSubData(keyBuffer->GetObj(), 0, keys.size() * sizeof(uint32_t), keys.data());
    glGetNamedBufferSubData(valBuffer->GetObj(), 0, indices.size() * sizeof(uint32_t), indices.data());
    std::vector<bool> seen(posVec.size());
    for (size_t i = 0; i < expected; ++i) {
      if (indices[i] >= seen.size() || seen[indices[i]])
        throw std::runtime_error("Invalid or duplicate sorted index");
      seen[indices[i]] = true;
      if (i && keys[i - 1] > keys[i])
        throw std::runtime_error("Radix keys out of order across workgroups");
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GL error");
  }
};

int main(int argc, char** argv) {
  const bool benchmark = argc == 2 && std::string(argv[1]) == "--benchmark";
  GLFWwindow* window = nullptr;
  auto path = std::filesystem::temp_directory_path() /
      ("mjgs-sort-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ply");
  int result = 1;
  try {
    if (!glfwInit()) throw std::runtime_error("glfw init failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(256, 256, "sort test", nullptr, nullptr);
    if (!window) throw std::runtime_error("window creation failed");
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("glew init failed");
    while (glGetError() != GL_NO_ERROR) {}
    if (!GLEW_KHR_shader_subgroup) throw std::runtime_error("subgroup support required");
    constexpr size_t count = 799083;  // Room-sized workload; partial final block.
    std::ofstream file(path, std::ios::binary);
    file << "ply\nformat binary_little_endian 1.0\nelement vertex " << count << "\n";
    for (auto name : {"x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2"})
      file << "property float " << name << "\n";
    file << "end_header\n";
    for (size_t i = 0; i < count; ++i) {
      float row[14] = {0, 0, -1.0f - static_cast<float>((i * 7919) % count) * .0001f,
                      0, -5, -5, -5, 1, 0, 0, 0, 0, 0, 0};
      file.write(reinterpret_cast<char*>(row), sizeof(row));
    }
    file.close();
    auto cloud = std::make_shared<GaussianCloud>(GaussianCloud::Options{false, false});
    if (!cloud->ImportPly(path.string())) throw std::runtime_error("PLY import failed");
    {
      CheckedRenderer renderer;
      if (!renderer.Init(cloud, false, false)) throw std::runtime_error("renderer init failed");
      auto projection = glm::perspective(glm::radians(60.0f), 1.0f, .1f, 100.0f);
      if (benchmark && GLEW_ARB_shader_atomic_counter_ops) {
        auto baseline = renderer.Benchmark(false, projection);
        renderer.Check(count);
        auto batched = renderer.Benchmark(true, projection);
        renderer.Check(count);
        std::printf("GPU sort median (799083 points): per-point atomic %.3f ms, subgroup atomic %.3f ms; %.2fx\n",
                    baseline, batched, baseline / batched);
      }
      if (benchmark) {
        for (uint32_t blocks : {8u, 16u, 32u, 64u, 128u}) {
          renderer.SetBlocks(blocks);
          auto milliseconds = renderer.Benchmark(false, projection);
          renderer.Check(count);
          std::printf("GPU sort median: blocks/group=%u %.3f ms\n", blocks, milliseconds);
        }
        renderer.SetBlocks(32);
      }
      for (int cycle = 0; cycle < 3; ++cycle) {
        renderer.Sort(glm::mat4(1), projection, glm::vec4(0, 0, 256, 256), glm::vec2(.1f, 100));
        renderer.Check(count);
        // A compacted set smaller than capacity must still populate and scan
        // zero histograms for the unused groups, without running scatter loops.
        renderer.Sort(glm::mat4(1), projection, glm::vec4(0, 0, 256, 256), glm::vec2(.1f, 40));
        renderer.Check(390001);
        auto away = glm::rotate(glm::mat4(1), glm::radians(180.0f), glm::vec3(0, 1, 0));
        renderer.Sort(away, projection, glm::vec4(0, 0, 256, 256), glm::vec2(.1f, 100));
        renderer.Check(0);
      }
    }
    std::puts("PASS: multi-workgroup radix ordering, index uniqueness, indirect count, empty/repopulated frustum");
    result = 0;
  } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); }
  if (window) glfwDestroyWindow(window);
  glfwTerminate();
  std::filesystem::remove(path);
  return result;
}
