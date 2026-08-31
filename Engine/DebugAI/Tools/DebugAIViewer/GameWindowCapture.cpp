#include "GameWindowCapture.h"

#include <wincodec.h>

#include <algorithm>
#include <cstdint>

namespace {

struct WindowSearch {
    DWORD processId = 0;
    HWND bestWindow = nullptr;
    std::uint64_t bestArea = 0;
};

BOOL CALLBACK FindLargestProcessWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window) || IsIconic(window)) {
        return TRUE;
    }
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const auto width = static_cast<std::uint64_t>((std::max)(0L, client.right - client.left));
    const auto height = static_cast<std::uint64_t>((std::max)(0L, client.bottom - client.top));
    const std::uint64_t area = width * height;
    if (area > search->bestArea) {
        search->bestArea = area;
        search->bestWindow = window;
    }
    return TRUE;
}

bool EncodePng(
    const unsigned char* pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    std::vector<unsigned char>& output,
    std::string& error) {
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = initializeResult == S_OK || initializeResult == S_FALSE;
    IWICImagingFactory* factory = nullptr;
    IStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    bool ok = false;

    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) {
        result = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    }
    if (SUCCEEDED(result)) {
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(result)) {
        result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(result)) {
        result = encoder->CreateNewFrame(&frame, &properties);
    }
    if (SUCCEEDED(result)) {
        result = frame->Initialize(properties);
    }
    if (SUCCEEDED(result)) {
        result = frame->SetSize(width, height);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) {
        result = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA) {
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (SUCCEEDED(result)) {
        result = frame->WritePixels(
            height,
            stride,
            stride * height,
            const_cast<BYTE*>(pixels));
    }
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    if (SUCCEEDED(result)) {
        HGLOBAL memory = nullptr;
        result = GetHGlobalFromStream(stream, &memory);
        if (SUCCEEDED(result) && memory) {
            const SIZE_T size = GlobalSize(memory);
            const void* data = GlobalLock(memory);
            if (data && size > 0) {
                const auto* begin = static_cast<const unsigned char*>(data);
                output.assign(begin, begin + size);
                ok = true;
            }
            if (data) GlobalUnlock(memory);
        }
    }
    if (!ok) {
        error = "PNG encoding failed (HRESULT " +
            std::to_string(static_cast<unsigned long>(result)) + ").";
    }
    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();
    return ok;
}

} // namespace

