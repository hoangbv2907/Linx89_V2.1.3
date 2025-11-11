#pragma once
#include <windows.h>
#include <string>
#include <richedit.h>

class MessageLogger {
public:
    MessageLogger();
    ~MessageLogger();

    HWND Create(HWND hParent, int x, int y, int width, int height);
    void AddMessage(const std::wstring& text);
    void Clear();
    void SetFont(HFONT hFont);

private:
    HWND hwnd_ = nullptr;
};