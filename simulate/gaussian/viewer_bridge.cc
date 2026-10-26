#include <GL/glew.h>  // Must precede any OpenGL headers.
#include "viewer_bridge.h"
#include "scene_config.h"
#include "gl_state.h"
#include "gaussiancloud.h"
#include "splatrenderer.h"
#include "core/util.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mjgs {
namespace {
SceneConfig config;
std::unique_ptr<SplatRenderer> renderer;
bool attempted = false;
bool warned = false;
bool visible = true;
bool exit_requested = false;
int frames = 0;
float Number(const char* text) {
  size_t end = 0;
  float x = std::stof(text, &end);
  if (text[end] || !std::isfinite(x)) throw std::runtime_error("Expected finite number");
  return x;
}
void Capture(mjrRect r) {
  if (++frames != config.capture_frame || config.screenshot.empty()) return;
  std::vector<unsigned char> pixels(static_cast<size_t>(r.width)*r.height*3);
  GLint pack; glGetIntegerv(GL_PACK_ALIGNMENT, &pack); glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(r.left,r.bottom,r.width,r.height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, pack);
  std::ofstream file(config.screenshot, std::ios::binary);
  file << "P6\n" << r.width << " " << r.height << "\n255\n";
  for (int y=r.height-1; y>=0; --y)
    file.write(reinterpret_cast<char*>(pixels.data()+static_cast<size_t>(y)*r.width*3),r.width*3);
  if (!file) throw std::runtime_error("Cannot write screenshot");
  std::fprintf(stderr,"[3DGS] Screenshot: %s\n",config.screenshot.c_str());
  exit_requested = config.exit_after_capture;
}
}
bool Configure(int& argc, char** argv) {
  try {
    int output = 1;
    for (int i=1; i<argc; ++i) {
      const std::string key = argv[i];
      auto next = [&]() -> const char* {
        if (++i >= argc) throw std::runtime_error("Missing argument after " + key);
        return argv[i];
      };
      if (key == "--gs-ply") config.ply = next();
      else if (key == "--gs-scale") config.scale = Number(next());
      else if (key == "--gs-translate" || key == "--gs-rotate") {
        glm::vec3 value;
        for (int j=0;j<3;++j) value[j] = Number(next());
        (key == "--gs-translate" ? config.translation : config.rotation_degrees) = value;
      } else if (key == "--gs-exit-after-capture") config.exit_after_capture = true;
      else if (key == "--gs-no-sh") config.full_sh = false;
      else if (key == "--gs-screenshot") config.screenshot = next();
      else if (key == "--gs-capture-frame") {
        float n = Number(next());
        if (n < 1 || n > 1000000 || n != std::floor(n)) throw std::runtime_error("Invalid capture frame");
        config.capture_frame = static_cast<int>(n);
      } else if (key == "--gs-help") {
        std::puts("simulate model.xml --gs-ply scene.ply [--gs-scale S] [--gs-translate X Y Z]\n"
                  "  [--gs-rotate RX RY RZ] (degrees, Rz*Ry*Rx) [--gs-no-sh]\n"
                  "  [--gs-screenshot image.ppm --gs-capture-frame N]\n"
                  "PLY adds appearance only; collision geometry belongs in MJCF.");
        std::exit(0);
      } else if (key.rfind("--gs-",0)==0) throw std::runtime_error("Unknown option: " + key);
      else argv[output++] = argv[i];
    }
    argc = output; argv[output] = nullptr;
    if (config.exit_after_capture && config.screenshot.empty()) throw std::runtime_error("Exit-after-capture requires --gs-screenshot");
    if (config.scale <= 0) throw std::runtime_error("--gs-scale must be positive");
    if (!config.screenshot.empty() && !config.capture_frame) config.capture_frame = 10;
    if (!config.ply.empty()) ValidatePly(config.ply);
    return true;
  } catch (const std::exception& e) { std::fprintf(stderr,"[3DGS] %s\n",e.what()); return false; }
}
void Render(const mjvScene& scene, mjrRect rect) {
  if (rect.width<=0 || rect.height<=0 || scene.ngeom == 0) return;
  // No asset means exactly the upstream viewer rendering, apart from optional capture.
  if (config.ply.empty()) { if (!config.screenshot.empty()) Capture(rect); return; }
  try {
    if (!attempted) {
      attempted = true;
      glewExperimental = GL_TRUE;
      if (glewInit()!=GLEW_OK) throw std::runtime_error("GLEW initialization failed");
      while (glGetError()!=GL_NO_ERROR) {} // GLEW may probe unsupported legacy enums.
      GLint profile=0; glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
      if (!GLEW_VERSION_4_6 || (profile & GL_CONTEXT_CORE_PROFILE_BIT))
        throw std::runtime_error("Need OpenGL 4.6 compatibility profile; normal viewer remains available");
      GlState state;
      SetRootPath(MJGS_SHADER_ROOT);
      auto cloud=std::make_shared<GaussianCloud>(GaussianCloud::Options{config.full_sh,false});
      if (!cloud->ImportPly(config.ply) || !cloud->GetNumGaussians())
        throw std::runtime_error("Gaussian asset loading failed");
      auto candidate=std::make_unique<SplatRenderer>();
      if (!candidate->Init(cloud,false,false)) throw std::runtime_error("Splat renderer initialization failed");
      renderer=std::move(candidate);
      std::fprintf(stderr,"[3DGS] Loaded %zu Gaussians, GL %s\n",cloud->GetNumGaussians(),glGetString(GL_VERSION));
    }
    if (!renderer) return;
    if (!visible) { if (!config.screenshot.empty()) Capture(rect); return; }
    const auto camera=mjv_averageCamera(scene.camera,scene.camera+1);
    if (scene.stereo!=mjSTEREO_NONE || camera.orthographic || scene.enabletransform) {
      if (!warned) std::fprintf(stderr,"[3DGS] Stereo/orthographic/scene transforms not supported; splats hidden\n");
      warned=true; return;
    }
    const glm::vec3 pos(camera.pos[0],camera.pos[1],camera.pos[2]);
    const glm::vec3 forward(camera.forward[0],camera.forward[1],camera.forward[2]);
    const glm::vec3 up(camera.up[0],camera.up[1],camera.up[2]);
    const glm::mat4 view=glm::lookAt(pos,pos+forward,up);
    float halfwidth=camera.frustum_width ? camera.frustum_width
      : 0.5f*rect.width/rect.height*(camera.frustum_top-camera.frustum_bottom);
    glm::mat4 projection=glm::frustum(camera.frustum_center-halfwidth,
      camera.frustum_center+halfwidth,camera.frustum_bottom,camera.frustum_top,
      camera.frustum_near,camera.frustum_far);
    glm::mat4 local_camera=glm::inverse(WorldFromGaussian(config))*glm::inverse(view);
    GlState state;
    // Use conventional clip coordinates for Splatapult; fragment shader writes reversed depth.
    glClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
    glViewport(rect.left,rect.bottom,rect.width,rect.height);
    glEnable(GL_SCISSOR_TEST); glScissor(rect.left,rect.bottom,rect.width,rect.height);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); glDepthMask(GL_FALSE); glDepthRange(0,1);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const glm::vec4 viewport(rect.left,rect.bottom,rect.width,rect.height);
    const glm::vec2 nearfar(camera.frustum_near,camera.frustum_far);
    renderer->Sort(local_camera,projection,viewport,nearfar);
    glMemoryBarrier(GL_ELEMENT_ARRAY_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    renderer->Render(local_camera,projection,viewport,nearfar);
    if (!config.screenshot.empty()) Capture(rect);
  } catch (const std::exception& e) {
    std::fprintf(stderr,"[3DGS] Disabled: %s\n",e.what()); renderer.reset();
  }
}
void Shutdown() { renderer.reset(); }
void ToggleVisible() { visible = !visible; std::fprintf(stderr,"[3DGS] %s\n",visible ? "Visible" : "Hidden"); }
bool ExitRequested() { return exit_requested; }
}
