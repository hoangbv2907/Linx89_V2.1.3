#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <memory>
#include "UIManager.h"      //Quản lý toàn bộ control trên cửa sổ (nút, text, icon…).
#include "AppController.h"  //Quản lý logic — threads — socket — đọc trạng thái máy in.
#include "MessageDef.h"     //Định nghĩa message định danh, struct trao đổi giữa threads và UI

class WindowManager {  //Khai báo lớp WindowManager
public:
    WindowManager();  //Constructor
	~WindowManager(); //Destructor

	bool Initialize(HINSTANCE hInstance); //được gọi từ main.cpp Khởi tạo WindowManager với instance handle
	bool CreateMainWindow(const std::wstring& title, int width, int height);    //Tạo cửa sổ chính với tiêu đề, chiều rộng và chiều cao
	HWND GetHwnd() const { return hwnd_; }  //Getter lấy HWND của cửa sổ chính Để main.cpp gọi ShowWindow và UpdateWindow.

    // HÀM CHÍNH của WindowManager/Message handling
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Getters
    UIManager* GetUIManager() { return uiManager_.get(); }
    AppController* GetAppController() { return appController_.get(); }

private:
	//WndProc tĩnh để đăng ký với hệ thống cửa sổ
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Message handlers
	void HandleCreate();                                        //Khởi tạo UIManager, AppController, Tạo các button, listbox, v.v.
	void HandleCommand(int id);                                 // Xử lý các lệnh từ button, menu, v.v.
    void HandleDrawItem(LPDRAWITEMSTRUCT dis);                  //Vẽ nút custom (Owner-Draw button)
    void HandleAppLog(LogMessage* msg);                         //Nhận message từ AppController thread và cập nhật UI log.
	void HandlePrinterUpdate(PrinterStateMessage* msg);         //Cập nhật trạng thái máy in trên UI.                  
    void HandleConnectionUpdate(ConnectionMessage* msg);        //Nhận thông tin connected/disconnected.
    void HandleButtonState(ButtonStateMessage* msg);            //Setter trạng thái button (active, disabled, running…)
	void HandleDestroy();                                       //Xử lý dọn dẹp khi cửa sổ bị đóng.
    // UI interaction handlers //Các handler dành cho UI//giao tiếp giữa WindowManager → AppController.
    void OnToggleClicked();
    void OnUploadClicked();
    void OnStartClicked();
    void OnPrintClicked();
    void OnStopClicked();
    void OnClearClicked();
    void OnSetClicked();

	HINSTANCE hInstance_ = nullptr;     //Instance handle của ứng dụng
    HWND hwnd_ = nullptr;               //Window chính của chương trình.
    std::unique_ptr<UIManager> uiManager_;  //Quản lý UI (nút, màu, vẽ icon…) Tạo/huỷ UI theo vòng đời window.
    std::unique_ptr<AppController> appController_; //Trung tâm logic → xử lý socket → máy in → message → UI.
};