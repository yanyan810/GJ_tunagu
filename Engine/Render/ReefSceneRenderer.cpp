#include "ReefSceneRenderer.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "SceneColorFormat.h"
#include "SrvManager.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
constexpr float kTau = 6.28318530718f;
constexpr float kShadowHalfExtent = 180.0f;
constexpr float kShadowNear = 1.0f;
constexpr float kShadowFar = 440.0f;
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
Vector3 Unit(const Vector3& v) {
    const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
    return lengthSquared > 0.000001f ? v * (1.0f / std::sqrt(lengthSquared)) : Vector3{ 0, 1, 0 };
}
float Smooth(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
class ReefRandom {
public:
    float Next() {
        state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
        return static_cast<float>(state_ & 0xffffffu) / 16777216.0f;
    }
    float Range(float a, float b) { return a + (b - a) * Next(); }
private:
    uint32_t state_ = 0x3a29c71bu;
};
// Broad asymmetric banks, with a flat navigation corridor and a feathered edge
// that sinks below the existing infinite floor. All heights are floor-relative.
float BankHeight(float x, float z) {
    const auto mound = [&](float cx, float cz, float sx, float sz, float height) {
        const float dx = (x - cx) / sx, dz = (z - cz) / sz;
        return height * std::exp(-(dx * dx + dz * dz));
    };
    const float corridor = Smooth(14.0f, 24.0f, std::abs(x));
    const float edge = (1.0f - Smooth(93.0f, 110.0f, std::abs(x))) *
        Smooth(-30.0f, -12.0f, z) * (1.0f - Smooth(185.0f, 210.0f, z));
    const float dune = mound(-36, 54, 21, 40, 6.4f) + mound(40, 105, 29, 49, 7.5f) +
        mound(-56, 144, 36, 36, 9.0f) + mound(33, 8, 19, 25, 1.2f);
    const float relief = 0.27f * std::sin(x * 0.13f + std::sin(z * 0.08f)) *
        std::sin(z * 0.12f);
    return edge * (0.055f + corridor * (dune + relief)) - 0.12f * (1.0f - edge);
}
}

void ReefSceneRenderer::Initialize(DirectXCommon* dx, SrvManager* srvManager, Camera* camera) {
    assert(dx && srvManager && camera);
    dx_ = dx;
    camera_ = camera;
    static_assert(sizeof(VertexData) == 48);
    static_assert(sizeof(FrameData) == 160);
    static_assert(offsetof(FrameData, towardSun) == 64);
    static_assert(offsetof(FrameData, cameraPosition) == 80);
    static_assert(offsetof(FrameData, floorHeight) == 92);
    static_assert(offsetof(FrameData, sandAppearance) == 96);
    static_assert(offsetof(FrameData, sandVariation) == 112);
    static_assert(offsetof(FrameData, sandSunColor) == 128);
    static_assert(offsetof(FrameData, sandAirSun) == 144);
    CreatePipeline_();
    CreateGeometry_();
    CreateShadowMap_(srvManager);
    frameResource_ = dx_->CreateBufferResource(256);
    shadowFrameResource_ = dx_->CreateBufferResource(256);
    HRESULT hr = frameResource_->Map(0, nullptr, reinterpret_cast<void**>(&frameData_));
    assert(SUCCEEDED(hr));
    hr = shadowFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&shadowFrameData_));
    assert(SUCCEEDED(hr));
    Update(0.0f, floorHeight_, towardSun_);
}

