#include "SeabedDetailRenderer.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
constexpr float kTau = 6.28318530718f;
constexpr float kTileSpacing = 384.0f;
// The generator's conservative horizontal bound plus the shader's fade end.
constexpr float kTileCullDistance = 173.0f + 210.0f;

// Private seed keeps the environment reproducible without changing gameplay RNG.
class DetailRandom {
public:
    float Next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ & 0x00ffffffu) / 16777216.0f;
    }
    float Range(float low, float high) { return low + (high - low) * Next(); }
private:
    uint32_t state_ = 0x6d3a28e1u;
};

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
}

Vector3 Unit(const Vector3& v) {
    const float squaredLength = v.x * v.x + v.y * v.y + v.z * v.z;
    return squaredLength > 0.000001f ? v * (1.0f / std::sqrt(squaredLength)) :
        Vector3{ 0.0f, 1.0f, 0.0f };
}
}

void SeabedDetailRenderer::Initialize(DirectXCommon* dx, Camera* camera) {
    assert(dx && camera);
    dx_ = dx;
    camera_ = camera;
    CreatePipeline_();
    CreateGeometry_();
    static_assert(sizeof(VertexData) == 48);
    static_assert(sizeof(FrameData) == 240);
    static_assert(offsetof(FrameData, towardSun) == 64);
    static_assert(offsetof(FrameData, floorHeight) == 92);
    static_assert(offsetof(FrameData, tiles) == 96);
    frameResource_ = dx_->CreateBufferResource(256);
    const HRESULT hr = frameResource_->Map(0, nullptr,
        reinterpret_cast<void**>(&frameData_));
    assert(SUCCEEDED(hr));
    Update(0.0f, floorHeight_, towardSun_);
}

void SeabedDetailRenderer::Update(float dt, float floorHeight, const Vector3& towardSun) {
    time_ = std::fmod(time_ + std::max(dt, 0.0f), 4096.0f);
    floorHeight_ = floorHeight;
    towardSun_ = Unit(towardSun);
}

void SeabedDetailRenderer::Draw() const {
    if (!enabled_ || !dx_ || !camera_ || !frameData_) {
        return;
    }
    // A paused game can still move its debug camera; animation time stays unchanged.
    FrameData frame{};
    frame.viewProjection = camera_->GetViewProjectionMatrix();
    frame.towardSun = towardSun_;
    frame.time = time_;
    frame.cameraPosition = camera_->GetTranslate();
    frame.floorHeight = floorHeight_;
    const int32_t cameraCellX = static_cast<int32_t>(std::floor(frame.cameraPosition.x / kTileSpacing));
    const int32_t cameraCellZ = static_cast<int32_t>(std::floor(frame.cameraPosition.z / kTileSpacing));
    UINT instanceCount = 0;
    for (int32_t z = -1; z <= 1; ++z) {
        for (int32_t x = -1; x <= 1; ++x) {
            const int32_t cellX = cameraCellX + x;
            const int32_t cellZ = cameraCellZ + z;
            const float worldX = static_cast<float>(cellX) * kTileSpacing;
            const float worldZ = static_cast<float>(cellZ) * kTileSpacing;
            const float deltaX = worldX - frame.cameraPosition.x;
            const float deltaZ = worldZ - frame.cameraPosition.z;
            if (deltaX * deltaX + deltaZ * deltaZ > kTileCullDistance * kTileCullDistance) {
                continue;
            }
            // Identical unsigned hash to the original VS, including negative cells.
            uint32_t tileHash = static_cast<uint32_t>(cellX) * 0x8da6b343u ^
                static_cast<uint32_t>(cellZ) * 0xd8163841u;
            tileHash ^= tileHash >> 16;
            frame.tiles[instanceCount++] = { worldX, worldZ,
                static_cast<float>(tileHash & 3u), 0.0f };
        }
    }
    if (instanceCount == 0) {
        return;
    }
    *frameData_ = frame;
    ID3D12GraphicsCommandList* commandList = dx_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, frameResource_->GetGPUVirtualAddress());
    // Reuse the mesh only for tiles that can survive the distance fade.
    commandList->DrawIndexedInstanced(indexCount_, instanceCount, 0, 0, 0);
}

