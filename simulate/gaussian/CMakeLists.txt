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
  "${MJGS_SPLAT_ROOT}/src/splatrenderer.h"
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
mjgs_replace_once(vertex "in vec4 r_sh0;  // sh coeff for red channel (up to third-order)"
  "#ifdef DC_ONLY\nin float r_sh0;\n#else\nin vec4 r_sh0;  // sh coeff for red channel (up to third-order)\n#endif")
mjgs_replace_once(vertex "in vec4 g_sh0;  // sh coeff for green channel"
  "#ifdef DC_ONLY\nin float g_sh0;\n#else\nin vec4 g_sh0;  // sh coeff for green channel\n#endif")
mjgs_replace_once(vertex "in vec4 b_sh0;  // sh coeff for blue channel"
  "#ifdef DC_ONLY\nin float b_sh0;\n#else\nin vec4 b_sh0;  // sh coeff for blue channel\n#endif")
mjgs_replace_once(vertex "vec3 ComputeRadianceFromSH(const vec3 v)\n{"
  "vec3 ComputeRadianceFromSH(const vec3 v)\n{\n#ifdef DC_ONLY\n    return vec3(0.5f) + 0.28209479177387814f * vec3(r_sh0, g_sh0, b_sh0);\n#else")
mjgs_replace_once(vertex "}\n\n#ifdef FRAMEBUFFER_SRGB"
  "#endif\n}\n\n#ifdef FRAMEBUFFER_SRGB")
mjgs_replace_once(vertex
  "    vec3 v = normalize(position.xyz - eye);\n    geom_color = vec4(ComputeRadianceFromSH(v), alpha);"
  "#ifdef DC_ONLY\n    geom_color = vec4(ComputeRadianceFromSH(vec3(0.0f)), alpha);\n#else\n    vec3 v = normalize(position.xyz - eye);\n    geom_color = vec4(ComputeRadianceFromSH(v), alpha);\n#endif")
# Reverse clip Z before rasterization instead of writing gl_FragDepth in every
# fragment. With the conventional [-1, 1] clip range this is algebraically the
# same window depth, and lets the driver reject occluded splats before shading.
mjgs_replace_once(vertex "    gl_Position = p4;"
  "    gl_Position = vec4(p4.xy, -p4.z, p4.w);")
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_vert.glsl" "${vertex}")

file(READ "${MJGS_GENERATED_DIR}/shader/splat_frag.glsl" fragment)
# These values are constant across a splat's quad; avoid interpolation work.
foreach(declaration IN ITEMS "vec4 frag_color" "vec4 frag_cov2inv" "vec2 frag_p")
  mjgs_replace_once(fragment "in ${declaration};" "flat in ${declaration};")
endforeach()
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_frag.glsl" "${fragment}")

file(READ "${MJGS_GENERATED_DIR}/shader/splat_geom.glsl" geometry)
mjgs_replace_once(geometry "ndcP.z < 0.25f ||"
  "ndcP.z < -1.0f || ndcP.z > 1.0f ||")
foreach(declaration IN ITEMS "vec4 frag_color" "vec4 frag_cov2inv" "vec2 frag_p")
  mjgs_replace_once(geometry "out ${declaration};" "flat out ${declaration};")
endforeach()
file(WRITE "${MJGS_GENERATED_DIR}/shader/splat_geom.glsl" "${geometry}")

configure_file(presort_compute.glsl "${MJGS_GENERATED_DIR}/shader/presort_compute.glsl" COPYONLY)

# Keep the compacted visible count on the GPU.  The multi-radix shaders read
# the atomic counter directly, so sorting never has to wait for a CPU readback.
foreach(name IN ITEMS multi_radixsort.glsl multi_radixsort_histograms.glsl)
  file(READ "${MJGS_GENERATED_DIR}/shader/${name}" radix)
  mjgs_replace_once(radix "uniform uint g_num_elements;"
    "layout(binding = 4, offset = 0) uniform atomic_uint element_count;")
  string(REPLACE "g_num_elements" "num_elements" radix "${radix}")
  mjgs_replace_once(radix "void main() {"
    "void main() {\n    const uint num_elements = atomicCounter(element_count);")
  # The GPU count can be much smaller than buffer capacity. All lanes in a
  # workgroup use the same loop bound, preserving barrier participation while
  # avoiding 32 rounds of barriers in groups beyond the compacted visible set.
  mjgs_replace_once(radix
    "for (uint index = 0; index < g_num_blocks_per_workgroup; index++) {"
    "const uint group_begin = gl_WorkGroupID.x * g_num_blocks_per_workgroup * WORKGROUP_SIZE;\n    const uint remaining = num_elements > group_begin ? num_elements - group_begin : 0u;\n    const uint blocks = min(g_num_blocks_per_workgroup, (remaining + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE);\n    for (uint index = 0; index < blocks; index++) {")
  file(WRITE "${MJGS_GENERATED_DIR}/shader/${name}" "${radix}")
