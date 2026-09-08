#include "MineBloom.h"

#include "DirectXCommon.h"
#include "SceneColorFormat.h"
#include "SrvManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kMask = 0, kNearA = 1, kNearB = 2, kWideA = 3, kWideB = 4;
constexpr UINT kTargetCount = 5;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}

struct PassConstants {
    float texelX, texelY;
    float directionX, directionY;
    float nearWeight, wideWeight;
    uint32_t mode;
    float padding;
};
static_assert(sizeof(PassConstants) == 32);

struct Target {
    ComPtr<ID3D12Resource> resource;
    UINT width = 0, height = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
}

struct MineBloom::Impl {
    DirectXCommon* dx = nullptr;
    SrvManager* srv = nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> filterPso, compositePso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
    ComPtr<ID3D12Resource> scene;
    std::array<Target, kTargetCount> targets;
    UINT rtvStride = 0, srvStride = 0;
    UINT width = 0, height = 0;
    bool active = false;

    void CreatePipeline();
    void EnsureTargets(UINT newWidth, UINT newHeight);
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Srv(UINT index) const;
    void Transition(UINT index, D3D12_RESOURCE_STATES next);
    void Viewport(UINT viewportWidth, UINT viewportHeight) const;
    void Filter(UINT source, UINT destination, uint32_t mode, float x, float y);
    void Restore();
};

MineBloom::MineBloom() : impl_(std::make_unique<Impl>()) {}
MineBloom::~MineBloom() = default;

void MineBloom::Initialize(DirectXCommon* dx, SrvManager* srv) {
    if (!dx || !srv) throw std::invalid_argument("Mine bloom needs a device and shared SRV heap");
    auto& e = *impl_;
    e.dx = dx;
    e.srv = srv;
    e.CreatePipeline();
}

D3D12_CPU_DESCRIPTOR_HANDLE MineBloom::Impl::Rtv(UINT index) const {
    auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtvStride;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE MineBloom::Impl::Srv(UINT index) const {
    auto handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * srvStride;
    return handle;
}

void MineBloom::Impl::CreatePipeline() {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = { 0, 0, sizeof(PassConstants) / sizeof(uint32_t) };
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    for (UINT i = 0; i < 2; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i + 1].DescriptorTable = { 1, &ranges[i] };
        parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 3;
    signature.pParameters = parameters;
    signature.NumStaticSamplers = 1;
    signature.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> blob, error;
    const HRESULT serialized = D3D12SerializeRootSignature(
        &signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
    Check(serialized, "Serialize Mine bloom root signature");
    Check(dx->GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(),
        blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Create Mine bloom root signature");

    const auto vs = dx->CompilesSharder(L"resources/shaders/MineBloom.VS.hlsl", L"vs_6_0");
    const auto ps = dx->CompilesSharder(L"resources/shaders/MineBloom.PS.hlsl", L"ps_6_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root.Get();
    pipeline.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pipeline.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = FALSE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    auto& blend = pipeline.BlendState.RenderTarget[0];
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = kSceneColorFormat;
    pipeline.SampleDesc.Count = 1;
    pipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pipeline,
        IID_PPV_ARGS(&filterPso)), "Create Mine bloom filter PSO");
    blend.BlendEnable = TRUE;
    blend.DestBlend = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    // Bloom changes HDR radiance only; keep the existing scene alpha intact.
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED |
        D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pipeline,
        IID_PPV_ARGS(&compositePso)), "Create Mine bloom composite PSO");
}

void MineBloom::Impl::EnsureTargets(UINT newWidth, UINT newHeight) {
    if (width == newWidth && height == newHeight && rtvHeap && srvHeap) return;
    if (!rtvHeap) {
        rtvHeap = dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kTargetCount + 1, false);
        srvHeap = dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kTargetCount, true);
        rtvStride = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        srvStride = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    std::array<Target, kTargetCount> replacement;
    const wchar_t* names[kTargetCount] = {
        L"Mine emission mask", L"Mine bloom quarter A", L"Mine bloom quarter B",
        L"Mine bloom eighth A", L"Mine bloom eighth B"
    };
    for (UINT i = 0; i < kTargetCount; ++i) {
        const UINT divisor = i == kMask ? 1 : (i <= kNearB ? 4 : 8);
        auto& target = replacement[i];
        target.width = (newWidth + divisor - 1) / divisor;
        target.height = (newHeight + divisor - 1) / divisor;
        target.resource = dx->CreateRenderTextureResource(
            target.width, target.height, kSceneColorFormat, { 0, 0, 0, 0 });
        target.resource->SetName(names[i]);
    }
    // The engine fences every completed frame. Replacing these per-instance
    // targets at the next Begin is safe under the same lifetime contract as
    // MineEffects' HDR/depth snapshots.
    targets = std::move(replacement);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = kSceneColorFormat;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kSceneColorFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    auto handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kTargetCount; ++i) {
        dx->GetDevice()->CreateRenderTargetView(targets[i].resource.Get(), &rtv, Rtv(i + 1));
        dx->GetDevice()->CreateShaderResourceView(targets[i].resource.Get(), &view, handle);
        handle.ptr += srvStride;
    }
    width = newWidth;
    height = newHeight;
}

