# MuJoCo/Splatapult adapter. Splatapult remains an unmodified third-party
# checkout; only the renderer sources needed by simulate are compiled here.

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

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
  "${MJGS_SPLAT_ROOT}/src/splatrenderer.cpp")

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
  "${splat_src}/core/util.cpp"
  "${splat_src}/core/log.cpp")
add_library(mujoco::3dgs_splat ALIAS mujoco_3dgs_splat)
target_compile_features(mujoco_3dgs_splat PUBLIC cxx_std_17)
target_include_directories(mujoco_3dgs_splat PUBLIC "${splat_src}")
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
target_compile_definitions(mujoco_3dgs_renderer PRIVATE
  MJGS_SHADER_ROOT="${MJGS_GENERATED_DIR}/")
set_target_properties(mujoco_3dgs_renderer PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MUJOCO_BUILD_3DGS_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