endforeach()

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
string(REPLACE
  "const uint32_t NUM_WORKGROUPS = (NUM_ELEMENTS + numBlocksPerWorkgroup - 1) / numBlocksPerWorkgroup;"
  "const uint32_t elementsPerWorkgroup = 256u * numBlocksPerWorkgroup;\n        const uint32_t NUM_WORKGROUPS = static_cast<uint32_t>((static_cast<uint64_t>(NUM_ELEMENTS) + elementsPerWorkgroup - 1) / elementsPerWorkgroup);"
  splat_source "${splat_source}")
mjgs_replace_once(splat_source
  "    isFramebufferSRGBEnabled = isFramebufferSRGBEnabledIn;\n    useRgcSortOverride = useRgcSortOverrideIn;"
  "    isFramebufferSRGBEnabled = isFramebufferSRGBEnabledIn;\n    useRgcSortOverride = useRgcSortOverrideIn;\n    hasFullSH = gaussianCloud->HasFullSH();")
mjgs_replace_once(splat_source
  "    if (isFramebufferSRGBEnabled || gaussianCloud->HasFullSH())\n    {\n        std::string defines = \"\";\n        if (isFramebufferSRGBEnabled)\n        {\n            defines += \"#define FRAMEBUFFER_SRGB\\n\";\n        }\n        if (gaussianCloud->HasFullSH())\n        {\n            defines += \"#define FULL_SH\\n\";\n        }\n        splatProg->AddMacro(\"DEFINES\", defines);\n    }"
  "    {\n        std::string defines = \"\";\n        if (isFramebufferSRGBEnabled)\n        {\n            defines += \"#define FRAMEBUFFER_SRGB\\n\";\n        }\n        if (hasFullSH)\n        {\n            defines += \"#define FULL_SH\\n\";\n        }\n        else\n        {\n            defines += \"#define DC_ONLY\\n\";\n        }\n        splatProg->AddMacro(\"DEFINES\", defines);\n    }")
mjgs_replace_once(splat_source
  "    if (useMultiRadixSort)\n    {\n        ZoneScopedNC(\"sort\""
  "    if (sortCount == 0) return;\n    if (useMultiRadixSort)\n    {\n        ZoneScopedNC(\"sort\"")
mjgs_replace_once(splat_source
  "    atomicCounterVec.resize(1, 0);"
  "    // Also serves as DrawElementsIndirectCommand: count, instanceCount, firstIndex, baseVertex, baseInstance.\n    atomicCounterVec.resize(5, 0);\n    atomicCounterVec[1] = 1;")
mjgs_replace_once(splat_source
  "        atomicCounterBuffer->Read(atomicCounterVec);\n        sortCount = atomicCounterVec[0];"
  "        if (useMultiRadixSort)\n        {\n            // Multi-radix shaders consume the GPU counter directly. Dispatch\n            // for the maximum capacity and let them bound work by the counter.\n            sortCount = static_cast<uint32_t>(numPoints);\n        }\n        else\n        {\n            atomicCounterBuffer->Read(atomicCounterVec);\n            sortCount = atomicCounterVec[0];\n        }")
mjgs_replace_once(splat_source
  "        sortProg->SetUniform(\"g_num_elements\", NUM_ELEMENTS);"
  "        // Visible element count is read from the GPU atomic counter.")
mjgs_replace_once(splat_source
  "        histogramProg->SetUniform(\"g_num_elements\", NUM_ELEMENTS);"
  "        // Visible element count is read from the GPU atomic counter.")