void MineBloom::Impl::Transition(UINT index, D3D12_RESOURCE_STATES next) {
    auto& target = targets[index];
    if (target.state == next) return;
    dx->TransitionResource(target.resource.Get(), target.state, next);
    target.state = next;
}

void MineBloom::Impl::Viewport(UINT viewportWidth, UINT viewportHeight) const {
    const D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(viewportWidth),
        static_cast<float>(viewportHeight), 0, 1 };
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight) };
    dx->GetCommandList()->RSSetViewports(1, &viewport);
    dx->GetCommandList()->RSSetScissorRects(1, &scissor);
}

bool MineBloom::Begin(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e = *impl_;
    if (!e.dx || !e.srv || e.active || !sceneColor || !sceneDepth ||
        sceneDepth != e.dx->GetDepthStencilResource()) return false;
    const auto color = sceneColor->GetDesc(), depth = sceneDepth->GetDesc();
    if (color.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depth.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        color.Format != kSceneColorFormat || depth.Format != DXGI_FORMAT_R32_TYPELESS ||
        color.Width == 0 || color.Height == 0 ||
        color.Width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        color.Height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        color.Width != depth.Width || color.Height != depth.Height ||
        color.DepthOrArraySize != 1 || depth.DepthOrArraySize != 1 ||
        color.MipLevels != 1 || depth.MipLevels != 1 ||
        color.SampleDesc.Count != 1 || depth.SampleDesc.Count != 1 ||
        !(color.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) return false;
    ComPtr<ID3D12Device> owner;
    if (FAILED(sceneColor->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != e.dx->GetDevice()) return false;

    e.EnsureTargets(static_cast<UINT>(color.Width), color.Height);
    if (e.scene.Get() != sceneColor) {
        e.scene = sceneColor;
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = kSceneColorFormat;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        e.dx->GetDevice()->CreateRenderTargetView(sceneColor, &rtv, e.Rtv(0));
    }
    e.Transition(kMask, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto dsv = e.dx->GetDSVCPUDescriptorHandle(0);
    const D3D12_CPU_DESCRIPTOR_HANDLE handles[]{ e.Rtv(0), e.Rtv(kMask + 1) };
    const float clear[]{ 0, 0, 0, 0 };
    auto* cmd = e.dx->GetCommandList();
    cmd->ClearRenderTargetView(handles[1], clear, 0, nullptr);
    cmd->OMSetRenderTargets(2, handles, FALSE, &dsv);
    e.Viewport(e.width, e.height);
    e.active = true;
    return true;
}

void MineBloom::Impl::Filter(UINT source, UINT destination, uint32_t mode, float x, float y) {
    auto* cmd = dx->GetCommandList();
    // Explicitly release the previous RTV before using it as an SRV.
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    Transition(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(destination, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto handle = Rtv(destination + 1);
    cmd->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    const auto& src = targets[source];
    const auto& dst = targets[destination];
    Viewport(dst.width, dst.height);
    const PassConstants constants{ 1.0f / src.width, 1.0f / src.height, x, y, 0, 0, mode, 0 };
    // Root constants are captured in each draw command, unlike repeatedly
    // overwriting one mapped CB that all queued blur passes would then share.
    cmd->SetGraphicsRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    cmd->SetGraphicsRootDescriptorTable(1, Srv(source));
    cmd->SetGraphicsRootDescriptorTable(2, Srv(source));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void MineBloom::Impl::Restore() {
    const auto sceneRtv = Rtv(0);
    const auto dsv = dx->GetDSVCPUDescriptorHandle(0);
    dx->GetCommandList()->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
    Viewport(width, height);
    srv->PreDraw();
    active = false;
}

void MineBloom::Composite(float strength) {
    auto& e = *impl_;
    if (!e.active) return;
    if (!std::isfinite(strength) || strength <= 0.0f) {
        e.Restore();
        return;
    }
    strength = std::min(strength, 4.0f);
    auto* cmd = e.dx->GetCommandList();
    ID3D12DescriptorHeap* heaps[]{ e.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(e.root.Get());
    cmd->SetPipelineState(e.filterPso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 0, nullptr);
    cmd->IASetIndexBuffer(nullptr);
    e.Filter(kMask, kNearA, 0, 0, 0);
    e.Filter(kNearA, kNearB, 1, 1.75f, 0);
    e.Filter(kNearB, kNearA, 1, 0, 1.75f);
    e.Filter(kNearA, kWideA, 0, 0, 0);
    e.Filter(kWideA, kWideB, 1, 4.5f, 0);
    e.Filter(kWideB, kWideA, 1, 0, 4.5f);

    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    e.Transition(kNearA, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    e.Transition(kWideA, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const auto sceneRtv = e.Rtv(0);
    cmd->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
    e.Viewport(e.width, e.height);
    cmd->SetPipelineState(e.compositePso.Get());
    const PassConstants constants{ 0, 0, 0, 0, strength * 0.35f, strength * 1.25f, 2, 0 };
    cmd->SetGraphicsRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    cmd->SetGraphicsRootDescriptorTable(1, e.Srv(kNearA));
    cmd->SetGraphicsRootDescriptorTable(2, e.Srv(kWideA));
    cmd->DrawInstanced(3, 1, 0, 0);
    e.Restore();
}
