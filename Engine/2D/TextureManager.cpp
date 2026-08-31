#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <Windows.h>

TextureManager* TextureManager::instance = nullptr;
using namespace StringUtility;

static void DebugPrintA(const std::string& s) {
    OutputDebugStringA((s + "\n").c_str());
}

TextureManager* TextureManager::GetInstance()
{
    if (instance == nullptr) {
        instance = new TextureManager();
    }
    return instance;
}

void TextureManager::Finalize()
{
    delete instance;
    instance = nullptr;
}

void TextureManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dx_ = dxCommon;
    srvManager_ = srvManager;

    // ---- 1x1 白テクスチャを必ず作る（filePath="" のとき用）----
    {
        DirectX::ScratchImage whiteImg{};
        HRESULT hr = whiteImg.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            1, 1, 1, 1
        );
        assert(SUCCEEDED(hr));

        auto* img = whiteImg.GetImage(0, 0, 0);
        assert(img && img->pixels && img->rowPitch >= 4);

        img->pixels[0] = 255; // R
        img->pixels[1] = 255; // G
        img->pixels[2] = 255; // B
        img->pixels[3] = 255; // A

        TextureData& tex = textureDatas_[kWhiteKey];
        tex.metadata = whiteImg.GetMetadata();
        tex.resource = dx_->CreateTextureResource(tex.metadata);
        dx_->UploadTextureData(tex.resource, whiteImg);

        tex.srvIndex = srvManager_->Allocate();
        tex.srvHandleCPU = srvManager_->GetCPUDescriptionHandle(tex.srvIndex);
        tex.srvHandleGPU = srvManager_->GetGPUDescriptionHandle(tex.srvIndex);

        srvManager_->CreateSRVTexture2D(
            tex.srvIndex,
            tex.resource.Get(),
            tex.metadata.format,
            1
        );
    }
}

void TextureManager::LoadTexture(const std::string& filePath)
{
    if (filePath.empty()) {
        return;
    }

    if (textureDatas_.contains(filePath)) {
        return;
    }

    std::string resolvedPath = filePath;
    std::filesystem::path resolvedPathW(ConvertString(resolvedPath));
    if (!std::filesystem::exists(resolvedPathW)) {
        resolvedPath = "resources/" + filePath;
        resolvedPathW = std::filesystem::path(ConvertString(resolvedPath));
        if (!std::filesystem::exists(resolvedPathW)) {
            DebugPrintA("[Texture] file not found: " + filePath + " (tried " + resolvedPath + ")");
            return;
        }
    }

    DirectX::ScratchImage image{};
    std::wstring filePathW = resolvedPathW.wstring();

    HRESULT hr = S_OK;

    // 拡張子で分岐。PMXではTGAテクスチャがよく使われるため、
    // WICではなくDirectXTexのTGAローダーへ明示的に振り分ける。
    std::string extension = ConvertString(resolvedPathW.extension().wstring());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".dds") {
        hr = DirectX::LoadFromDDSFile(
            filePathW.c_str(),
            DirectX::DDS_FLAGS_NONE,
            nullptr,
            image
        );
    }
    else if (extension == ".tga") {
        hr = DirectX::LoadFromTGAFile(
            filePathW.c_str(),
            nullptr,
            image
        );
    }
    else {
        hr = DirectX::LoadFromWICFile(
            filePathW.c_str(),
            DirectX::WIC_FLAGS_FORCE_SRGB,
            nullptr,
            image
        );
    }
    assert(SUCCEEDED(hr));

    DirectX::ScratchImage mipImages{};

    // 圧縮DDSはGenerateMipMapsが失敗しやすいのでそのまま使う
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        mipImages = std::move(image);
    }
    else {
        hr = DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            image.GetMetadata(),
            DirectX::TEX_FILTER_SRGB,
            0,
            mipImages
        );
        if (FAILED(hr)) {
            mipImages = std::move(image);
        }
    }

    TextureData& tex = textureDatas_[filePath];

    tex.metadata = mipImages.GetMetadata();
    tex.resource = dx_->CreateTextureResource(tex.metadata);

    dx_->UploadTextureData(tex.resource, mipImages);

    tex.srvIndex = srvManager_->Allocate();
    tex.srvHandleCPU = srvManager_->GetCPUDescriptionHandle(tex.srvIndex);
    tex.srvHandleGPU = srvManager_->GetGPUDescriptionHandle(tex.srvIndex);

    // cubemap かどうかで SRV を分ける
    if (tex.metadata.IsCubemap()) {
        srvManager_->CreateSRVTextureCube(
            tex.srvIndex,
            tex.resource.Get(),
            tex.metadata.format,
            static_cast<UINT>(tex.metadata.mipLevels)
        );
    }
    else {
        srvManager_->CreateSRVTexture2D(
            tex.srvIndex,
            tex.resource.Get(),
            tex.metadata.format,
            static_cast<UINT>(tex.metadata.mipLevels)
        );
    }
}


// TextureManager.cpp
bool TextureManager::HasTexture(const std::string& key) const {
    return textureDatas_.contains(key);
}

void TextureManager::LoadTextureFromMemory(const std::string& key, const uint8_t* data, size_t sizeBytes)
{
    if (key.empty()) return;
    if (textureDatas_.contains(key)) return;

    DirectX::ScratchImage image{};
    HRESULT hr = DirectX::LoadFromWICMemory(
        data, sizeBytes,
        DirectX::WIC_FLAGS_FORCE_SRGB,
        nullptr,
        image
    );
    if (FAILED(hr)) {
        OutputDebugStringA(("[Texture] LoadFromWICMemory failed: " + key + "\n").c_str());
        return;
    }

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(), image.GetImageCount(), image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB, 0, mipImages
    );
    if (FAILED(hr)) mipImages = std::move(image);

    TextureData& tex = textureDatas_[key];
    tex.metadata = mipImages.GetMetadata();
    tex.resource = dx_->CreateTextureResource(tex.metadata);
    dx_->UploadTextureData(tex.resource, mipImages);

    tex.srvIndex = srvManager_->Allocate();
    tex.srvHandleCPU = srvManager_->GetCPUDescriptionHandle(tex.srvIndex);
    tex.srvHandleGPU = srvManager_->GetGPUDescriptionHandle(tex.srvIndex);

    srvManager_->CreateSRVTexture2D(
        tex.srvIndex,
        tex.resource.Get(),
        tex.metadata.format,
        (UINT)tex.metadata.mipLevels
    );
}


const TextureManager::TextureData&
TextureManager::GetDataByPathOrWhite_(const std::string& filePath) const
{
    if (filePath.empty()) {
        return textureDatas_.at(kWhiteKey);
    }
    auto it = textureDatas_.find(filePath);
    if (it == textureDatas_.end()) {
        return textureDatas_.at(kWhiteKey);
    }
    return it->second;
}

TextureManager::TextureData&
TextureManager::GetDataByPathOrWhite_(const std::string& filePath)
{
    if (filePath.empty()) {
        return textureDatas_.at(kWhiteKey);
    }
    auto it = textureDatas_.find(filePath);
    if (it == textureDatas_.end()) {
        return textureDatas_.at(kWhiteKey);
    }
    return it->second;
}

uint32_t TextureManager::GetSrvIndex(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).metadata;
}

ID3D12DescriptorHeap* TextureManager::GetSrvDescriptorHeap() const
{
    assert(srvManager_);
    return srvManager_->GetDescriptorHeap();
}
