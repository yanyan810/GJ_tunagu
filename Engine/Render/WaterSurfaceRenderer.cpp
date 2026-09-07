#include "WaterSurfaceRenderer.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "SrvManager.h"
#include "SceneColorFormat.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace {
constexpr char kNormalAPath[] = "resources/water/water_normal_a.png";
constexpr char kNormalBPath[] = "resources/water/water_normal_b.png";
constexpr char kReflectionPath[] = "resources/skybox/skybox.dds";
constexpr UINT kGridCells = 128;
// Concentrate vertices near the player; match the spacing estimate in the VS.
constexpr float kGridDistribution = 4.8f;

float FiniteClamp(float value, float fallback, float minimum, float maximum) {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

Vector3 FiniteColor(const Vector3& value, const Vector3& fallback) {
    return {
        FiniteClamp(value.x, fallback.x, 0.0f, 1.0f),
        FiniteClamp(value.y, fallback.y, 0.0f, 1.0f),
        FiniteClamp(value.z, fallback.z, 0.0f, 1.0f)
    };
}
}

void WaterSurfaceRenderer::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    assert(dx);
    assert(srv);
    assert(camera);
    dx_ = dx;
    srv_ = srv;
    camera_ = camera;

    CreateRootSignature_();
    CreatePipelineStates_();
    CreateResources_();

    TextureManager* textureManager = TextureManager::GetInstance();
    textureManager->LoadTextureLinear(kNormalAPath);
    textureManager->LoadTextureLinear(kNormalBPath);
    textureManager->LoadTexture(kReflectionPath);
    normalAHandle_ = textureManager->GetSrvHandleGPU(kNormalAPath);
    normalBHandle_ = textureManager->GetSrvHandleGPU(kNormalBPath);
    reflectionHandle_ = textureManager->GetSrvHandleGPU(kReflectionPath);

    Update(0.0f);
}

void WaterSurfaceRenderer::SetSceneOpticsSettings(bool enabled, float strength,
    int steps, float maxDistance) {
    sceneOpticsEnabled_ = enabled;
    sceneOpticsStrength_ = FiniteClamp(strength, 0.85f, 0.0f, 1.0f);
    sceneOpticsSteps_ = std::clamp(steps, 12, 40);
    sceneOpticsMaxDistance_ = FiniteClamp(maxDistance, 160.0f, 10.0f, 400.0f);
}

bool WaterSurfaceRenderer::PrepareSceneCapture_(const D3D12_RESOURCE_DESC& colorDesc,
    const D3D12_RESOURCE_DESC& depthDesc) {
    if (colorDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        colorDesc.SampleDesc.Count != 1 || depthDesc.SampleDesc.Count != 1 ||
        colorDesc.DepthOrArraySize != 1 || depthDesc.DepthOrArraySize != 1 ||
        colorDesc.MipLevels != 1 || depthDesc.MipLevels != 1 ||
        colorDesc.Width != depthDesc.Width || colorDesc.Height != depthDesc.Height ||
        colorDesc.Format != kSceneColorFormat || depthDesc.Format != DXGI_FORMAT_R32_TYPELESS) {
        return false;
    }
    if (sceneColorCopy_ && sceneDepthCopy_ &&
        sceneColorCopy_->GetDesc().Width == colorDesc.Width &&
        sceneColorCopy_->GetDesc().Height == colorDesc.Height) {
        return true;
    }

    // The renderer already waits for its frame fence before recording the next
    // frame. Keep descriptor indices stable when the render resolution changes.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC copyColor = colorDesc;
    D3D12_RESOURCE_DESC copyDepth = depthDesc;
    copyColor.Flags = D3D12_RESOURCE_FLAG_NONE;
    copyDepth.Flags = D3D12_RESOURCE_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12Resource> color;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    HRESULT hr = dx_->GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &copyColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr, IID_PPV_ARGS(&color));
    if (FAILED(hr)) {
        OutputDebugStringA("[WaterSurface] Scene color capture allocation failed; using environment reflection.\n");
        return false;
    }
    hr = dx_->GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &copyDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr, IID_PPV_ARGS(&depth));
    if (FAILED(hr)) {
        OutputDebugStringA("[WaterSurface] Scene depth capture allocation failed; using environment reflection.\n");
        return false;
    }
    color->SetName(L"Water pre-surface HDR color");
    depth->SetName(L"Water pre-surface depth");
    sceneColorCopy_ = std::move(color);
    sceneDepthCopy_ = std::move(depth);
    srv_->CreateSRVTexture2D(sceneColorSrvIndex_, sceneColorCopy_.Get(), kSceneColorFormat, 1);
    srv_->CreateSRVTexture2D(sceneDepthSrvIndex_, sceneDepthCopy_.Get(), DXGI_FORMAT_R32_FLOAT, 1);
    return true;
}

