# MuJoCo/Splatapult adapter. Splatapult remains an unmodified third-party
# checkout; only the renderer sources needed by simulate are compiled here.

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
if(SIMULATE_BUILD_EXECUTABLE)
  find_package(OpenSSL REQUIRED)
  find_path(MJGS_JSON_INCLUDE nlohmann/json.hpp REQUIRED)
  add_library(mujoco_3dgs_manifest STATIC manifest.cc)
  target_compile_features(mujoco_3dgs_manifest PUBLIC cxx_std_17)
  target_include_directories(mujoco_3dgs_manifest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE ${MJGS_JSON_INCLUDE})
  target_link_libraries(mujoco_3dgs_manifest PUBLIC mujoco::mujoco PRIVATE OpenSSL::Crypto tinyxml2)
endif()

set(MJGS_SPLAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/splatapult")
set(MJGS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MJGS_GENERATED_DIR}/shader")

function(mjgs_replace_once variable old new)
  string(FIND "${${variable}}" "${old}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Splatapult adaptation no longer matches upstream: ${old}")
  endif()
  string(REPLACE "${old}" "${new}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${MJGS_SPLAT_ROOT}/src/splatrenderer.cpp"
  "${MJGS_SPLAT_ROOT}/src/core/util.cpp")

file(GLOB splat_shaders CONFIGURE_DEPENDS "${MJGS_SPLAT_ROOT}/shader/*")
foreach(shader IN LISTS splat_shaders)
  get_filename_component(name "${shader}" NAME)
  configure_file("${shader}" "${MJGS_GENERATED_DIR}/shader/${name}" COPYONLY)
endforeach()

# Adapt Splatapult's projection/depth conventions to MuJoCo's native viewer
# without modifying the third-party checkout.
file(READ "${MJGS_GENERATED_DIR}/shader/splat_vert.glsl" vertex)
mjgs_replace_once(vertex "float X0 = viewport.x * (0.00001f * projParams.y);"
  "float X0 = viewport.x;")
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_vert.glsl" "${vertex}")

file(READ "${MJGS_GENERATED_DIR}/shader/splat_frag.glsl" fragment)
mjgs_replace_once(fragment "    vec2 d ="
  "    gl_FragDepth = 1.0 - gl_FragCoord.z;\n    vec2 d =")
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_frag.glsl" "${fragment}")

file(READ "${MJGS_GENERATED_DIR}/shader/splat_geom.glsl" geometry)
mjgs_replace_once(geometry "ndcP.z < 0.25f ||"
  "ndcP.z < -1.0f || ndcP.z > 1.0f ||")
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_geom.glsl" "${geometry}")

file(READ "${MJGS_GENERATED_DIR}/shader/presort_compute.glsl" presort)
mjgs_replace_once(presort "depth > 0.0f &&"
  "depth >= nearFar.x && depth <= nearFar.y &&")
file(WRITE "${MJGS_GENERATED_DIR}/shader/presort_compute.glsl" "${presort}")

# Embed the adapted shaders so installed viewers never depend on a build tree.
set(embedded "#pragma once\n#include <string>\ninline bool MjgsLoadShader(const std::string& name, std::string& data) {\n")
foreach(shader IN LISTS splat_shaders)
  get_filename_component(name "${shader}" NAME)
  file(READ "${MJGS_GENERATED_DIR}/shader/${name}" source)
  string(FIND "${source}" ")mjgs_shader\"" delimiter)
  if(NOT delimiter EQUAL -1)
    message(FATAL_ERROR "Embedded shader delimiter occurs in ${name}")
  endif()
  string(APPEND embedded "if (name == \"shader/${name}\") { data = R\"mjgs_shader(${source})mjgs_shader\"; return true; }\n")
endforeach()
string(APPEND embedded "return false;\n}\n")
file(WRITE "${MJGS_GENERATED_DIR}/mjgs_embedded_shaders.h" "${embedded}")
file(READ "${MJGS_SPLAT_ROOT}/src/core/util.cpp" util_source)
mjgs_replace_once(util_source "bool LoadFile(const std::string& filename, std::string& data)\n{"
  "bool LoadFile(const std::string& filename, std::string& data)\n{\n    if (MjgsLoadShader(filename, data)) return true;")
file(WRITE "${MJGS_GENERATED_DIR}/util.cpp" "#include \"mjgs_embedded_shaders.h\"\n${util_source}")

file(READ "${MJGS_SPLAT_ROOT}/src/splatrenderer.cpp" splat_source)
mjgs_replace_once(splat_source
  "    if (useMultiRadixSort)\n    {\n        ZoneScopedNC(\"sort\""
  "    if (sortCount == 0) return;\n    if (useMultiRadixSort)\n    {\n        ZoneScopedNC(\"sort\"")
mjgs_replace_once(splat_source
  "        splatProg->SetUniform(\"projParams\", glm::vec4(0.0f, nearFar.x, nearFar.y, 0.0f));"
  "        // projParams is optimized out after removing the viewport-X workaround.")
file(WRITE "${MJGS_GENERATED_DIR}/splatrenderer.cpp" "${splat_source}")

set(splat_src "${MJGS_SPLAT_ROOT}/src")
add_library(mujoco_3dgs_splat STATIC
  "${splat_src}/gaussiancloud.cpp"
  "${splat_src}/ply.cpp"
  "${MJGS_GENERATED_DIR}/splatrenderer.cpp"
  "${splat_src}/core/binaryattribute.cpp"
  "${splat_src}/core/program.cpp"
  "${splat_src}/core/vertexbuffer.cpp"
  "${MJGS_GENERATED_DIR}/util.cpp"
  "${splat_src}/core/log.cpp")
add_library(mujoco::3dgs_splat ALIAS mujoco_3dgs_splat)
target_compile_features(mujoco_3dgs_splat PUBLIC cxx_std_17)
target_include_directories(mujoco_3dgs_splat PUBLIC "${splat_src}" PRIVATE "${splat_src}/core" "${MJGS_GENERATED_DIR}")
target_link_libraries(mujoco_3dgs_splat PUBLIC
  GLEW::GLEW glm::glm Eigen3::Eigen OpenGL::GL SDL2::SDL2)
set_target_properties(mujoco_3dgs_splat PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mujoco_3dgs_renderer STATIC
  viewer_bridge.cc scene_config.cc ply_validation.cc)
add_library(mujoco::3dgs_renderer ALIAS mujoco_3dgs_renderer)
target_compile_features(mujoco_3dgs_renderer PUBLIC cxx_std_17)
target_include_directories(mujoco_3dgs_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mujoco_3dgs_renderer
  PRIVATE mujoco_3dgs_splat
  PUBLIC mujoco::mujoco)
set_target_properties(mujoco_3dgs_renderer PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MUJOCO_BUILD_3DGS_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
