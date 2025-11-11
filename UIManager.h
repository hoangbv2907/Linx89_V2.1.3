#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <map>
#include <memory>
#include "CommonDefs.h"
#include "ModernButton.h"
#include "ToggleSwitch.h"
#include "MessageLogger.h"

class UIManager {
public:
    UIManager();
    ~UIManager();

    bool Initialize(HWND hParent);
    void CreateControls();

    // Message handling
    void AddMessage(const std::wstring& text);
    void ClearMessages();
    void UpdatePrinterStatus(const std::wstring& text);
    void UpdatePrinterUIState(PrinterState state);
    void UpdateButtonStates(PrinterState state);

    // Toggle switch
    void SetToggleState(bool state);
    bool IsToggleOn() const;

    // Data access
    std::wstring GetIPAddress() const;
    std::wstring GetInputText() const;
    int GetCountValue() const;
    bool ValidateInput() const;

    // Owner draw handling
    void HandleOwnerDraw(LPDRAWITEMSTRUCT dis);

private:
    HWND hParent_ = nullptr;

    // Controls
    HWND hIpAddress_ = nullptr;
    HWND hInputValue_ = nullptr;
    HWND hCount_ = nullptr;
    HWND hStatusDisplay_ = nullptr;
    HWND hCurrentCount_ = nullptr;
    HWND hPrinterState_ = nullptr;
    HWND hBtnUpload_ = nullptr;

    // Custom controls
    std::unique_ptr<ToggleSwitch> toggleSwitch_;
    std::unique_ptr<MessageLogger> messageLogger_;
    ModernButton btnStart_, btnPrint_, btnStop_, btnClear_, btnSet_;

    // State
    int currentCount_ = 0;
    bool isToggleOn_ = false;

    // Helper methods
    HWND CreateStatic(int x, int y, int w, int h, const wchar_t* text);
    HWND CreateButton(int x, int y, int w, int h, const wchar_t* text, int id);
    HWND CreateEdit(int x, int y, int w, int h, const wchar_t* text = L"");
    void ApplyFontToAllControls();

    // Button state management
    void UpdateButtonStateForPrinterState(PrinterState state);
};