void ReefSceneRenderer::Update(float dt, float floorY, const Vector3& towardSun) {
    time_ = std::fmod(time_ + std::max(0.0f, dt), 4096.0f);
    const Vector3 airSun = Unit(towardSun);
    const float refractedX = airSun.x / 1.333f, refractedZ = airSun.z / 1.333f;
    const Vector3 sun{ refractedX,
        std::sqrt(std::max(0.0f, 1.0f - refractedX * refractedX - refractedZ * refractedZ)), refractedZ };
    const Vector3 delta = sun - towardSun_;
    shadowDirty_ = shadowDirty_ || std::abs(floorY - floorHeight_) > 0.00001f ||
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 0.00000001f;
    floorHeight_ = floorY;
    collisionWorld_.SetFloorHeight(floorY);
    towardSun_ = sun;
    // Fixed world coverage avoids the swimming camera making the shadow shimmer.
    const Vector3 target{ 0.0f, floorHeight_ + 10.0f, 85.0f };
    const Vector3 eye = target + towardSun_ * 220.0f;
    const Vector3 up = std::abs(towardSun_.y) > 0.96f ? Vector3{ 0, 0, 1 } : Vector3{ 0, 1, 0 };
    const Matrix4x4 view = Matrix4x4::MakeViewMatrix(eye, target, up);
    // Explicit left-handed D3D orthographic projection: z maps to [0,1].
    // The engine's legacy orthographic helper maps z to [-1,1].
    Matrix4x4 projection{};
    projection.m[0][0] = 1.0f / kShadowHalfExtent;
    projection.m[1][1] = 1.0f / kShadowHalfExtent;
    projection.m[2][2] = 1.0f / (kShadowFar - kShadowNear);
    projection.m[3][2] = -kShadowNear / (kShadowFar - kShadowNear);
    projection.m[3][3] = 1.0f;
    shadowViewProjection_ = view * projection;
}

void ReefSceneRenderer::SetSandAppearance(const Vector3& color, float strength,
    const Vector3& variation, const Vector3& sunColor, const Vector3& airTowardSun) {
    sandAppearance_ = { std::max(0.0f, color.x), std::max(0.0f, color.y),
        std::max(0.0f, color.z), std::max(strength, 0.0f) };
    sandVariation_ = { variation.x >= 0.5f ? 1.0f : 0.0f,
        std::max(variation.y, 0.0f), std::max(variation.z, 0.0f), 0.0f };
    sandSunColor_ = { std::max(sunColor.x, 0.0f), std::max(sunColor.y, 0.0f),
        std::max(sunColor.z, 0.0f), 0.0f };
    sandAirSun_ = { airTowardSun.x, airTowardSun.y, airTowardSun.z, 0.0f };
}

void ReefSceneRenderer::BindGeometry_() const {
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->IASetIndexBuffer(&indexBufferView_);
}

void ReefSceneRenderer::Draw() const {
    if (!enabled_ || !dx_ || !camera_ || !frameData_) return;
    *frameData_ = { camera_->GetViewProjectionMatrix(), towardSun_, time_,
        camera_->GetTranslate(), floorHeight_, sandAppearance_,
        sandVariation_, sandSunColor_, sandAirSun_ };
    BindGeometry_();
    auto* cmd = dx_->GetCommandList();
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, frameResource_->GetGPUVirtualAddress());
    cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void ReefSceneRenderer::DrawShadow() {
    if (!enabled_ || !dx_ || !shadowMap_ || !shadowFrameData_ || !shadowDirty_) return;
    // Separate storage from the main camera CB: both draws can be in the same
    // submitted command list without one CPU update replacing the other's data.
    *shadowFrameData_ = { shadowViewProjection_, towardSun_, 0.0f, {}, floorHeight_, sandAppearance_,
        sandVariation_, sandSunColor_, sandAirSun_ };
    auto* cmd = dx_->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = shadowMap_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cmd->ResourceBarrier(1, &barrier);
    const auto dsv = shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    const D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(GetShadowMapSize()),
        static_cast<float>(GetShadowMapSize()), 0, 1 };
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(GetShadowMapSize()),
        static_cast<LONG>(GetShadowMapSize()) };
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    BindGeometry_();
    cmd->SetPipelineState(shadowPipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, shadowFrameResource_->GetGPUVirtualAddress());
    cmd->DrawIndexedInstanced(shadowIndexCount_, 1, 0, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmd->ResourceBarrier(1, &barrier);
    shadowDirty_ = false;
}