bool CaptureGameProcessWindow(
    DWORD processId,
    unsigned int maximumWidth,
    GameWindowCaptureResult& result,
    HWND windowToHide) {
    result = {};
    if (processId == 0) {
        result.error = "The connected game process ID is not available yet.";
        return false;
    }

    WindowSearch search{ processId };
    EnumWindows(FindLargestProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.bestWindow) {
        result.error = "No visible, non-minimized game window was found.";
        return false;
    }

    RECT client{};
    if (!GetClientRect(search.bestWindow, &client)) {
        result.error = "The game client area could not be measured.";
        return false;
    }
    const int sourceWidth = client.right - client.left;
    const int sourceHeight = client.bottom - client.top;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        result.error = "The game client area is empty.";
        return false;
    }

    maximumWidth = (std::clamp)(maximumWidth, 256u, 1280u);
    const unsigned int targetWidth = (std::min)(
        static_cast<unsigned int>(sourceWidth), maximumWidth);
    const unsigned int targetHeight = (std::max)(
        1u,
        static_cast<unsigned int>(
            static_cast<std::uint64_t>(sourceHeight) * targetWidth /
            static_cast<unsigned int>(sourceWidth)));

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(targetWidth);
    info.bmiHeader.biHeight = -static_cast<LONG>(targetHeight);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    WINDOWPLACEMENT hiddenWindowPlacement{};
    hiddenWindowPlacement.length = sizeof(hiddenWindowPlacement);
    const bool hideWindow = windowToHide && windowToHide != search.bestWindow &&
        IsWindowVisible(windowToHide) &&
        GetWindowPlacement(windowToHide, &hiddenWindowPlacement);
    if (hideWindow) {
        ShowWindow(windowToHide, SW_HIDE);
        Sleep(80);
    }

    HDC source = GetDC(search.bestWindow);
    HDC target = source ? CreateCompatibleDC(source) : nullptr;
    HDC printedSource = source ? CreateCompatibleDC(source) : nullptr;
    void* pixels = nullptr;
    HBITMAP bitmap = target
        ? CreateDIBSection(target, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)
        : nullptr;
    HGDIOBJ previousTarget = bitmap ? SelectObject(target, bitmap) : nullptr;

    BITMAPINFO sourceInfo = info;
    sourceInfo.bmiHeader.biWidth = sourceWidth;
    sourceInfo.bmiHeader.biHeight = -sourceHeight;
    void* printedPixels = nullptr;
    HBITMAP printedBitmap = printedSource
        ? CreateDIBSection(
            printedSource,
            &sourceInfo,
            DIB_RGB_COLORS,
            &printedPixels,
            nullptr,
            0)
        : nullptr;
    HGDIOBJ previousPrinted = printedBitmap
        ? SelectObject(printedSource, printedBitmap) : nullptr;
    bool printed = false;
    if (printedSource && printedBitmap && printedPixels) {
        constexpr UINT kPrintClientAndFullContent = PW_CLIENTONLY | 0x00000002;
        printed = PrintWindow(
            search.bestWindow,
            printedSource,
            kPrintClientAndFullContent) != FALSE;
        if (printed) {
            const auto* printedBytes = static_cast<const unsigned char*>(printedPixels);
            const std::size_t printedByteCount =
                static_cast<std::size_t>(sourceWidth) * sourceHeight * 4;
            printed = std::any_of(
                printedBytes,
                printedBytes + printedByteCount,
                [](unsigned char value) { return value != 0; });
        }
    }

    bool copied = false;
    if (source && target && bitmap && pixels) {
        SetStretchBltMode(target, HALFTONE);
        SetBrushOrgEx(target, 0, 0, nullptr);
        HDC captureSource = printed ? printedSource : source;
        copied = StretchBlt(
            target,
            0,
            0,
            static_cast<int>(targetWidth),
            static_cast<int>(targetHeight),
            captureSource,
            0,
            0,
            sourceWidth,
            sourceHeight,
            SRCCOPY | CAPTUREBLT) != FALSE;
    }

    if (!copied) {
        result.error = "The game window pixels could not be captured.";
    } else {
        auto* bytes = static_cast<unsigned char*>(pixels);
        const std::size_t byteCount =
            static_cast<std::size_t>(targetWidth) * targetHeight * 4;
        const bool anyVisiblePixel = std::any_of(
            bytes,
            bytes + byteCount,
            [](unsigned char value) { return value != 0; });
        if (!anyVisiblePixel) {
            result.error =
                "The captured image was completely black. Keep the game window visible and not minimized.";
        } else {
            for (std::size_t index = 3; index < byteCount; index += 4) {
                bytes[index] = 255;
            }
            result.window = search.bestWindow;
            result.width = targetWidth;
            result.height = targetHeight;
            const int titleLength = GetWindowTextLengthW(search.bestWindow);
            if (titleLength > 0) {
                result.windowTitle.resize(static_cast<std::size_t>(titleLength) + 1);
                const int copiedTitle = GetWindowTextW(
                    search.bestWindow,
                    result.windowTitle.data(),
                    titleLength + 1);
                result.windowTitle.resize((std::max)(0, copiedTitle));
            }
            EncodePng(
                bytes,
                targetWidth,
                targetHeight,
                targetWidth * 4,
                result.pngBytes,
                result.error);
        }
    }

    if (previousPrinted) SelectObject(printedSource, previousPrinted);
    if (printedBitmap) DeleteObject(printedBitmap);
    if (printedSource) DeleteDC(printedSource);
    if (previousTarget) SelectObject(target, previousTarget);
    if (bitmap) DeleteObject(bitmap);
    if (target) DeleteDC(target);
    if (source) ReleaseDC(search.bestWindow, source);
    if (hideWindow) {
        const int restoreCommand = hiddenWindowPlacement.showCmd == SW_SHOWMAXIMIZED
            ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE;
        ShowWindow(windowToHide, restoreCommand);
    }
    return !result.pngBytes.empty();
}

std::string Base64Encode(const std::vector<unsigned char>& bytes) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t value = (first << 16) | (second << 8) | third;
        result.push_back(table[(value >> 18) & 0x3f]);
        result.push_back(table[(value >> 12) & 0x3f]);
        result.push_back(index + 1 < bytes.size() ? table[(value >> 6) & 0x3f] : '=');
        result.push_back(index + 2 < bytes.size() ? table[value & 0x3f] : '=');
    }
    return result;
}