mjgs_replace_once(splat_source
  "    {\n        ZoneScopedNC(\"copy-sorted\", tracy::Color::DarkGreen);\n\n        if (useMultiRadixSort && (NUM_BYTES % 2) == 1)  // odd\n        {\n            glBindBuffer(GL_COPY_READ_BUFFER, valBuffer2->GetObj());\n        }\n        else\n        {\n            glBindBuffer(GL_COPY_READ_BUFFER, valBuffer->GetObj());\n        }\n        glBindBuffer(GL_COPY_WRITE_BUFFER, splatVao->GetElementBuffer()->GetObj());\n        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sortCount * sizeof(uint32_t));\n\n        GL_ERROR_CHECK(\"SplatRenderer::Sort() copy-sorted\");\n    }"
  "    // Four radix passes leave the sorted indices in valBuffer. Rendering\n    // binds it directly instead of copying the index list every sort.")
mjgs_replace_once(splat_source
  "        splatVao->Bind();\n        glDrawElements(GL_POINTS, sortCount, GL_UNSIGNED_INT, nullptr);\n        splatVao->Unbind();"
  "        splatVao->Bind();\n        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, valBuffer->GetObj());\n        const bool useMultiRadixSort = GLEW_KHR_shader_subgroup && !useRgcSortOverride;\n        if (useMultiRadixSort)\n        {\n            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, atomicCounterBuffer->GetObj());\n            glDrawElementsIndirect(GL_POINTS, GL_UNSIGNED_INT, nullptr);\n            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);\n        }\n        else\n        {\n            glDrawElements(GL_POINTS, sortCount, GL_UNSIGNED_INT, nullptr);\n        }\n        splatVao->Unbind();")
mjgs_replace_once(splat_source
  "        splatProg->SetUniform(\"projParams\", glm::vec4(0.0f, nearFar.x, nearFar.y, 0.0f));"
  "        // projParams is optimized out after removing the viewport-X workaround.")
mjgs_replace_once(splat_source
  "        splatProg->SetUniform(\"eye\", eye);"
  "        if (hasFullSH) splatProg->SetUniform(\"eye\", eye);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"r_sh0\"), gaussianCloud->GetR_SH0Attrib(), 4, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"r_sh0\"), gaussianCloud->GetR_SH0Attrib(), hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"g_sh0\"), gaussianCloud->GetG_SH0Attrib(), 4, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"g_sh0\"), gaussianCloud->GetG_SH0Attrib(), hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"b_sh0\"), gaussianCloud->GetB_SH0Attrib(), 4, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"b_sh0\"), gaussianCloud->GetB_SH0Attrib(), hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    glEnableVertexAttribArray(loc);\n}"
  "    glEnableVertexAttribArray(loc);\n}\n\n// Compact layout used by mjlab's DC-only assets. The upstream base layout\n// reserves vec4 for each color channel even though only the first coefficient\n// is populated, costing 36 bytes of vertex bandwidth per Gaussian.\nstruct MjgsDcGaussian\n{\n    float position[4];\n    float color[3];\n    float covariance[9];\n};")
mjgs_replace_once(splat_source
  "    // allocate large buffer to hold interleaved vertex data\n    gaussianDataBuffer = std::make_shared<BufferObject>(GL_ARRAY_BUFFER,\n                                                        gaussianCloud->GetRawDataPtr(),\n                                                        gaussianCloud->GetTotalSize(), 0);\n\n    const size_t numGaussians = gaussianCloud->GetNumGaussians();"
  "    const size_t numGaussians = gaussianCloud->GetNumGaussians();\n\n    if (hasFullSH)\n    {\n        gaussianDataBuffer = std::make_shared<BufferObject>(GL_ARRAY_BUFFER,\n                                                            gaussianCloud->GetRawDataPtr(),\n                                                            gaussianCloud->GetTotalSize(), 0);\n    }\n    else\n    {\n        std::vector<MjgsDcGaussian> compact(numGaussians);\n        const auto* raw = static_cast<const uint8_t*>(gaussianCloud->GetRawDataPtr());\n        const size_t sourceStride = gaussianCloud->GetStride();\n        for (size_t i = 0; i < numGaussians; ++i)\n        {\n            const void* source = raw + i * sourceStride;\n            const float* position = gaussianCloud->GetPosWithAlphaAttrib().Get<float>(source);\n            const float* red = gaussianCloud->GetR_SH0Attrib().Get<float>(source);\n            const float* green = gaussianCloud->GetG_SH0Attrib().Get<float>(source);\n            const float* blue = gaussianCloud->GetB_SH0Attrib().Get<float>(source);\n            const float* cov0 = gaussianCloud->GetCov3_Col0Attrib().Get<float>(source);\n            const float* cov1 = gaussianCloud->GetCov3_Col1Attrib().Get<float>(source);\n            const float* cov2 = gaussianCloud->GetCov3_Col2Attrib().Get<float>(source);\n            for (int j = 0; j < 4; ++j) compact[i].position[j] = position[j];\n            compact[i].color[0] = red[0];\n            compact[i].color[1] = green[0];\n            compact[i].color[2] = blue[0];\n            for (int j = 0; j < 3; ++j)\n            {\n                compact[i].covariance[j] = cov0[j];\n                compact[i].covariance[3 + j] = cov1[j];\n                compact[i].covariance[6 + j] = cov2[j];\n            }\n        }\n        gaussianDataBuffer = std::make_shared<BufferObject>(GL_ARRAY_BUFFER,\n                                                            compact.data(),\n                                                            compact.size() * sizeof(MjgsDcGaussian), 0);\n    }")
