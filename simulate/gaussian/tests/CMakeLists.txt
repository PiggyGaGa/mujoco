add_executable(mjgs_asset_test
  asset_test.cc
  ../scene_config.cc
  ../ply_validation.cc)
target_include_directories(mjgs_asset_test PRIVATE ..)
target_link_libraries(mjgs_asset_test PRIVATE glm::glm)
add_test(NAME mjgs_asset_test COMMAND mjgs_asset_test)

find_package(Threads REQUIRED)
add_executable(mjgs_lifecycle_test lifecycle_test.cc)
target_link_libraries(mjgs_lifecycle_test PRIVATE mujoco_3dgs_renderer Threads::Threads)
add_test(NAME mjgs_lifecycle_test COMMAND mjgs_lifecycle_test)

# Explicit opt-in GPU/window test; not part of display-free ctest.
add_executable(mjgs_render_test render_test.cc)
target_link_libraries(mjgs_render_test PRIVATE
  mujoco_3dgs_renderer GLEW::GLEW glfw)

# Like mjgs_render_test, this explicitly requires a desktop GL context.
add_executable(mjgs_transparency_test transparency_test.cc)
target_link_libraries(mjgs_transparency_test PRIVATE
  mujoco_3dgs_renderer GLEW::GLEW glfw)
