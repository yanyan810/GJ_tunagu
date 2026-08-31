#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

struct GameWindowCaptureResult {
    HWND window = nullptr;
    std::wstring windowTitle;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<unsigned char> pngBytes;
    std::string error;
};

bool CaptureGameProcessWindow(
    DWORD processId,
    unsigned int maximumWidth,
    GameWindowCaptureResult& result,
    HWND windowToHide = nullptr);

std::string Base64Encode(const std::vector<unsigned char>& bytes);
