// main.cpp — entry point: istanza singola, apartment WinRT, message loop.
// Nessun render loop: fra un evento e l'altro il processo dorme qui dentro.
#include "app.h"

#include <winrt/Windows.Foundation.h>

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MiniBar.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    int code = 0;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        App app;
        if (app.Init(inst)) {
            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            code = static_cast<int>(msg.wParam);
        } else {
            code = 1;
        }
        app.Shutdown();

        winrt::uninit_apartment();
    } catch (...) {
        code = 2;
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return code;
}