void ReefSceneRenderer::CreatePipeline_() {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &parameter;
    Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
    const auto vs = dx_->CompilesSharder(L"resources/shaders/ReefScene.VS.hlsl", L"vs_6_0");
    const auto ps = dx_->CompilesSharder(L"resources/shaders/ReefScene.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[4]{};
    inputs[0].SemanticName = "POSITION"; inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[1].SemanticName = "NORMAL"; inputs[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[2].SemanticName = "TEXCOORD"; inputs[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputs[3].SemanticName = "TEXCOORD"; inputs[3].SemanticIndex = 1;
    inputs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    for (auto& input : inputs) input.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.InputLayout = { inputs, _countof(inputs) };
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kSceneColorFormat;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = dx_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
    desc.NumRenderTargets = 0;
    desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    desc.PS = {};
    desc.RasterizerState.DepthBias = 128;
    desc.RasterizerState.SlopeScaledDepthBias = 1.25f;
    desc.RasterizerState.DepthBiasClamp = 0.004f;
    hr = dx_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&shadowPipelineState_));
    assert(SUCCEEDED(hr));
}

void ReefSceneRenderer::CreateShadowMap_(SrvManager* srvManager) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = GetShadowMapSize();
    desc.Height = GetShadowMapSize();
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT hr = dx_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&shadowMap_));
    assert(SUCCEEDED(hr));
    shadowMap_->SetName(L"Fixed reef sunlight shadow map");
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    hr = dx_->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&shadowDsvHeap_));
    assert(SUCCEEDED(hr));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dx_->GetDevice()->CreateDepthStencilView(shadowMap_.Get(), &dsv,
        shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());
    const uint32_t index = srvManager->Allocate();
    srvManager->CreateSRVTexture2D(index, shadowMap_.Get(), DXGI_FORMAT_R32_FLOAT, 1);
    shadowSrv_ = srvManager->GetGPUDescriptionHandle(index);
}

