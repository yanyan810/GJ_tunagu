#include "UnderwaterBackgroundRenderer.h"

#include "DirectXCommon.h"
#include <cassert>

void UnderwaterBackgroundRenderer::Initialize(DirectXCommon* dx) {
    assert(dx);
    dx_ = dx;
    CreateRootSignature_();
    CreatePipelineState_();

    parameterResource_ = dx_->CreateBufferResource(
        sizeof(UnderwaterBackgroundParameters));
    parameterResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&parameterData_));
    assert(parameterData_);
}

void UnderwaterBackgroundRenderer::SetParameters(
    const UnderwaterBackgroundParameters& parameters) {
    parameters_ = parameters;
    if (parameterData_) {
        *parameterData_ = parameters_;
    }
}

void UnderwaterBackgroundRenderer::Draw() const {
    if (!dx_ || !parameterResource_ || parameters_.enabled < 0.5f) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dx_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(
        0, parameterResource_->GetGPUVirtualAddress());
    commandList->DrawInstanced(3, 1, 0, 0);
}

void UnderwaterBackgroundRenderer::CreateRootSignature_() {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = 1;
    desc.pParameters = &parameter;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (errorBlob) {
        OutputDebugStringA(
            static_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void UnderwaterBackgroundRenderer::CreatePipelineState_() {
    const Microsoft::WRL::ComPtr<IDxcBlob> vertexShader =
        dx_->CompilesSharder(L"resources/shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    const Microsoft::WRL::ComPtr<IDxcBlob> pixelShader =
        dx_->CompilesSharder(
            L"resources/shaders/UnderwaterBackground.PS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}
