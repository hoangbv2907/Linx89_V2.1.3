#pragma once
#include <string>
#include <windows.h>

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        out.data(), size, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
        nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
        out.data(), size);
    return out;
}
