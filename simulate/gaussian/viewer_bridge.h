#pragma once
#include <mujoco/mujoco.h>
namespace mjgs {
// Configure once before starting the viewer thread; removes integration CLI flags.
bool Configure(int& argc, char** argv);
// Called on the active OpenGL render thread, after scene rendering and before UI.
void Render(const mjvScene& scene, mjrRect viewport);
void Shutdown();
// Mutable 0/1 state used by simulate's Show/Hide radio control.
int* HiddenState();
void ToggleVisible();
bool ExitRequested();
}
