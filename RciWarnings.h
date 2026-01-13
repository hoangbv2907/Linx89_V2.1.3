#pragma once
#include <string>
#include <vector>
#include <cstdint>

std::wstring JoinList(const std::vector<std::wstring>& v, const wchar_t* sep = L", ");
std::wstring DecodePStatus(uint8_t p);
std::wstring DecodeCStatus(uint8_t c);
std::vector<std::wstring> DecodeWarningMask(uint32_t mask);

