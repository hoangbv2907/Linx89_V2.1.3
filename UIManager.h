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

    //Khởi tạo và tạo giao diện
    bool Initialize(HWND hParent);  //Lưu lại handle của window chính (hParent_), Tạo logger & toggle switch, Chuẩn bị cho việc tạo UI
	void CreateControls();  //Tạo các control trên giao diện

    // Message handling
    void AddMessage(const std::wstring& text); //Thêm 1 dòng text vào MessageLogger
	void ClearMessages();   //Xóa hết các dòng trong MessageLogger
    void UpdatePrinterStatus(const std::wstring& text); //Hiển thị trạng thái máy in lên hPrinterState_
    void UpdatePrinterUIState(PrinterState state);  //Thay đổi giao diện tùy theo trạng thái máy in
    void UpdateButtonStates(PrinterState state);    //Wrap lại logic set trạng thái của từng nút

    // Toggle switch
    void SetToggleState(bool state);    //Set ON/OFF cho công tắc giao diện
	bool IsToggleOn() const;    //Lấy trạng thái ON/OFF của công tắc giao diện

    // Data access
	std::wstring GetIPAddress() const;  //Lấy giá trị user nhập trong ô IP Address
	std::wstring GetInputText() const;  //Lấy nội dung nhập trong edit box tùy mục đích
    int GetCountValue() const;  //Lấy số lượng in user nhập
	bool ValidateInput() const; //Kiểm tra tính hợp lệ của dữ liệu nhập vào (IP Address và Count)

    // Owner draw handling
	void HandleOwnerDraw(LPDRAWITEMSTRUCT dis); //Xử lý vẽ lại các control owner-draw (nút bấm và toggle switch)

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