void WaterSurfaceRenderer::CaptureScene(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    if (!sceneOpticsData_) {
        return;
    }
    sceneOpticsData_->enabled = 0.0f;
    if (!enabled_ || !sceneOpticsEnabled_ || sceneOpticsStrength_ <= 0.0f ||
        !sceneColor || !sceneDepth ||
        !PrepareSceneCapture_(sceneColor->GetDesc(), sceneDepth->GetDesc())) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = dx_->GetCommandList();
    dx_->TransitionResource(sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    dx_->TransitionResource(sceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    dx_->TransitionResource(sceneColorCopy_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    dx_->TransitionResource(sceneDepthCopy_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->CopyResource(sceneColorCopy_.Get(), sceneColor);
    commandList->CopyResource(sceneDepthCopy_.Get(), sceneDepth);
    dx_->TransitionResource(sceneColorCopy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_->TransitionResource(sceneDepthCopy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_->TransitionResource(sceneColor, D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    dx_->TransitionResource(sceneDepth, D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    sceneOpticsData_->textureSize = {
        static_cast<float>(sceneColor->GetDesc().Width),
        static_cast<float>(sceneColor->GetDesc().Height) };
    sceneOpticsData_->viewProjection = camera_->GetViewProjectionMatrix();
    sceneOpticsData_->inverseViewProjection = Matrix4x4::Inverse(
        camera_->GetViewProjectionMatrix());
    sceneOpticsData_->view = camera_->GetViewMatrix();
    sceneOpticsData_->strength = sceneOpticsStrength_;
    sceneOpticsData_->maxDistance = sceneOpticsMaxDistance_;
    sceneOpticsData_->thickness = 1.10f;
    sceneOpticsData_->steps = sceneOpticsSteps_;
    const Matrix4x4& projection = camera_->GetProjectionMatrix();
    sceneOpticsData_->depthUnpack = { projection.m[3][2], projection.m[2][2] };
    sceneOpticsData_->enabled = 1.0f;
}

void WaterSurfaceRenderer::SetNormalSettings(
    float scaleA, float scaleB, const Vector2& speedA,
    const Vector2& speedB, float strength) {
    normalScaleA_ = scaleA;
    normalScaleB_ = scaleB;
    normalSpeedA_ = speedA;
    normalSpeedB_ = speedB;
    normalStrength_ = strength;
}

void WaterSurfaceRenderer::SetFresnelSettings(float strength, float power) {
    fresnelStrength_ = strength;
    fresnelPower_ = power;
}

void WaterSurfaceRenderer::SetUnderwaterAppearance(float skyExposure, float floorHeight,
    const Vector3& floorColor, const Vector3& extinctionDistanceRGB,
    const Vector3& deepWaterColor, float floorReflectionStrength) {
    skyExposure_ = FiniteClamp(skyExposure, 0.65f, 0.0f, 4.0f);
    reflectionFloorHeight_ = std::isfinite(floorHeight) ? floorHeight : -22.0f;
    reflectionFloorColor_ = FiniteColor(floorColor, { 0.58f, 0.49f, 0.34f });
    extinctionDistanceRGB_ = {
        FiniteClamp(extinctionDistanceRGB.x, 40.0f, 0.001f, 100000.0f),
        FiniteClamp(extinctionDistanceRGB.y, 95.0f, 0.001f, 100000.0f),
        FiniteClamp(extinctionDistanceRGB.z, 130.0f, 0.001f, 100000.0f)
    };
    deepWaterColor_ = FiniteColor(deepWaterColor, { 0.018f, 0.115f, 0.16f });
    floorReflectionStrength_ = FiniteClamp(floorReflectionStrength, 1.0f, 0.0f, 1.0f);
}

void WaterSurfaceRenderer::Update(float dt) {
    if (!camera_ || !transformationData_ || !cameraData_ || !parameterData_) {
        return;
    }

    time_ = std::fmod(time_ + std::max(dt, 0.0f), 4096.0f);
    const Vector3 cameraPosition = camera_->GetTranslate();
    const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(
        { 1.0f, 1.0f, 1.0f },
        {},
        { cameraPosition.x, waterLevel_, cameraPosition.z });
    transformationData_->World = world;
    transformationData_->WVP = Matrix4x4::Multiply(
        world, camera_->GetViewProjectionMatrix());
    cameraData_->worldPosition = cameraPosition;
    cameraData_->padding = 0.0f;

    parameterData_->surfaceTint = surfaceTint_;
    parameterData_->normalScaleA = std::max(normalScaleA_, 0.0001f);
    parameterData_->normalScaleB = std::max(normalScaleB_, 0.0001f);
    parameterData_->normalStrength = std::max(normalStrength_, 0.0f);
    parameterData_->time = time_;
    parameterData_->normalSpeedA = normalSpeedA_;
    parameterData_->normalSpeedB = normalSpeedB_;
    parameterData_->fresnelStrength = std::max(fresnelStrength_, 0.0f);
    parameterData_->fresnelPower = std::max(fresnelPower_, 0.01f);
    parameterData_->reflectionStrength = std::clamp(reflectionStrength_, 0.0f, 1.0f);
    parameterData_->waveStrength = std::clamp(waveStrength_, 0.0f, 2.0f);
    const float sunLength = std::sqrt(
        sunDirection_.x * sunDirection_.x + sunDirection_.y * sunDirection_.y +
        sunDirection_.z * sunDirection_.z);
    parameterData_->sunDirection = sunLength > 0.0001f ?
        sunDirection_ * (1.0f / sunLength) : Vector3{ 0.0f, 1.0f, 0.0f };
    parameterData_->waterLevel = waterLevel_;
    parameterData_->reflectionFloorColor = reflectionFloorColor_;
    parameterData_->reflectionFloorHeight = reflectionFloorHeight_;
    parameterData_->extinctionDistanceRGB = extinctionDistanceRGB_;
    parameterData_->skyExposure = skyExposure_;
    parameterData_->deepWaterColor = deepWaterColor_;
    parameterData_->floorReflectionStrength = floorReflectionStrength_;
    // A capture belongs to one frame only. Rendering without CaptureScene safely
    // uses the environment approximation instead of a stale camera's image.
    sceneOpticsData_->enabled = 0.0f;
}

void WaterSurfaceRenderer::DrawDepth() const {
    if (!enabled_) {
        return;
    }
    BindCommon_(depthPipelineState_.Get());
    dx_->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void WaterSurfaceRenderer::DrawColor() const {
    if (!enabled_) {
        return;
    }
    BindCommon_(colorPipelineState_.Get());
    dx_->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void WaterSurfaceRenderer::CreateRootSignature_() {
    D3D12_DESCRIPTOR_RANGE ranges[5]{};
    for (UINT i = 0; i < _countof(ranges); ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER parameters[9]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 1;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (UINT i = 0; i < 5; ++i) {
        parameters[3 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3 + i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[3 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[3 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[8].Descriptor.ShaderRegister = 2;
    parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = { sampler, sampler };
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(parameters);
    desc.pParameters = parameters;
    desc.NumStaticSamplers = _countof(samplers);
    desc.pStaticSamplers = samplers;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (errorBlob) {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void WaterSurfaceRenderer::CreatePipelineStates_() {
    const Microsoft::WRL::ComPtr<IDxcBlob> vertexShader =
        dx_->CompilesSharder(L"resources/shaders/WaterSurface.VS.hlsl", L"vs_6_0");
    const Microsoft::WRL::ComPtr<IDxcBlob> depthShader =
        dx_->CompilesSharder(L"resources/shaders/WaterSurfaceDepth.PS.hlsl", L"ps_6_0");
    const Microsoft::WRL::ComPtr<IDxcBlob> colorShader =
        dx_->CompilesSharder(L"resources/shaders/WaterSurface.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputElement{};
    inputElement.SemanticName = "POSITION";
    inputElement.SemanticIndex = 0;
    inputElement.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElement.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC base{};
    base.pRootSignature = rootSignature_.Get();
    base.InputLayout = { &inputElement, 1 };
    base.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    base.RasterizerState = rasterizer;
    base.NumRenderTargets = 1;
    base.RTVFormats[0] = kSceneColorFormat;
    base.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    base.SampleDesc.Count = 1;
    base.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    base.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    D3D12_DEPTH_STENCIL_DESC depthState{};
    depthState.DepthEnable = TRUE;
    depthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    base.DepthStencilState = depthState;
    base.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    base.PS = { depthShader->GetBufferPointer(), depthShader->GetBufferSize() };
    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &base, IID_PPV_ARGS(&depthPipelineState_));
    assert(SUCCEEDED(hr));

    depthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    base.DepthStencilState = depthState;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = base.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    base.PS = { colorShader->GetBufferPointer(), colorShader->GetBufferSize() };
    hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &base, IID_PPV_ARGS(&colorPipelineState_));
    assert(SUCCEEDED(hr));
}

void WaterSurfaceRenderer::CreateResources_() {
    static_assert(sizeof(TransformationData) == 128);
    static_assert(sizeof(CameraData) == 16);
    static_assert(sizeof(WaterParameters) == 128);
    static_assert(offsetof(WaterParameters, normalSpeedA) == 32);
    static_assert(offsetof(WaterParameters, fresnelStrength) == 48);
    static_assert(offsetof(WaterParameters, waveStrength) == 60);
    static_assert(offsetof(WaterParameters, sunDirection) == 64);
    static_assert(offsetof(WaterParameters, waterLevel) == 76);
    static_assert(offsetof(WaterParameters, reflectionFloorColor) == 80);
    static_assert(offsetof(WaterParameters, reflectionFloorHeight) == 92);
    static_assert(offsetof(WaterParameters, extinctionDistanceRGB) == 96);
    static_assert(offsetof(WaterParameters, skyExposure) == 108);
    static_assert(offsetof(WaterParameters, deepWaterColor) == 112);
    static_assert(offsetof(WaterParameters, floorReflectionStrength) == 124);
    static_assert(sizeof(SceneOpticsParameters) == 240);
    static_assert(offsetof(SceneOpticsParameters, inverseViewProjection) == 64);
    static_assert(offsetof(SceneOpticsParameters, view) == 128);
    static_assert(offsetof(SceneOpticsParameters, textureSize) == 192);
    static_assert(offsetof(SceneOpticsParameters, enabled) == 200);
    static_assert(offsetof(SceneOpticsParameters, maxDistance) == 208);
    static_assert(offsetof(SceneOpticsParameters, steps) == 216);
    static_assert(offsetof(SceneOpticsParameters, depthUnpack) == 224);

    std::vector<VertexData> vertices((kGridCells + 1) * (kGridCells + 1));
    const auto gridCoordinate = [this](UINT index) {
        const float t = 2.0f * static_cast<float>(index) / kGridCells - 1.0f;
        return surfaceHalfExtent_ * std::sinh(kGridDistribution * t) /
            std::sinh(kGridDistribution);
    };
    for (UINT z = 0; z <= kGridCells; ++z) {
        for (UINT x = 0; x <= kGridCells; ++x) {
            vertices[z * (kGridCells + 1) + x].position =
                { gridCoordinate(x), 0.0f, gridCoordinate(z), 1.0f };
        }
    }
    std::vector<UINT> indices;
    indices.reserve(kGridCells * kGridCells * 6);
    for (UINT z = 0; z < kGridCells; ++z) {
        for (UINT x = 0; x < kGridCells; ++x) {
            const UINT a = z * (kGridCells + 1) + x;
            const UINT b = a + kGridCells + 1;
            indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    }
    indexCount_ = static_cast<UINT>(indices.size());
    const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(VertexData));
    const UINT indexBytes = static_cast<UINT>(indices.size() * sizeof(UINT));
    vertexResource_ = dx_->CreateBufferResource(vertexBytes);
    VertexData* mappedVertices = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertices));
    std::memcpy(mappedVertices, vertices.data(), vertexBytes);
    vertexResource_->Unmap(0, nullptr);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = vertexBytes;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    indexResource_ = dx_->CreateBufferResource(indexBytes);
    UINT* mappedIndices = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedIndices));
    std::memcpy(mappedIndices, indices.data(), indexBytes);
    indexResource_->Unmap(0, nullptr);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = indexBytes;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    transformationResource_ = dx_->CreateBufferResource(256);
    cameraResource_ = dx_->CreateBufferResource(256);
    parameterResource_ = dx_->CreateBufferResource(256);
    sceneOpticsResource_ = dx_->CreateBufferResource(256);
    transformationResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationData_));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
    parameterResource_->Map(0, nullptr, reinterpret_cast<void**>(&parameterData_));
    sceneOpticsResource_->Map(0, nullptr, reinterpret_cast<void**>(&sceneOpticsData_));
    assert(sceneOpticsData_);
    *sceneOpticsData_ = {};
    sceneColorSrvIndex_ = srv_->Allocate();
    sceneDepthSrvIndex_ = srv_->Allocate();
    // Valid null SRVs also make the disabled/failure path safe for the debug layer.
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    nullSrv.Format = kSceneColorFormat;
    dx_->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv,
        srv_->GetCPUDescriptionHandle(sceneColorSrvIndex_));
    nullSrv.Format = DXGI_FORMAT_R32_FLOAT;
    dx_->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv,
        srv_->GetCPUDescriptionHandle(sceneDepthSrvIndex_));
    assert(transformationData_ && cameraData_ && parameterData_);
}

void WaterSurfaceRenderer::BindCommon_(ID3D12PipelineState* pipelineState) const {
    ID3D12GraphicsCommandList* commandList = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = {
        TextureManager::GetInstance()->GetSrvDescriptorHeap()
    };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, transformationResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, cameraResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        2, parameterResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(3, normalAHandle_);
    commandList->SetGraphicsRootDescriptorTable(4, normalBHandle_);
    commandList->SetGraphicsRootDescriptorTable(5, reflectionHandle_);
    commandList->SetGraphicsRootDescriptorTable(6, srv_->GetGPUDescriptionHandle(sceneColorSrvIndex_));
    commandList->SetGraphicsRootDescriptorTable(7, srv_->GetGPUDescriptionHandle(sceneDepthSrvIndex_));
    commandList->SetGraphicsRootConstantBufferView(8, sceneOpticsResource_->GetGPUVirtualAddress());
}