mjgs_replace_once(splat_source
  "    const size_t stride = gaussianCloud->GetStride();"
  "    const size_t stride = hasFullSH ? gaussianCloud->GetStride() : sizeof(MjgsDcGaussian);\n    const BinaryAttribute positionAttrib = hasFullSH ? gaussianCloud->GetPosWithAlphaAttrib() : BinaryAttribute(BinaryAttribute::Type::Float, 0);\n    const BinaryAttribute redAttrib = hasFullSH ? gaussianCloud->GetR_SH0Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 4 * sizeof(float));\n    const BinaryAttribute greenAttrib = hasFullSH ? gaussianCloud->GetG_SH0Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 5 * sizeof(float));\n    const BinaryAttribute blueAttrib = hasFullSH ? gaussianCloud->GetB_SH0Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 6 * sizeof(float));\n    const BinaryAttribute cov0Attrib = hasFullSH ? gaussianCloud->GetCov3_Col0Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 7 * sizeof(float));\n    const BinaryAttribute cov1Attrib = hasFullSH ? gaussianCloud->GetCov3_Col1Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 10 * sizeof(float));\n    const BinaryAttribute cov2Attrib = hasFullSH ? gaussianCloud->GetCov3_Col2Attrib() : BinaryAttribute(BinaryAttribute::Type::Float, 13 * sizeof(float));")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"position\"), gaussianCloud->GetPosWithAlphaAttrib(), 4, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"position\"), positionAttrib, 4, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"r_sh0\"), gaussianCloud->GetR_SH0Attrib(), hasFullSH ? 4 : 1, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"r_sh0\"), redAttrib, hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"g_sh0\"), gaussianCloud->GetG_SH0Attrib(), hasFullSH ? 4 : 1, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"g_sh0\"), greenAttrib, hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"b_sh0\"), gaussianCloud->GetB_SH0Attrib(), hasFullSH ? 4 : 1, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"b_sh0\"), blueAttrib, hasFullSH ? 4 : 1, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col0\"), gaussianCloud->GetCov3_Col0Attrib(), 3, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col0\"), cov0Attrib, 3, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col1\"), gaussianCloud->GetCov3_Col1Attrib(), 3, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col1\"), cov1Attrib, 3, stride);")
mjgs_replace_once(splat_source
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col2\"), gaussianCloud->GetCov3_Col2Attrib(), 3, stride);"
  "    SetupAttrib(splatProg->GetAttribLoc(\"cov3_col2\"), cov2Attrib, 3, stride);")
file(WRITE "${MJGS_GENERATED_DIR}/splatrenderer.cpp" "${splat_source}")

file(READ "${MJGS_SPLAT_ROOT}/src/splatrenderer.h" splat_header)
mjgs_replace_once(splat_header
  "    uint32_t numBlocksPerWorkgroup = 1024;"
  "    // 32 blocks x 256 lanes balances occupancy and per-group barrier cost.\n    uint32_t numBlocksPerWorkgroup = 32;")
mjgs_replace_once(splat_header
  "    bool isFramebufferSRGBEnabled;\n    bool useRgcSortOverride;"
  "    bool isFramebufferSRGBEnabled;\n    bool useRgcSortOverride;\n    bool hasFullSH;")
file(WRITE "${MJGS_GENERATED_DIR}/splatrenderer.h" "${splat_header}")

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
target_include_directories(mujoco_3dgs_splat BEFORE PUBLIC
  "$<BUILD_INTERFACE:${MJGS_GENERATED_DIR}>"
  "$<BUILD_INTERFACE:${splat_src}>")
target_include_directories(mujoco_3dgs_splat PRIVATE "${splat_src}/core")
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
