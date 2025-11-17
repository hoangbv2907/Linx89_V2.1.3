#pragma once
#include <windows.h>
#include <string>

class ToggleSwitch {
public:
    ToggleSwitch();
    ~ToggleSwitch();

	// Tạo control Win32
    HWND Create(HWND hParent, int x, int y, int width, int height, int id);
	// Vẽ công tắc
    void Draw(LPDRAWITEMSTRUCT dis);
	// Chuyển đổi trạng thái
    void Toggle();
	// Đặt trạng thái cụ thể
    void SetState(bool state);
	// Lấy trạng thái hiện tại
    bool IsOn() const { return isOn_; }

private:
	HWND hwnd_ = nullptr;   // Handle của control
	bool isOn_ = false; // Trạng thái công tắc
};