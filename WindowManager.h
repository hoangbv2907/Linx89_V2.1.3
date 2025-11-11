#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <memory>
#include "UIManager.h"
#include "AppController.h"
#include "MessageDef.h"

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    bool Initialize(HINSTANCE hInstance);
    bool CreateMainWindow(const std::wstring& title, int width, int height);
    HWND GetHwnd() const { return hwnd_; }

    // Message handling
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Getters
    UIManager* GetUIManager() { return uiManager_.get(); }
    AppController* GetAppController() { return appController_.get(); }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Message handlers
    void HandleCreate();
    void HandleCommand(int id);
    void HandleDrawItem(LPDRAWITEMSTRUCT dis);
    void HandleAppLog(LogMessage* msg);
    void HandlePrinterUpdate(PrinterStateMessage* msg);
    void HandleConnectionUpdate(ConnectionMessage* msg);
    void HandleButtonState(ButtonStateMessage* msg);
    void HandleDestroy();
    // UI interaction handlers
    void OnToggleClicked();
    void OnUploadClicked();
    void OnStartClicked();
    void OnPrintClicked();
    void OnStopClicked();
    void OnClearClicked();
    void OnSetClicked();

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::unique_ptr<UIManager> uiManager_;
    std::unique_ptr<AppController> appController_;
};