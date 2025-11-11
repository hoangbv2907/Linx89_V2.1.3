#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>

class FontManager {
public:
    static FontManager& GetInstance() {
        static FontManager instance;
        return instance;
    }

    HFONT GetDefaultFont() {
        if (!hDefaultFont_) CreateFonts();
        return hDefaultFont_.get();  // ✅ .get() trả về raw handle
    }

    HFONT GetToggleFont() {
        if (!hToggleFont_) CreateFonts();
        return hToggleFont_.get();   // ✅ .get() trả về raw handle
    }

    void Cleanup() {
        hDefaultFont_.reset();
        hToggleFont_.reset();
    }

private:
    FontManager() = default;

    // Custom deleter cho HFONT
    struct FontDeleter {
        void operator()(HFONT font) const {
            if (font) DeleteObject(font);
        }
    };

    using UniqueHFONT = std::unique_ptr<std::remove_pointer<HFONT>::type, FontDeleter>;

    void CreateFonts() {
        HDC hdc = GetDC(NULL);
        int defaultSize = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        int toggleSize = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);

        // ✅ Tạo unique_ptr với custom deleter
        hDefaultFont_ = UniqueHFONT(CreateFontW(defaultSize, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"));

        hToggleFont_ = UniqueHFONT(CreateFontW(toggleSize, 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"));
    }

    // ✅ Member variables là unique_ptr
    UniqueHFONT hDefaultFont_;
    UniqueHFONT hToggleFont_;
};