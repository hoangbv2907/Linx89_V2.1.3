#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ColorScheme {
public:
    static ColorScheme& GetInstance() {
        static ColorScheme instance;
        return instance;
    }

    struct ButtonColors {
        COLORREF normal = RGB(220, 220, 220);
        COLORREF hover = RGB(240, 240, 240);
        COLORREF pressed = RGB(190, 190, 190);
        COLORREF disabled = RGB(240, 240, 240);
        COLORREF highlighted = RGB(255, 230, 150);
        COLORREF text = RGB(30, 30, 30);
        COLORREF disabledText = RGB(150, 150, 150);
    };

    ButtonColors GetButtonColors() const {
        return defaultButtonColors_;
    }

    COLORREF GetToggleOnColor() const { return RGB(0, 150, 255); }
    COLORREF GetToggleOffColor() const { return RGB(180, 180, 180); }
    COLORREF GetToggleKnobColor() const { return RGB(255, 255, 255); }

private:
    ColorScheme() = default;
    ButtonColors defaultButtonColors_;
};