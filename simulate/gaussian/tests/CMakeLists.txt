add_executable(mjgs_asset_test
  asset_test.cc
  ../scene_config.cc
  ../ply_validation.cc)
target_include_directories(mjgs_asset_test PRIVATE ..)
target_link_libraries(mjgs_asset_test PRIVATE glm::glm)
add_test(NAME mjgs_asset_test COMMAND mjgs_asset_test)

# Explicit opt-in GPU/window test; not part of display-free ctest.
add_executable(mjgs_render_test render_test.cc)
target_link_libraries(mjgs_render_test PRIVATE
  mujoco_3dgs_renderer GLEW::GLEW glfw)
