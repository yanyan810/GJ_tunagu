#pragma once
#include "Sprite.h"
#include <gdiplus.h>
#include <objidl.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

// Rasterize UTF-8 text with a private font, then render through the normal Sprite pipeline.
class TextSprite {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx, const std::string& text,
        int width, int height, float fontSize, bool centered = false) {
        static Runtime runtime;
        const std::string key = "__textsprite/" + std::to_string(width) + "/" + std::to_string(height) +
            "/" + std::to_string(fontSize) + (centered ? "/center/" : "/left/") + text;
        auto* textures = TextureManager::GetInstance();
        if (!textures->HasTexture(key)) {
            Gdiplus::PrivateFontCollection fonts;
            const auto path = std::filesystem::exists("resources/tex/font/menu.ttf")
                ? L"resources/tex/font/menu.ttf" : L"tex/font/menu.ttf";
            if (fonts.AddFontFile(path) != Gdiplus::Ok) throw std::runtime_error("TextSprite: font unavailable");
            Gdiplus::FontFamily family;
            int count = 0;
            fonts.GetFamilies(1, &family, &count);
            if (!count) throw std::runtime_error("TextSprite: font family unavailable");
            Gdiplus::Font font(&family, fontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
            Gdiplus::Graphics graphics(&bitmap);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(length, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
            Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat format;
            format.SetAlignment(centered ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
            graphics.DrawString(wide.c_str(), length, &font,
                Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), &format, &brush);
            // Encode in memory for TextureManager's WIC upload path; no generated files.
            const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            Microsoft::WRL::ComPtr<IStream> stream;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || bitmap.Save(stream.Get(), &png) != Gdiplus::Ok)
                throw std::runtime_error("TextSprite: texture encoding failed");
            HGLOBAL buffer{};
            GetHGlobalFromStream(stream.Get(), &buffer);
            STATSTG info{};
            stream->Stat(&info, STATFLAG_NONAME);
            const auto* data = static_cast<const uint8_t*>(GlobalLock(buffer));
            textures->LoadTextureFromMemory(key, data, static_cast<size_t>(info.cbSize.QuadPart));
            GlobalUnlock(buffer);
            if (!textures->HasTexture(key)) throw std::runtime_error("TextSprite: texture upload failed");
        }
        sprite_.Initialize(common, dx, key);
    }
    void Draw(float x, float y, const Matrix4x4& view, const Matrix4x4& projection) {
        sprite_.SetPosition({x, y});
        sprite_.Update(view, projection);
        sprite_.Draw();
    }
private:
    struct Runtime {
        ULONG_PTR token{};
        Runtime() { Gdiplus::GdiplusStartupInput input; if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) throw std::runtime_error("GDI+ startup failed"); }
        ~Runtime() { Gdiplus::GdiplusShutdown(token); }
    };
    Sprite sprite_;
};
