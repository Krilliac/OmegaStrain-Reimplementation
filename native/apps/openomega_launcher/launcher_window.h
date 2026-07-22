#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

namespace omega::launcher
{
// Runs the Windows-only prelaunch configuration surface. Gameplay state remains
// owned by the separate openomega executable.
[[nodiscard]] int RunLauncher(HINSTANCE instance, int show_command);
} // namespace omega::launcher
