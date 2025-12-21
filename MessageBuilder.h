#pragma once
#include <vector>
#include <string>

class MessageBuilder {
public:  
    std::vector<uint8_t> BuildSimpleTextField(const std::wstring& text)
    {
        std::vector<uint8_t> out;

        for (wchar_t ch : text) {
            if (ch >= 32 && ch <= 126) {
                out.push_back(static_cast<uint8_t>(ch));
            }
            else {
                out.push_back('?');
            }
        }
        return out;
    }
};