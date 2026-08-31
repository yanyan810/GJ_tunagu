#pragma once
#include <d3d12.h>
#include <cstdint>


#ifdef USE_IMGUI

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

#endif // USE_IMGUI


class WinApp;
class DirectXCommon;
class SrvManager;

class ImGuiManagaer {
public:
    void Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);
    void SetSceneTexture(uint32_t srvIndex);
    void SetPreviewTexture(uint32_t srvIndex);
    void Begin();
    void End(ID3D12GraphicsCommandList* cmd);
    void Shutdown();

private:
    void BeginDockSpace_();
#ifdef USE_IMGUI
    void BuildDefaultDockLayout_(ImGuiID dockspaceId);
#endif // USE_IMGUI
    void DrawEditorPanels_();

    WinApp* winApp_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    bool initialized_ = false;
    int selectedParticleItem_ = 0;
    uint32_t sceneSrvIndex_ = 0;
    uint32_t previewSrvIndex_ = 0;
    bool hasSceneTexture_ = false;
    bool hasPreviewTexture_ = false;
    uint32_t imguiSrvIndex_ = 0; // SrvManager が 0番をImGui予約してる前提
};
