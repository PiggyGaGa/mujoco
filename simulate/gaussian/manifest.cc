#include "manifest.h"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tinyxml2.h>

namespace mjgs {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
void Require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
void Keys(const Json& j, std::initializer_list<const char*> required,
          std::initializer_list<const char*> optional = {}) {
  Require(j.is_object(), "Expected JSON object");
  std::set<std::string> allowed;
  for (auto k : required) { Require(j.contains(k), std::string("Missing field: ")+k); allowed.insert(k); }
  for (auto k : optional) allowed.insert(k);
  for (auto it=j.begin(); it!=j.end(); ++it)
    Require(allowed.count(it.key()), "Unsupported field: "+it.key());
}
fs::path Resolve(const fs::path& root, const fs::path& directory, const std::string& raw) {
  fs::path p(raw);
  Require(!raw.empty() && !p.is_absolute() && raw.find('\\') == std::string::npos &&
          raw.find(':') == std::string::npos, "Expected portable relative asset path: "+raw);
  for (const auto& part : p) Require(part != "..", "Parent traversal in asset path");
  p = fs::canonical(directory / p);
  auto relative = p.lexically_relative(root);
  Require(!relative.empty() && *relative.begin() != ".." && fs::is_regular_file(p),
          "Asset must stay inside manifest directory: "+raw);
  return p;
}
std::string Hash(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  Require(file.good(), "Cannot read asset: "+path.string());
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  Require(ctx && EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr)==1, "SHA256 init failed");
  char buffer[65536];
  while (file.read(buffer, sizeof(buffer)) || file.gcount())
    Require(EVP_DigestUpdate(ctx.get(), buffer, file.gcount())==1, "SHA256 update failed");
  Require(file.eof(), "Asset read failed");
  unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length;
  Require(EVP_DigestFinal_ex(ctx.get(), digest, &length)==1, "SHA256 failed");
  std::ostringstream result;
  for (unsigned int i=0; i<length; ++i) result << std::hex << std::setw(2) << std::setfill('0') << int(digest[i]);
  return result.str();
}
fs::path File(const Json& j, const fs::path& root) {
  auto path = Resolve(root, root, j.at("path").get<std::string>());
  auto digest = j.at("sha256").get<std::string>();
  std::transform(digest.begin(), digest.end(), digest.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  Require(digest.size()==64 && digest.find_first_not_of("0123456789abcdef")==std::string::npos,
          "Invalid SHA256");
  Require(Hash(path)==digest, "SHA256 mismatch: "+path.string());
  return path;
}
template <size_t N> std::array<double,N> Vector(const Json& j) {
  Require(j.is_array() && j.size()==N, "Invalid pose vector length");
  std::array<double,N> value;
  for (size_t i=0;i<N;++i) { Require(j[i].is_number(), "Pose must contain numbers"); value[i]=j[i].get<double>(); Require(std::isfinite(value[i]), "Nonfinite pose"); }
  if constexpr (N==4) {
    double norm=0; for (double x : value) norm+=x*x;
    Require(std::abs(std::sqrt(norm)-1)<=2e-6, "Expected unit wxyz quaternion");
  }
  return value;
}
// Match mjlab v1's dependency discovery on the primary collision XML.
void References(tinyxml2::XMLElement* e, const fs::path& root, const fs::path& dir,
                const std::string& asset, const std::string& mesh,
                const std::string& texture, std::set<fs::path>& refs) {
  for (; e; e=e->NextSiblingElement()) {
    Require(std::string(e->Name())!="include", "Collision MJCF includes are not supported (mjlab convention)");
    if (auto file=e->Attribute("file")) {
      std::string tag=e->Name();
      auto subdir = tag=="include" ? "" : tag=="mesh" ? mesh : tag=="texture" ? texture : asset;
      refs.insert(Resolve(root, dir, (fs::path(subdir)/file).generic_string()));
    }
    References(e->FirstChildElement(), root, dir, asset, mesh, texture, refs);
  }
}
}
Manifest LoadManifest(const std::string& path, const std::string& spawn) {
  auto source=fs::canonical(path); auto root=source.parent_path();
  std::ifstream stream(source); Json j; stream >> j;
  Keys(j, {"schema_version","coordinate_system","visual","collision","world_from_scene","spawn_points"}, {"$schema"});
  Require(j["schema_version"].is_number_integer() && j["schema_version"]==1, "Unsupported schema_version");
  Require(j["coordinate_system"]==Json({{"handedness","right"},{"up_axis","z"},{"length_unit","meter"}}), "Expected right-handed Z-up meters");
  Keys(j["visual"], {"type","path","sha256"});
  Keys(j["collision"], {"type","path","sha256"}, {"geom_group","dependencies"});
  Require(j["visual"]["type"]=="3dgs_ply" && j["collision"]["type"]=="mjcf", "Invalid asset type");
  Manifest m; m.visual=File(j["visual"],root).string(); m.collision=File(j["collision"],root).string();
  if (j["collision"].contains("geom_group")) {
    Require(j["collision"]["geom_group"].is_number_integer(), "geom_group must be integer");
    m.geom_group=j["collision"]["geom_group"].get<int>();
  }
  Require(m.geom_group>=0 && m.geom_group<=5, "geom_group outside [0,5]");
  std::set<fs::path> declared, referenced;
  auto dependencies=j["collision"].value("dependencies",Json::array());
  Require(dependencies.is_array(), "dependencies must be array");
  for (const auto& dep : dependencies) {
    Keys(dep,{"path","sha256"});
    Require(declared.insert(File(dep,root)).second, "Duplicate dependency");
  }
  tinyxml2::XMLDocument xml;
  Require(xml.LoadFile(m.collision.c_str())==tinyxml2::XML_SUCCESS, "Invalid collision XML");
  auto document=xml.RootElement(); Require(document, "Empty collision XML");
  auto compiler=document->FirstChildElement("compiler");
  auto attr=[&](const char* name) { auto v=compiler ? compiler->Attribute(name) : nullptr; return std::string(v ? v : ""); };
  auto asset=attr("assetdir");
  for (auto name : {"assetdir","meshdir","texturedir"}) {
    auto raw=attr(name); fs::path dir(raw);
    Require(!dir.is_absolute() && raw.find('\\')==std::string::npos && raw.find(':')==std::string::npos,
            "Collision compiler directories must be relative");
    for (const auto& part : dir) Require(part!="..", "Parent traversal in compiler directory");
  }
  References(document, root, fs::path(m.collision).parent_path(), asset,
             asset.empty()?attr("meshdir"):asset, asset.empty()?attr("texturedir"):asset, referenced);
  Require(declared==referenced, "collision.dependencies does not match XML file references");
  Keys(j["world_from_scene"], {"position","quaternion_wxyz"});
  m.position=Vector<3>(j["world_from_scene"]["position"]);
  m.quaternion=Vector<4>(j["world_from_scene"]["quaternion_wxyz"]);
  const auto& points=j["spawn_points"];
  Require(points.is_array() && !points.empty(), "spawn_points must be nonempty array");
  std::set<std::string> names; int selected=-1;
  for (size_t i=0;i<points.size();++i) {
    const auto& p=points[i]; Keys(p,{"name","position","quaternion_wxyz"});
    auto name=p["name"].get<std::string>();
    Require(!name.empty() && names.insert(name).second, "Invalid/duplicate spawn name");
    Vector<3>(p["position"]); Vector<4>(p["quaternion_wxyz"]);
    if (name==(spawn.empty()?"default":spawn)) selected=i;
  }
  if (selected<0 && spawn.empty() && points.size()==1) selected=0;
  Require(selected>=0, "Select an existing --scene-spawn-point");
  auto pos=Vector<3>(points[selected]["position"]);
  auto quat=Vector<4>(points[selected]["quaternion_wxyz"]);
  mju_rotVecQuat(m.spawn_position.data(),pos.data(),m.quaternion.data());
  for (int i=0;i<3;++i) m.spawn_position[i]+=m.position[i];
  mju_mulQuat(m.spawn_quaternion.data(),m.quaternion.data(),quat.data());
  return m;
}
mjModel* CompileManifest(const Manifest& m, const std::string& base) {
  using Spec=std::unique_ptr<mjSpec,decltype(&mj_deleteSpec)>;
  char error[2048]={};
  Spec parent(base.empty()?mj_makeSpec():mj_parseXML(base.c_str(),nullptr,error,sizeof(error)),mj_deleteSpec);
  Require(bool(parent), "Base XML: "+std::string(error));
  Spec child(mj_parseXML(m.collision.c_str(),nullptr,error,sizeof(error)),mj_deleteSpec);
  Require(bool(child), "Collision XML: "+std::string(error));
  for (auto type : {mjOBJ_JOINT,mjOBJ_ACTUATOR,mjOBJ_SENSOR})
    Require(!mjs_firstElement(child.get(),type), "Collision MJCF must be static, without joints/actuators/sensors");
  for (auto e=mjs_firstElement(child.get(),mjOBJ_BODY); e; e=mjs_nextElement(child.get(),e))
    Require(!mjs_asBody(e)->mocap, "Collision MJCF cannot contain mocap bodies");
  bool collides=false;
  for (auto e=mjs_firstElement(child.get(),mjOBJ_GEOM); e; e=mjs_nextElement(child.get(),e)) {
    auto g=mjs_asGeom(e); g->group=m.geom_group;
    Require(g->type>=mjGEOM_PLANE && g->type<=mjGEOM_MESH, "Unsupported collision geom type");
    collides |= g->contype || g->conaffinity;
  }
  Require(collides, "Collision MJCF has no collision-enabled geoms");
  auto frame=mjs_addFrame(mjs_findBody(parent.get(),"world"),nullptr);
  std::copy(m.position.begin(),m.position.end(),frame->pos);
  std::copy(m.quaternion.begin(),m.quaternion.end(),frame->quat);
  Require(mjs_attach(frame->element,mjs_findBody(child.get(),"world")->element,"manifest/","")!=nullptr,
          "Collision attach failed: "+std::string(mjs_getError(parent.get())));
  auto model=mj_compile(parent.get(),nullptr);
  Require(model!=nullptr, "Manifest compile failed: "+std::string(mjs_getError(parent.get())));
  return model;
}
}
