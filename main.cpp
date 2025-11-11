#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include "WindowManager.h"
#include "Logger.h"
//thay doi luc 11h11 ng�y 11/11/2025 
#pragma comment(lib, "ws2_32.lib")

static WindowManager g_windowManager;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Initialize logger
    Logger::GetInstance().Write(L"Linx Controller starting...");

    // Initialize application
    if (!g_windowManager.Initialize(hInstance)) {
        Logger::GetInstance().Write(L"Failed to initialize WindowManager", 2);
        return -1;
    }

    // Create main window
    if (!g_windowManager.CreateMainWindow(L"Linx Controller - MVC Đa Luồng", 520, 520)) {
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return -1;
    }

    // Show window
    ShowWindow(g_windowManager.GetHwnd(), nCmdShow);
    UpdateWindow(g_windowManager.GetHwnd());

    Logger::GetInstance().Write(L"Application started successfully");

    // Main message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Logger::GetInstance().Write(L"Application shutting down");
    return (int)msg.wParam;
}