void ReefSceneRenderer::CreateGeometry_() {
    std::vector<VertexData> vertices;
    std::vector<UINT> indices;
    vertices.reserve(70000);
    indices.reserve(240000);
    ReefRandom random;
    const auto normalsFrom = [&](UINT firstVertex, size_t firstIndex) {
        for (size_t i = firstIndex; i < indices.size(); i += 3) {
            auto& a = vertices[indices[i]]; auto& b = vertices[indices[i + 1]];
            auto& c = vertices[indices[i + 2]];
            const Vector3 n = Cross(b.position - a.position, c.position - a.position);
            a.normal += n; b.normal += n; c.normal += n;
        }
        for (size_t i = firstVertex; i < vertices.size(); ++i) vertices[i].normal = Unit(vertices[i].normal);
    };
    // Two-metre samples resolve the banks' actual silhouette and their shadows.
    constexpr UINT columns = 111, rows = 121;
    for (UINT z = 0; z < rows; ++z) {
        for (UINT x = 0; x < columns; ++x) {
            const float px = -110.0f + static_cast<float>(x) * 2.0f;
            const float pz = -30.0f + static_cast<float>(z) * 2.0f;
            vertices.push_back({ { px, BankHeight(px, pz), pz }, {}, { px, pz }, { 0, 0, 0, 0 } });
        }
    }
    for (UINT z = 0; z + 1 < rows; ++z) {
        for (UINT x = 0; x + 1 < columns; ++x) {
            const UINT a = z * columns + x;
            indices.insert(indices.end(), { a, a + columns, a + 1, a + 1, a + columns, a + columns + 1 });
        }
    }
    normalsFrom(0, 0);

    // The infinite floor owns the flat seabed. Clip buried/near-coplanar sand
    // instead of letting two differently tessellated surfaces compete at y=0.
    // Use this same clipped mesh for drawing, sunlight and collision below.
    constexpr float kSandFloorSeparation = 0.02f;
    std::vector<UINT> visibleSandIndices;
    visibleSandIndices.reserve(indices.size());
    for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        const std::array<UINT, 3> source{ indices[triangle], indices[triangle + 1], indices[triangle + 2] };
        std::array<UINT, 4> polygon{};
        size_t count = 0;
        UINT previousIndex = source.back();
        for (UINT currentIndex : source) {
            // Copy before appending: push_back can invalidate vertex references.
            const VertexData previous = vertices[previousIndex];
            const VertexData current = vertices[currentIndex];
            const bool previousInside = previous.position.y >= kSandFloorSeparation;
            const bool currentInside = current.position.y >= kSandFloorSeparation;
            if (previousInside != currentInside) {
                const float t = (kSandFloorSeparation - previous.position.y) /
                    (current.position.y - previous.position.y);
                VertexData edge = previous;
                edge.position = previous.position + (current.position - previous.position) * t;
                edge.position.y = kSandFloorSeparation;
                edge.normal = Unit(previous.normal + (current.normal - previous.normal) * t);
                edge.uv = { edge.position.x, edge.position.z };
                polygon[count++] = static_cast<UINT>(vertices.size());
                vertices.push_back(edge);
            }
            if (currentInside) { polygon[count++] = currentIndex; }
            previousIndex = currentIndex;
        }
        for (size_t corner = 1; corner + 1 < count; ++corner) {
            const Vector3 cross = Cross(vertices[polygon[corner]].position - vertices[polygon[0]].position,
                vertices[polygon[corner + 1]].position - vertices[polygon[0]].position);
            if (cross.x * cross.x + cross.y * cross.y + cross.z * cross.z > 1.0e-12f) {
                visibleSandIndices.insert(visibleSandIndices.end(),
                    { polygon[0], polygon[corner], polygon[corner + 1] });
            }
        }
    }
    indices.swap(visibleSandIndices);

    const auto rock = [&](float x, float z, float sx, float sz, float height, float extraY = 0.0f) {
        constexpr UINT sides = 18, rings = 10;
        const UINT start = static_cast<UINT>(vertices.size());
        const size_t firstIndex = indices.size();
        const float base = BankHeight(x, z) + extraY;
        const float phase = random.Range(0, kTau), rotation = random.Range(0, kTau);
        const float variant = random.Next();
        for (UINT row = 0; row < rings; ++row) {
            const float t = static_cast<float>(row) / (rings - 1);
            const float radius = std::sqrt(std::max(0.003f, 1.0f - t * t)) *
                (0.92f + 0.06f * std::sin(t * 16.0f + phase));
            for (UINT side = 0; side < sides; ++side) {
                const float angle = kTau * static_cast<float>(side) / sides;
                const float lobe = 1.0f + 0.13f * std::sin(angle * 3 + phase) +
                    0.06f * std::sin(angle * 7 - phase + t * 2.0f);
                const float lx = std::cos(angle) * sx * radius * lobe;
                const float lz = std::sin(angle) * sz * radius * lobe;
                const float y = height * (t - 0.10f + 0.035f * radius * std::sin(angle * 4 + phase));
                vertices.push_back({ { x + lx * std::cos(rotation) - lz * std::sin(rotation),
                    base + y, z + lx * std::sin(rotation) + lz * std::cos(rotation) }, {},
                    { static_cast<float>(side) / sides, t }, { 1, variant, 0, 0 } });
            }
        }
        for (UINT row = 0; row + 1 < rings; ++row) {
            for (UINT side = 0; side < sides; ++side) {
                const UINT a = start + row * sides + side, b = start + row * sides + (side + 1) % sides;
                indices.insert(indices.end(), { a, a + sides, b, b, a + sides, b + sides });
            }
        }
        const UINT cap = static_cast<UINT>(vertices.size());
        vertices.push_back({ { x, base + height * 0.905f, z }, {}, { 0.5f, 1 }, { 1, variant, 0, 0 } });
        for (UINT side = 0; side < sides; ++side)
            indices.insert(indices.end(), { start + (rings - 1) * sides + side, cap,
                start + (rings - 1) * sides + (side + 1) % sides });
        normalsFrom(start, firstIndex);
    };
    // Different masses frame the approach. All large structures stay beside the
    // route; the far left wall and right arch overlap in depth, not in silhouette.
    rock(-35, 42, 10, 15, 12); rock(-45, 65, 14, 17, 21);
    rock(-53, 87, 16, 14, 28); rock(-57, 115, 21, 13, 27);
    rock(-64, 149, 25, 21, 25); rock(42, 40, 10, 14, 11);
    rock(61, 123, 17, 25, 23); rock(56, 164, 22, 17, 25);
    rock(-28, 27, 6, 9, 5); rock(30, 65, 6, 10, 7);

    // A rough elliptical stone arch with a genuine open underside, not a solid
    // rock with a dark patch. It spans x=16..56 at z=97, keeping the main lane open.
    {
        constexpr UINT segments = 40, tubeSides = 16;
        const UINT start = static_cast<UINT>(vertices.size());
        const size_t firstIndex = indices.size();
        constexpr float centerX = 36.0f, centerZ = 97.0f, radiusX = 15.5f, radiusY = 20.0f;
        const float baseY = BankHeight(centerX, centerZ) - 1.0f;
        for (UINT row = 0; row <= segments; ++row) {
            const float t = static_cast<float>(row) / segments;
            const float angle = t * kTau * 0.5f;
            const Vector3 center{ centerX + radiusX * std::cos(angle),
                baseY + radiusY * std::sin(angle), centerZ + 1.2f * std::sin(angle * 2) };
            const Vector3 radial = Unit({ std::cos(angle) / radiusX, std::sin(angle) / radiusY, 0 });
            for (UINT side = 0; side < tubeSides; ++side) {
                const float tubeAngle = kTau * static_cast<float>(side) / tubeSides;
                const float radius = 3.6f * (1.0f + 0.13f * std::sin(angle * 7.0f + tubeAngle * 3.0f) +
                    0.08f * std::cos(angle * 13.0f - tubeAngle * 2.0f));
                const Vector3 offset = radial * (std::cos(tubeAngle) * radius) +
                    Vector3{ 0, 0, std::sin(tubeAngle) * radius * 1.2f };
                vertices.push_back({ center + offset, {}, { t, static_cast<float>(side) / tubeSides },
                    { 1, 0.68f, 0, 0 } });
            }
        }
        for (UINT row = 0; row < segments; ++row) {
            for (UINT side = 0; side < tubeSides; ++side) {
                const UINT a = start + row * tubeSides + side;
                const UINT b = start + row * tubeSides + (side + 1) % tubeSides;
                indices.insert(indices.end(), { a, a + tubeSides, b, b, a + tubeSides, b + tubeSides });
            }
        }
        normalsFrom(start, firstIndex);
        rock(20.5f, 97, 4.5f, 5.5f, 7); rock(51.5f, 97, 6.5f, 7, 8);
    }
    for (UINT i = 0; i < 42; ++i) {
        const float side = i % 2 == 0 ? -1.0f : 1.0f;
        const float x = side * random.Range(24.0f, 74.0f), z = random.Range(8.0f, 180.0f);
        const float size = random.Range(1.2f, 4.6f);
        rock(x, z, size, size * random.Range(0.7f, 1.5f), size * random.Range(0.5f, 1.25f));
    }
    // Only immutable terrain and rock cast the cached shadow. The following
    // vegetation moves gently and receives the shared shadow without casting it.
    shadowIndexCount_ = static_cast<UINT>(indices.size());
    std::vector<ReefCollisionWorld::Triangle> collisionTriangles;
    collisionTriangles.reserve(shadowIndexCount_ / 3);
    for (UINT i = 0; i < shadowIndexCount_; i += 3) {
        const auto& a = vertices[indices[i]];
        collisionTriangles.push_back({ a.position, vertices[indices[i + 1]].position,
            vertices[indices[i + 2]].position, a.detail.x < 0.5f });
    }
    collisionWorld_.SetReefTriangles(std::move(collisionTriangles));

    const auto leafClump = [&](float x, float z, float size, float variant, bool broadLeaf) {
        for (UINT blade = 0; blade < 8; ++blade) {
            constexpr UINT segments = 6;
            const float angle = random.Range(0, kTau), phase = random.Range(0, kTau);
            const float h = size * random.Range(0.7f, broadLeaf ? 1.25f : 1.5f);
            const float bend = h * random.Range(0.15f, broadLeaf ? 0.36f : 0.5f);
            const float bx = x + random.Range(-0.6f, 0.6f), bz = z + random.Range(-0.6f, 0.6f);
            const float by = BankHeight(bx, bz) - 0.10f;
            const UINT start = static_cast<UINT>(vertices.size());
            for (UINT row = 0; row <= segments; ++row) {
                const float t = static_cast<float>(row) / segments;
                const float orientation = angle + t * 0.5f;
                const Vector3 side{ std::cos(orientation), 0, std::sin(orientation) };
                const Vector3 facing{ -std::sin(orientation), 0, std::cos(orientation) };
                // Broad blades retain a readable paddle silhouette at medium
                // distance; only the final short segment rounds into the tip.
                const float profile = broadLeaf
                    ? std::sqrt(std::max(0.0f, std::sin(t * kTau * 0.5f)))
                    : (0.2f + std::sqrt(t)) * (1.0f - t);
                const float width = size * (broadLeaf ? 0.24f : 0.11f) * profile + 0.005f;
                const Vector3 center{ bx - std::sin(angle) * bend * t * t, by + h * t,
                    bz + std::cos(angle) * bend * t * t };
                for (UINT column = 0; column < 3; ++column) {
                    const float across = static_cast<float>(column) - 1.0f;
                    vertices.push_back({ center + side * (across * width) +
                        facing * (column == 1 ? width * 0.2f : 0.0f),
                        Unit(facing + side * (across * 0.25f) + Vector3{ 0, -t * bend / h, 0 }),
                        { 0.5f + across * 0.5f, t }, { broadLeaf ? 2.2f : 2.0f, variant, h * t * t, phase } });
                }
            }
            for (UINT row = 0; row < segments; ++row) {
                for (UINT column = 0; column < 2; ++column) {
                    const UINT a = start + row * 3 + column;
                    indices.insert(indices.end(), { a, a + 1, a + 3, a + 1, a + 4, a + 3 });
                }
            }
        }
    };
    // Disconnected meadows define edges to the open sand; the foreground uses
    // shorter leaves, while tall fronds stay on the outer banks.
    const Vector3 meadows[] = { {-22, 1.6f, 13}, {26, 1.8f, 28}, {-28, 2.3f, 49},
        {29, 2.8f, 78}, {-35, 3.4f, 96}, {57, 3.7f, 122}, {-40, 3.1f, 151}, {28, 2.4f, 165} };
    for (UINT meadowIndex = 0; meadowIndex < _countof(meadows); ++meadowIndex) {
        const auto& meadow = meadows[meadowIndex];
        const bool warmMeadow = meadowIndex == 1 || meadowIndex == 2 || meadowIndex == 3;
        const UINT clumps = warmMeadow ? 52 : 25;
        for (UINT clump = 0; clump < clumps; ++clump) {
            const float angle = random.Range(0, kTau), radial = std::sqrt(random.Next());
            const float edge = 0.84f + 0.14f * std::sin(angle * 3.0f + static_cast<float>(meadowIndex));
            const float radius = radial * (warmMeadow ? 7.0f : 9.5f) * edge;
            const float x = meadow.x + std::cos(angle) * radius;
            // Include leaf width and bend in the route's clearance, not only roots.
            if (std::abs(x) < 18.0f) continue;
            const float heightFalloff = warmMeadow ? 1.0f - 0.30f * radial : 1.0f;
            const float palette = warmMeadow
                ? (meadowIndex == 3 ? 0.55f : 0.15f) + random.Range(-0.07f, 0.07f)
                : random.Range(0.1f, 0.9f);
            leafClump(x, meadow.z + std::sin(angle) * radius * 1.35f,
                meadow.y * random.Range(0.7f, 1.15f) * heightFalloff, palette, warmMeadow);
        }
    }

    const auto coralBranch = [&](const Vector3& a, const Vector3& b, float radius, float variant, float startTip) {
        constexpr UINT sides = 7, rings = 4;
        const UINT start = static_cast<UINT>(vertices.size());
        const Vector3 axis = Unit(b - a);
        const Vector3 tangent = Unit(Cross(axis, std::abs(axis.y) > 0.9f ? Vector3{ 1, 0, 0 } : Vector3{ 0, 1, 0 }));
        const Vector3 bitangent = Cross(axis, tangent);
        for (UINT row = 0; row < rings; ++row) {
            const float t = static_cast<float>(row) / (rings - 1);
            const Vector3 center = a + (b - a) * t;
            const float r = radius * (1.0f - 0.78f * t);
            for (UINT side = 0; side < sides; ++side) {
                const float angle = kTau * static_cast<float>(side) / sides;
                const Vector3 n = tangent * std::cos(angle) + bitangent * std::sin(angle);
                vertices.push_back({ center + n * r, n, { static_cast<float>(side) / sides,
                    startTip + t * (1.0f - startTip) }, { 3, variant, 0, 0 } });
            }
        }
        for (UINT row = 0; row + 1 < rings; ++row) {
            for (UINT side = 0; side < sides; ++side) {
                const UINT aIndex = start + row * sides + side;
                const UINT bIndex = start + row * sides + (side + 1) % sides;
                indices.insert(indices.end(), { aIndex, bIndex, aIndex + sides, bIndex, bIndex + sides, aIndex + sides });
            }
        }
        const UINT tip = static_cast<UINT>(vertices.size());
        vertices.push_back({ b + axis * radius * 0.08f, axis, { 0.5f, 1 }, { 3, variant, 0, 0 } });
        for (UINT side = 0; side < sides; ++side)
            indices.insert(indices.end(), { start + (rings - 1) * sides + side,
                start + (rings - 1) * sides + (side + 1) % sides, tip });
    };
    const Vector3 coralBeds[] = { {20, 1.8f, 35}, {-24, 1.5f, 63}, {27, 2.8f, 110},
        {-32, 2.3f, 132}, {43, 2.3f, 152} };
    for (UINT bed = 0; bed < _countof(coralBeds); ++bed) {
        const auto& center = coralBeds[bed];
        const float variant = bed % 2 == 0 ? 0.05f : 0.92f;
        for (UINT coral = 0; coral < 8; ++coral) {
            const float x = center.x + random.Range(-3.5f, 3.5f), z = center.z + random.Range(-5, 5);
            const float size = center.y * random.Range(0.7f, 1.3f);
            const Vector3 base{ x, BankHeight(x, z) - 0.08f, z };
            const Vector3 trunk = base + Vector3{ 0.12f * size, size, 0.0f };
            coralBranch(base, trunk, size * 0.095f, variant, 0.0f);
            for (UINT branch = 0; branch < 5; ++branch) {
                const float angle = random.Range(0, kTau);
                const Vector3 origin = base + (trunk - base) * random.Range(0.35f, 0.85f);
                const Vector3 end = origin + Vector3{ std::cos(angle) * size * 0.55f,
                    size * random.Range(0.5f, 1.0f), std::sin(angle) * size * 0.55f };
                coralBranch(origin, end, size * 0.052f, variant, 0.25f);
                coralBranch(origin + (end - origin) * 0.6f,
                    end + Vector3{ std::sin(angle) * size * 0.3f, size * 0.32f, std::cos(angle) * size * 0.3f },
                    size * 0.032f, variant, 0.6f);
            }
        }
    }

    // Small soft colonies add a second, round silhouette at the edges of the
    // three warm meadows. These deformable plants never enter the static shadow
    // or collision mesh, and do not extend the playable area's boundary.
    const auto softPolyp = [&](float x, float z, float height, float width, float palette) {
        constexpr UINT sides = 7, rings = 6;
        constexpr float elevations[rings] = { 0.0f, 0.28f, 0.58f, 0.83f, 0.96f, 1.0f };
        constexpr float radii[rings] = { 0.80f, 0.96f, 1.0f, 0.95f, 0.70f, 0.27f };
        const UINT start = static_cast<UINT>(vertices.size());
        const size_t firstIndex = indices.size();
        const float angle = random.Range(0.0f, kTau), phase = random.Range(0.0f, kTau);
        const Vector3 lean{ std::cos(angle) * height * 0.16f, 0.0f, std::sin(angle) * height * 0.16f };
        const float baseY = BankHeight(x, z) - 0.04f;
        for (UINT row = 0; row < rings; ++row) {
            const float t = elevations[row];
            const Vector3 center = Vector3{ x, baseY + height * t, z } + lean * (t * t);
            for (UINT side = 0; side < sides; ++side) {
                const float theta = kTau * static_cast<float>(side) / sides;
                const Vector3 position = center + Vector3{ std::cos(theta) * width * radii[row],
                    0.0f, std::sin(theta) * width * radii[row] };
                vertices.push_back({ position, {}, { static_cast<float>(side) / sides, t },
                    { 3.3f, palette, height * t * t * 0.65f, phase } });
            }
        }
        for (UINT row = 0; row + 1 < rings; ++row) {
            for (UINT side = 0; side < sides; ++side) {
                const UINT a = start + row * sides + side;
                const UINT b = start + row * sides + (side + 1) % sides;
                indices.insert(indices.end(), { a, a + sides, b, b, a + sides, b + sides });
            }
        }
        const UINT tip = static_cast<UINT>(vertices.size());
        vertices.push_back({ Vector3{ x, baseY + height * 1.01f, z } + lean, {}, { 0.5f, 1.0f },
            { 3.3f, palette, height * 0.65f, phase } });
        for (UINT side = 0; side < sides; ++side)
            indices.insert(indices.end(), { start + (rings - 1) * sides + side, tip,
                start + (rings - 1) * sides + (side + 1) % sides });
        normalsFrom(start, firstIndex);
    };
    const Vector3 softBeds[] = { { 22.0f, 0.9f, 31.0f }, { -23.0f, 1.15f, 54.0f }, { 26.0f, 1.25f, 84.0f } };
    for (UINT bed = 0; bed < _countof(softBeds); ++bed) {
        const auto& center = softBeds[bed];
        for (UINT colony = 0; colony < 11; ++colony) {
            const float x = center.x + random.Range(-2.3f, 2.3f);
            const float z = center.z + random.Range(-3.8f, 3.8f);
            const float palette = (bed == 1 ? 0.68f : 0.15f) + random.Range(-0.06f, 0.06f);
            for (UINT stalk = 0; stalk < 5; ++stalk) {
                const float height = center.y * random.Range(0.50f, 1.30f);
                softPolyp(x + random.Range(-0.50f, 0.50f), z + random.Range(-0.50f, 0.50f),
                    height, random.Range(0.07f, 0.13f), palette);
            }
        }
    }

    vertexCount_ = static_cast<UINT>(vertices.size());
    indexCount_ = static_cast<UINT>(indices.size());
    const UINT vertexBytes = vertexCount_ * static_cast<UINT>(sizeof(VertexData));
    const UINT indexBytes = indexCount_ * static_cast<UINT>(sizeof(UINT));
    vertexResource_ = dx_->CreateBufferResource(vertexBytes);
    indexResource_ = dx_->CreateBufferResource(indexBytes);
    void* mapped = nullptr;
    HRESULT hr = vertexResource_->Map(0, nullptr, &mapped);
    assert(SUCCEEDED(hr));
    std::memcpy(mapped, vertices.data(), vertexBytes);
    vertexResource_->Unmap(0, nullptr);
    hr = indexResource_->Map(0, nullptr, &mapped);
    assert(SUCCEEDED(hr));
    std::memcpy(mapped, indices.data(), indexBytes);
    indexResource_->Unmap(0, nullptr);
    vertexBufferView_ = { vertexResource_->GetGPUVirtualAddress(), vertexBytes, sizeof(VertexData) };
    indexBufferView_ = { indexResource_->GetGPUVirtualAddress(), indexBytes, DXGI_FORMAT_R32_UINT };
}
