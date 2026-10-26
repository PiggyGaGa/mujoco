#include "scene_config.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace mjgs {
void ValidatePly(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot open PLY: " + path);
  std::string line;
  if (!std::getline(file, line) || (line != "ply" && line != "ply\r"))
    throw std::runtime_error("Not a PLY file");
  std::map<std::string, size_t> fields;
  const std::map<std::string, size_t> sizes = {{"float",4},{"float32",4},{"double",8},
    {"uchar",1},{"char",1},{"ushort",2},{"short",2},{"uint",4},{"int",4}};
  size_t count = 0, stride = 0;
  bool vertex = false, little = false, ended = false;
  for (int n = 0; n < 4096 && std::getline(file, line); ++n) {
    std::istringstream in(line);
    std::string tag, type, name;
    in >> tag;
    if (tag == "format") { in >> type; little = type == "binary_little_endian"; }
    else if (tag == "element") {
      in >> name;
      if (name != "vertex") throw std::runtime_error("Only vertex-only Gaussian PLY is supported");
      if (vertex || !(in >> count)) throw std::runtime_error("Invalid vertex count");
      vertex = true;
    } else if (tag == "property") {
      in >> type >> name;
      auto size = sizes.find(type);
      if (!vertex || size == sizes.end() || stride > 65536)
        throw std::runtime_error("Unsupported PLY property type/layout");
      // Splatapult's BinaryAttribute expects standard float fields for Gaussian parameters.
      if (type == "float") fields[name] = stride;
      stride += size->second;
    } else if (tag == "end_header") { ended = true; break; }
  }
  if (!little || !ended || !count || !stride)
    throw std::runtime_error("Expected nonempty binary little-endian Gaussian PLY");
  std::vector<std::string> required = {"x", "y", "z", "opacity"};
  for (auto pair : {std::make_pair("rot_",4), {"scale_",3}, {"f_dc_",3}})
    for (int i=0; i<pair.second; ++i) required.push_back(pair.first + std::to_string(i));
  for (const auto& field : required)
    if (!fields.count(field)) throw std::runtime_error("Missing float Gaussian field: " + field);
  const auto begin = file.tellg();
  file.seekg(0, std::ios::end);
  const auto bytes = file.tellg() - begin;
  if (bytes < 0 || count > static_cast<size_t>(bytes) / stride || count > 0x7fffffffu)
    throw std::runtime_error("Truncated or oversized Gaussian PLY");
  file.seekg(begin);
  std::vector<char> row(stride);
  for (size_t i = 0; i < count; ++i) {
    file.read(row.data(), row.size());
    auto value = [&](const std::string& key) {
      float v; std::memcpy(&v, row.data() + fields.at(key), sizeof(v)); return v;
    };
    for (const auto& field : fields)
      if (!std::isfinite(value(field.first))) throw std::runtime_error("Non-finite PLY parameter");
    double norm = 0;
    for (int j = 0; j < 4; ++j) { double q = value("rot_" + std::to_string(j)); norm += q*q; }
    if (norm < 1e-12 || std::abs(norm - 1) > 0.02)
      throw std::runtime_error("PLY quaternions must be normalized wxyz");
    for (int j = 0; j < 3; ++j) {
      float s = std::exp(value("scale_" + std::to_string(j)));
      if (!std::isfinite(s) || s <= 0) throw std::runtime_error("Invalid Gaussian scale");
    }
  }
}
}
