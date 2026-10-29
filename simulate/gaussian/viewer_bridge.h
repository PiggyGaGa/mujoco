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
// Called on the active OpenGL render thread, after scene rendering and before UI.
void Render(const mjvScene& scene, mjrRect viewport);
void Shutdown();
// Mutable 0/1 state used by simulate's Show/Hide radio control.
int* HiddenState();
void ToggleVisible();
bool ExitRequested();
}
