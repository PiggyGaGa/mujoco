#pragma once
#include <mujoco/mujoco.h>
#include <string>
namespace mjgs {
// Configure once before starting the viewer thread; removes integration CLI flags.
bool Configure(int& argc, char** argv);
// Set an already validated, world-coordinate PLY on the render thread before
// starting its loop. No OpenGL operations are performed here.
void SetWorldPly(const std::string& path);
void ValidatePly(const std::string& path);
void SetManifestVisual(const std::string& path, const double* position, const double* quaternion);
const std::string& Status();
// Draw Gaussian splats against opaque depth. The caller must choose the render stage.
void Render(const mjvScene& scene, mjrRect viewport);
// Complete classic-renderer frame: opaque/background -> splats -> transparent -> labels.
// Capture screenshots only after the complete frame, before viewer UI.
void RenderScene(mjvScene& scene, mjrRect viewport, const mjrContext* context);
void Shutdown();
// Mutable 0/1 state used by simulate's Show/Hide radio control.
int* HiddenState();
void ToggleVisible();
bool ExitRequested();
}