void SeabedDetailRenderer::CreatePipeline_() {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &parameter;
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signatureBlob, &errorBlob);
    if (errorBlob) {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    const Microsoft::WRL::ComPtr<IDxcBlob> vertexShader = dx_->CompilesSharder(
        L"resources/shaders/SeabedDetail.VS.hlsl", L"vs_6_0");
    const Microsoft::WRL::ComPtr<IDxcBlob> pixelShader = dx_->CompilesSharder(
        L"resources/shaders/SeabedDetail.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[4]{};
    inputs[0].SemanticName = "POSITION";
    inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[1].SemanticName = "NORMAL";
    inputs[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[2].SemanticName = "TEXCOORD";
    inputs[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputs[3].SemanticName = "TEXCOORD";
    inputs[3].SemanticIndex = 1;
    inputs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    for (auto& input : inputs) {
        input.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.InputLayout = { inputs, _countof(inputs) };
    desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = dx_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}

void SeabedDetailRenderer::CreateGeometry_() {
    std::vector<VertexData> vertices;
    std::vector<UINT> indices;
    vertices.reserve(40000);
    indices.reserve(120000);
    DetailRandom random;

    const auto addRock = [&](float x, float z, float width, float depth, float height) {
        constexpr UINT sides = 12;
        constexpr UINT rings = 7;
        constexpr float radius[rings] = { 0.83f, 1.0f, 0.98f, 0.82f, 0.65f, 0.40f, 0.09f };
        constexpr float elevation[rings] = { -0.18f, 0.04f, 0.24f, 0.48f, 0.69f, 0.88f, 1.0f };
        const float rotation = random.Range(0.0f, kTau);
        const float phaseA = random.Range(0.0f, kTau);
        const float phaseB = random.Range(0.0f, kTau);
        const float leanX = random.Range(-0.25f, 0.25f) * width;
        const float leanZ = random.Range(-0.25f, 0.25f) * depth;
        const float variant = random.Next();
        const UINT start = static_cast<UINT>(vertices.size());
        const size_t firstIndex = indices.size();
        for (UINT row = 0; row < rings; ++row) {
            for (UINT side = 0; side < sides; ++side) {
                const float angle = kTau * static_cast<float>(side) / sides;
                const float lobes = 1.0f + 0.18f * std::sin(angle * 3.0f + phaseA) +
                    0.11f * std::sin(angle * 5.0f + phaseB + elevation[row] * 2.1f);
                const float localX = std::cos(angle) * radius[row] * lobes * width;
                const float localZ = std::sin(angle) * radius[row] * lobes * depth;
                const float relief = (0.07f * std::sin(angle * 2.0f + phaseB) +
                    0.035f * std::sin(angle * 5.0f + phaseA)) * radius[row];
                const Vector3 position{
                    x + localX * std::cos(rotation) - localZ * std::sin(rotation) + leanX * elevation[row],
                    height * (elevation[row] + relief) - 0.12f,
                    z + localX * std::sin(rotation) + localZ * std::cos(rotation) + leanZ * elevation[row] };
                vertices.push_back({ position, {}, { static_cast<float>(side) / sides, elevation[row] },
                    { 0.0f, variant, 0.0f, 0.0f } });
            }
        }
        for (UINT row = 0; row + 1 < rings; ++row) {
            for (UINT side = 0; side < sides; ++side) {
                const UINT a = start + row * sides + side;
                const UINT b = start + row * sides + (side + 1) % sides;
                indices.insert(indices.end(), { a, a + sides, b, b, a + sides, b + sides });
            }
        }
        const UINT top = static_cast<UINT>(vertices.size());
        vertices.push_back({ { x + leanX, height * 1.045f - 0.12f, z + leanZ }, {},
            { 0.5f, 1.0f }, { 0.0f, variant, 0.0f, 0.0f } });
        for (UINT side = 0; side < sides; ++side) {
            indices.insert(indices.end(), { start + (rings - 1) * sides + side, top,
                start + (rings - 1) * sides + (side + 1) % sides });
        }
        // Area-weighted normals keep broad rock forms smooth without sphere silhouettes.
        for (size_t i = firstIndex; i < indices.size(); i += 3) {
            VertexData& a = vertices[indices[i]];
            VertexData& b = vertices[indices[i + 1]];
            VertexData& c = vertices[indices[i + 2]];
            const Vector3 normal = Cross(b.position - a.position, c.position - a.position);
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (size_t i = start; i < vertices.size(); ++i) {
            vertices[i].normal = Unit(vertices[i].normal);
        }
    };

    const auto addGrassClump = [&](float x, float z, float size) {
        constexpr UINT segments = 5;
        const UINT blades = 7 + static_cast<UINT>(random.Next() * 5.0f);
        for (UINT blade = 0; blade < blades; ++blade) {
            const float angle = random.Range(0.0f, kTau);
            const float height = size * random.Range(0.85f, 1.8f);
            const float halfWidth = random.Range(0.065f, 0.135f) * size;
            const float baseX = x + random.Range(-0.65f, 0.65f) * size;
            const float baseZ = z + random.Range(-0.65f, 0.65f) * size;
            const float bend = random.Range(0.15f, 0.50f) * height;
            const float twist = random.Range(-0.8f, 0.8f);
            const float phase = random.Range(0.0f, kTau);
            const float variant = random.Next();
            const UINT start = static_cast<UINT>(vertices.size());
            for (UINT row = 0; row <= segments; ++row) {
                const float t = static_cast<float>(row) / segments;
                const float orientation = angle + twist * t;
                const Vector3 sideDirection{ std::cos(orientation), 0.0f, std::sin(orientation) };
                const Vector3 normal{ -std::sin(orientation), 0.0f, std::cos(orientation) };
                const float width = halfWidth * (0.25f + 1.5f * std::sqrt(t)) * (1.0f - t) +
                    halfWidth * 0.015f;
                const Vector3 center{ baseX - std::sin(angle) * bend * t * t,
                    height * t - 0.08f, baseZ + std::cos(angle) * bend * t * t };
                for (UINT column = 0; column < 3; ++column) {
                    const float across = static_cast<float>(column) - 1.0f;
                    const Vector3 position = center + sideDirection * (across * width) +
                        normal * ((column == 1 ? 0.18f : 0.0f) * width);
                    const Vector3 bladeNormal = Unit(normal + sideDirection * (across * 0.25f) +
                        Vector3{ 0.0f, -2.0f * bend * t / height, 0.0f });
                    vertices.push_back({ position, bladeNormal, { across * 0.5f + 0.5f, t },
                        { 1.0f, variant, height * t * t, phase } });
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

    // Irregular, disconnected groups leave sand channels instead of a visible ring.
    for (UINT cluster = 0; cluster < 26; ++cluster) {
        const bool distant = cluster >= 12;
        const float angle = static_cast<float>(cluster) * 2.39996323f + random.Range(-0.24f, 0.24f);
        const float distance = distant ? random.Range(86.0f, 146.0f) : random.Range(35.0f, 78.0f);
        const float x = std::sin(angle) * distance;
        const float z = std::cos(angle) * distance;
        const float scale = distant ? random.Range(4.5f, 8.0f) : random.Range(1.6f, 3.4f);
        const UINT rocks = 3 + static_cast<UINT>(random.Next() * 3.0f);
        for (UINT rock = 0; rock < rocks; ++rock) {
            const float partScale = rock == 0 ? 1.0f : random.Range(0.30f, 0.75f);
            addRock(x + random.Range(-0.9f, 0.9f) * scale,
                z + random.Range(-0.9f, 0.9f) * scale,
                scale * partScale * random.Range(0.8f, 1.3f),
                scale * partScale * random.Range(0.65f, 1.25f),
                scale * partScale * random.Range(0.7f, distant ? 1.65f : 1.1f));
        }
        for (UINT clump = 0; clump < 4; ++clump) {
            const float clumpAngle = random.Range(0.0f, kTau);
            const float offset = scale * random.Range(1.05f, 1.8f);
            addGrassClump(x + std::cos(clumpAngle) * offset,
                z + std::sin(clumpAngle) * offset, random.Range(0.9f, 1.9f));
        }
    }
    // Smaller isolated meadows give a readable foreground when looking down.
    for (UINT patch = 0; patch < 20; ++patch) {
        const float angle = random.Range(0.0f, kTau);
        const float distance = random.Range(26.0f, 65.0f);
        addGrassClump(std::sin(angle) * distance, std::cos(angle) * distance,
            random.Range(0.8f, 1.5f));
    }

    indexCount_ = static_cast<UINT>(indices.size());
    const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(VertexData));
    const UINT indexBytes = static_cast<UINT>(indices.size() * sizeof(UINT));
    vertexResource_ = dx_->CreateBufferResource(vertexBytes);
    void* vertexData = nullptr;
    HRESULT hr = vertexResource_->Map(0, nullptr, &vertexData);
    assert(SUCCEEDED(hr));
    std::memcpy(vertexData, vertices.data(), vertexBytes);
    vertexResource_->Unmap(0, nullptr);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = vertexBytes;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    indexResource_ = dx_->CreateBufferResource(indexBytes);
    void* indexData = nullptr;
    hr = indexResource_->Map(0, nullptr, &indexData);
    assert(SUCCEEDED(hr));
    std::memcpy(indexData, indices.data(), indexBytes);
    indexResource_->Unmap(0, nullptr);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = indexBytes;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}
