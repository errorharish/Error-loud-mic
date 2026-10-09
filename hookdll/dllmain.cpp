#include <windows.h>
#include "hook.h"

static HMODULE g_self = nullptr;

static DWORD WINAPI Main(LPVOID) {
    if (InstallHook()) RunGui();   // gui крутится пока не закроют крестиком

    UninstallHook();
    // ждём пока никто не выполняется внутри хука, потом выгружаемся целиком
    for (int i = 0; i < 400; ++i) {
        if (InterlockedCompareExchange(&g_inflight, 0, 0) <= 0) break;
        Sleep(5);
    }
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_self = h;
        HANDLE th = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
