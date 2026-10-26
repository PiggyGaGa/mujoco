#include "scene_config.h"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
int main() {
  auto path=std::filesystem::temp_directory_path()/
    ("mjgs-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ply");
  try {
    mjgs::SceneConfig c; c.scale=2; c.rotation_degrees.z=90; c.translation={1,2,3};
    glm::vec4 p=mjgs::WorldFromGaussian(c)*glm::vec4(1,0,0,1);
    if (glm::length(glm::vec3(p)-glm::vec3(1,4,3))>1e-5f) throw std::runtime_error("Transform mismatch");
    auto write=[&](bool valid, bool truncated) {
      std::ofstream f(path,std::ios::binary);
      f<<"ply\nformat binary_little_endian 1.0\nelement vertex 1\n";
      for (auto name : {"x","y","z","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3","f_dc_0","f_dc_1","f_dc_2"})
        f<<"property float "<<name<<"\n";
      f<<"end_header\n";
      float row[14]={0,0,-2,1,0,0,0,valid?1.f:0.f,0,0,0,0,0,0};
      if (!truncated) f.write(reinterpret_cast<char*>(row),sizeof(row));
    };
    write(true,false); mjgs::ValidatePly(path.string());
    for (int i=0;i<2;++i) {
      write(false,i==1); bool rejected=false;
      try { mjgs::ValidatePly(path.string()); } catch (const std::exception&) { rejected=true; }
      if (!rejected) throw std::runtime_error("Invalid PLY accepted");
    }
    std::filesystem::remove(path);
    std::cout<<"Transform and PLY validation passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::filesystem::remove(path); std::cerr<<e.what()<<'\n'; return 1;
  }
}
