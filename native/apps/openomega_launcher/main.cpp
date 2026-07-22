#include "launcher_window.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line,
                    const int show_command)
{
    (void)previous_instance;
    (void)command_line;

    (void)::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT com_result =
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com_result))
    {
        ::MessageBoxW(nullptr, L"OpenOmega could not initialize the Windows launcher services.",
                      L"OpenOmega Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }

    int result = 1;
    try
    {
        result = omega::launcher::RunLauncher(instance, show_command);
    }
    catch (...)
    {
        ::MessageBoxW(nullptr, L"OpenOmega encountered an unexpected launcher error.",
                      L"OpenOmega Launcher", MB_OK | MB_ICONERROR);
    }
    ::CoUninitialize();
    return result;
}
