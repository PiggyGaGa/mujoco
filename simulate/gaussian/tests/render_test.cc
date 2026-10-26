#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <mujoco/mujoco.h>
#include "viewer_bridge.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <chrono>
int main() {
  GLFWwindow* window=nullptr;
  mjModel* model=nullptr; mjData* data=nullptr;
  mjvScene scene; mjv_defaultScene(&scene);
  mjrContext context; mjr_defaultContext(&context);
  auto ply=std::filesystem::temp_directory_path()/
    ("mjgs-render-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ply");
  int result=1;
  try {
    std::ofstream f(ply,std::ios::binary);
    f<<"ply\nformat binary_little_endian 1.0\nelement vertex 2\n";
    for (auto name:{"x","y","z","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3","f_dc_0","f_dc_1","f_dc_2"})
      f<<"property float "<<name<<"\n";
    f<<"end_header\n";
    for (int i=0;i<2;++i) {
      float row[14]={i?-.6f:.3f,0,i?-3.f:-1.5f,8,-2.8f,-2.8f,-2.8f,1,0,0,0,1.772454f,-1.772454f,-1.772454f};
      f.write(reinterpret_cast<char*>(row),sizeof(row));
    }
    f.close();
    std::string path=ply.string(); char name[]="test", flag[]="--gs-ply";
    char* argv[]={name,flag,path.data(),nullptr}; int argc=3;
    if (!mjgs::Configure(argc,argv)) throw std::runtime_error("configure failed");
    if (!glfwInit()) throw std::runtime_error("glfw init failed");
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_SAMPLES,4);
    window=glfwCreateWindow(256,256,"3DGS depth test",nullptr,nullptr);
    if (!window) throw std::runtime_error("window creation failed");
    glfwMakeContextCurrent(window);
    std::printf("GL: %s\n", glGetString(GL_VERSION));
    const char* xml=R"(<mujoco><visual><headlight ambient="1 1 1" diffuse="0 0 0"/></visual><worldbody>
      <camera name="test" pos="0 0 0" fovy="60"/>
      <geom type="box" pos=".4 0 -2" size=".25 .35 .05" rgba="0 1 0 1"/>
      <geom type="box" pos="-.4 0 -2" size=".25 .35 .05" rgba="0 1 0 1"/>
      </worldbody></mujoco>)";
    auto* spec=mj_parseXMLString(xml,nullptr,nullptr,0);
    if (!spec) throw std::runtime_error("XML parse failed");
    model=mj_compile(spec,nullptr); mj_deleteSpec(spec);
    if (!model) throw std::runtime_error("model compile failed");
    data=mj_makeData(model); mj_forward(model,data);
    mjv_makeScene(model,&scene,1000); mjr_makeContext(model,&context,mjFONTSCALE_100);
    mjvCamera camera; mjv_defaultCamera(&camera); camera.type=mjCAMERA_FIXED; camera.fixedcamid=0;
    mjvOption options; mjv_defaultOption(&options);
    mjv_updateScene(model,data,&options,nullptr,&camera,mjCAT_ALL,&scene);
    mjr_setBuffer(mjFB_OFFSCREEN,&context);
    mjrRect rect{17,23,256,256}; mjr_render(rect,&scene,&context);
    auto cam=mjv_averageCamera(scene.camera,scene.camera+1);
    std::printf("ngeom=%d pos=%g,%g,%g forward=%g,%g,%g near=%g far=%g top=%g bottom=%g\n",scene.ngeom,cam.pos[0],cam.pos[1],cam.pos[2],cam.forward[0],cam.forward[1],cam.forward[2],cam.frustum_near,cam.frustum_far,cam.frustum_top,cam.frustum_bottom);
    mjgs::Render(scene,rect);
    unsigned char pixels[256*256*3]; mjr_readPixels(pixels,nullptr,rect,&context);
    std::ofstream image("/tmp/mjgs-depth-test.ppm",std::ios::binary);
    image<<"P6\n256 256\n255\n"; image.write(reinterpret_cast<char*>(pixels),sizeof(pixels));
    int colored=0; for (auto v:pixels) if(v) ++colored;
    std::printf("nonzero channels=%d buffer=%d glerror=%x\n",colored,context.currentBuffer,glGetError());
    auto* front=pixels+(128*256+172)*3;
    auto* behind=pixels+(128*256+84)*3;
    std::printf("front RGB=%d,%d,%d behind RGB=%d,%d,%d\n",front[0],front[1],front[2],behind[0],behind[1],behind[2]);
    if (front[0] < 150 || front[1]>100 || behind[1]<150 || behind[0]>100)
      throw std::runtime_error("Front/back occlusion check failed");
    // A second native frame must not inherit the splat program/bindings/depth state.
    mjr_render(rect,&scene,&context);
    mjgs::ToggleVisible(); mjgs::Render(scene,rect);
    mjr_readPixels(pixels,nullptr,rect,&context);
    if (pixels[(128*256+172)*3+1]<150) throw std::runtime_error("Native rendering/toggle regression");
    mjgs::ToggleVisible();
    // Empty splat frusta must not launch zero-sized sorting kernels.
    for (auto& c : scene.camera) { c.forward[2]=1; c.forward[0]=c.forward[1]=0; }
    mjgs::Render(scene,rect);
    if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL state error");
    std::puts("Depth, viewport offset, visibility, native state restoration and empty-frustum tests passed"); result=0;
  } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); }
  if(window) { mjgs::Shutdown(); mjr_freeContext(&context); mjv_freeScene(&scene); }
  mj_deleteData(data); mj_deleteModel(model);
  if(window) glfwDestroyWindow(window); glfwTerminate(); std::filesystem::remove(ply);
  return result;
}
