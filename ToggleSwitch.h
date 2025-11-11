#pragma once
#include <windows.h>
#include <string>

class ToggleSwitch {
public:
    ToggleSwitch();
    ~ToggleSwitch();

    HWND Create(HWND hParent, int x, int y, int width, int height, int id);
    void Draw(LPDRAWITEMSTRUCT dis);
    void Toggle();
    void SetState(bool state);
    bool IsOn() const { return isOn_; }

private:
    HWND hwnd_ = nullptr;
    bool isOn_ = false;
};