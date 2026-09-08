#include "BossWaterEffectRenderer.h"
#include "WorldEffectsFog.h"
#include "MineBloom.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "SceneColorFormat.h"
#include "SrvManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
constexpr size_t kMaxDraws = 1024, kMaxVertices = kMaxDraws * 24;
constexpr size_t kConstantStride = 256;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Finite(const Vector4& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w); }
float Safe(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
}
Vector3 Unit(const Vector3& v, const Vector3& fallback={0,1,0}) {
    const float squared=Dot(v,v);
    return std::isfinite(squared)&&squared>0.000001f ? v*(1.0f/std::sqrt(squared)) : fallback;
}
Vector4 Pack(const Vector3& v, float w) { return {v.x,v.y,v.z,w}; }
void Check(HRESULT hr, const char* message) { if(FAILED(hr)) throw std::runtime_error(message); }
struct WaterVertex { Vector3 position,normal; Vector2 uv; };
struct FrameConstants {
    Matrix4x4 viewProjection;
    Vector4 cameraPositionTime,cameraRightRefraction,cameraUpEmission,viewportStyle;
    WorldEffectsFog::Parameters worldEffectsFog;
};
struct PrimitiveConstants { Vector4 colorOpacity,style; };
static_assert(sizeof(WaterVertex)==32 && sizeof(FrameConstants)==240 && sizeof(PrimitiveConstants)==32);
struct DrawItem { PrimitiveConstants constants; UINT first=0,count=0; float distance=0; };
}

struct BossWaterEffectRenderer::Impl {
    DirectXCommon* dx=nullptr;
    SrvManager* srv=nullptr;
    Camera* camera=nullptr;
    float time=0;
    bool finite=true;
    std::vector<WaterVertex> geometry;
    std::vector<DrawItem> draws;
    MineBloom bloom;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12Resource> vertices,frameBuffer,drawBuffer,colorCopy,depthCopy;
    ComPtr<ID3D12DescriptorHeap> copyHeap;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    FrameConstants* frame=nullptr;
    WaterVertex* vertexData=nullptr;
    unsigned char* drawData=nullptr;
    UINT descriptorSize=0;

    void CreatePipeline();
    bool Capture(ID3D12Resource* color, ID3D12Resource* depth);
    bool AddDraw(UINT first, UINT count, const Vector3& center, const Vector4& tint,
        float kind, float intensity, float phase=0, bool closed=false);
    void Ribbon(std::span<const Point> path, const Vector4& tint, float intensity, float phase);
    void Billboard(const Vector3& center, float radius, const Vector4& tint, float kind,
        float intensity, const Vector3& velocity);
};

BossWaterEffectRenderer::BossWaterEffectRenderer() : impl_(std::make_unique<Impl>()) {}
BossWaterEffectRenderer::~BossWaterEffectRenderer()=default;

void BossWaterEffectRenderer::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    if(!dx||!srv||!camera) throw std::invalid_argument("BossWaterEffectRenderer needs device, heap and camera");
    auto& e=*impl_; e.dx=dx; e.srv=srv; e.camera=camera;
    e.bloom.Initialize(dx,srv); e.CreatePipeline();
    e.vertices=dx->CreateBufferResource(kMaxVertices*sizeof(WaterVertex));
    e.frameBuffer=dx->CreateBufferResource(kConstantStride);
    e.drawBuffer=dx->CreateBufferResource(kConstantStride*kMaxDraws);
    Check(e.vertices->Map(0,nullptr,reinterpret_cast<void**>(&e.vertexData)),"Map Boss water geometry");
    Check(e.frameBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.frame)),"Map Boss water frame constants");
    Check(e.drawBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.drawData)),"Map Boss water draw constants");
    e.vertexView={e.vertices->GetGPUVirtualAddress(),static_cast<UINT>(kMaxVertices*sizeof(WaterVertex)),sizeof(WaterVertex)};
    e.copyHeap=dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,true);
    e.descriptorSize=dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    e.geometry.reserve(kMaxVertices); e.draws.reserve(kMaxDraws);
    Clear();
}
void BossWaterEffectRenderer::Clear() {
    auto& e=*impl_; e.time=0; e.finite=true; e.geometry.clear(); e.draws.clear();
}
void BossWaterEffectRenderer::Begin(float time) {
    Clear(); impl_->time=Safe(time,0,0,3600);
}
void BossWaterEffectRenderer::Ribbon(std::span<const Point> path, const Vector4& tint,
    float intensity, float phase) {
    auto& e=*impl_;
    if(!e.camera || !Finite(e.camera->GetTranslate())) return;
    e.Ribbon(path,tint,intensity,phase);
}
void BossWaterEffectRenderer::Billboard(const Vector3& center, float radius, const Vector4& tint,
    Particle kind, float intensity, const Vector3& velocity) {
    auto& e=*impl_;
    if(!e.camera || !Finite(e.camera->GetTranslate())) return;
    e.Billboard(center,radius,tint,static_cast<float>(kind),intensity,velocity);
}
BossWaterEffectRenderer::Stats BossWaterEffectRenderer::GetStats() const {
    const auto& e=*impl_; return {e.draws.size(),e.geometry.size(),e.finite};
}

bool BossWaterEffectRenderer::Impl::AddDraw(UINT first, UINT count, const Vector3& center, const Vector4& tint,
    float kind, float intensity, float phase, bool closed) {
    if(draws.size()>=kMaxDraws||!Finite(center)||!Finite(tint)||!std::isfinite(intensity)||
        !std::isfinite(phase)||intensity<=0.00001f||tint.w<=0.00001f) return false;
    const Vector3 delta=center-camera->GetTranslate();
    const float distance=Dot(delta,delta);
    if(!std::isfinite(distance)) return false;
    draws.push_back({{tint,{kind,intensity,closed?1.0f:0.0f,phase}},first,count,distance});
    return true;
}
void BossWaterEffectRenderer::Impl::Ribbon(std::span<const Point> path, const Vector4& tint,
    float intensity, float phase) {
    if(path.size()<2||path.size()>513||intensity<=0.00001f||tint.w<=0.00001f) return;
    // Chunks are sorted separately at intersections. Each ribbon is a single
    // two-sided sheet: moving inside the current cannot double its transmission.
    std::vector<WaterVertex> left(path.size()),right(path.size());
    for(size_t i=0;i<path.size();++i) {
        if(!Finite(path[i].position)||!std::isfinite(path[i].width)) {finite=false;return;}
        const auto tangent=Unit(path[std::min(i+1,path.size()-1)].position-path[i?i-1:0].position);
        const auto toCamera=Unit(camera->GetTranslate()-path[i].position,{0,0,-1});
        const auto side=Unit(Cross(tangent,toCamera),Unit(Cross(tangent,{0,0,1}),{1,0,0}));
        const auto normal=Unit(Cross(side,tangent),toCamera);
        const float width=std::clamp(path[i].width,0.001f,2.0f);
        Vector3 a=path[i].position-side*width,b=path[i].position+side*width;
        const float v=static_cast<float>(i)/static_cast<float>(path.size()-1);
        if(!Finite(a)||!Finite(b)||!Finite(normal)) {finite=false;return;}
        left[i]={a,normal,{0,v}};right[i]={b,normal,{1,v}};
    }
    const auto seam=path.front().position-path.back().position;
    const bool closed=path.size()>3 && Dot(seam,seam)<0.0001f;
    for(size_t start=0;start+1<path.size();start+=4) {
        const size_t end=std::min(start+4,path.size()-1);
        const UINT count=static_cast<UINT>((end-start)*6),first=static_cast<UINT>(geometry.size());
        if(geometry.size()+count>kMaxVertices||draws.size()>=kMaxDraws) break;
        for(size_t i=start;i<end;++i) {
            geometry.push_back(left[i]);geometry.push_back(right[i]);geometry.push_back(left[i+1]);
            geometry.push_back(left[i+1]);geometry.push_back(right[i]);geometry.push_back(right[i+1]);
        }
        if(!AddDraw(first,count,path[(start+end)/2].position,tint,0,intensity,phase,closed)) geometry.resize(first);
    }
}
void BossWaterEffectRenderer::Impl::Billboard(const Vector3& center, float radius, const Vector4& tint,
    float kind, float intensity, const Vector3& velocity) {
    if(geometry.size()+6>kMaxVertices||draws.size()>=kMaxDraws||!Finite(center)||
        !Finite(velocity)||!std::isfinite(radius)||radius<=0.0001f) return;
    const auto& world=camera->GetWorldMatrix();
    Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]},up{world.m[1][0],world.m[1][1],world.m[1][2]};
    const auto facing=Unit(camera->GetTranslate()-center,{0,0,-1});
    if(Dot(velocity,velocity)>0.00001f && kind==1) {
        up=Unit(velocity-facing*Dot(velocity,facing),up);
        right=Unit(Cross(up,facing),right);
    }
    radius=std::min(radius,50.0f);
    right*=radius;up*=radius*(kind==1?2.4f:1.0f);
    const UINT first=static_cast<UINT>(geometry.size());
    const WaterVertex a{center-right-up,facing,{0,1}},b{center-right+up,facing,{0,0}},
        c{center+right-up,facing,{1,1}},d{center+right+up,facing,{1,0}};
    if(!Finite(a.position)||!Finite(b.position)||!Finite(c.position)||!Finite(d.position)) {finite=false;return;}
    geometry.insert(geometry.end(),{a,b,c,c,b,d});
    if(!AddDraw(first,6,center,tint,kind,intensity)) geometry.resize(first);
}

void BossWaterEffectRenderer::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth, const Style& style) {
    auto& e=*impl_;
    if(!e.dx||!e.camera) return;
    std::stable_sort(e.draws.begin(),e.draws.end(),[](const DrawItem& a,const DrawItem& b){return a.distance>b.distance;});
    if(e.draws.empty()||!e.bloom.Begin(sceneColor,sceneDepth)) return;
    if(!e.Capture(sceneColor,sceneDepth)) {e.bloom.Composite(0);return;}
    const auto& world=e.camera->GetWorldMatrix();
    *e.frame={e.camera->GetViewProjectionMatrix(),Pack(e.camera->GetTranslate(),e.time),
        {world.m[0][0],world.m[0][1],world.m[0][2],Safe(style.refraction,4,0,10)},
        {world.m[1][0],world.m[1][1],world.m[1][2],Safe(style.glow,1,0,2.5f)},
        {static_cast<float>(sceneColor->GetDesc().Width),static_cast<float>(sceneColor->GetDesc().Height),Safe(style.opacity,1,0,1.5f),0},WorldEffectsFog::GetParameters()};
    std::memcpy(e.vertexData,e.geometry.data(),e.geometry.size()*sizeof(WaterVertex));
    auto* command=e.dx->GetCommandList();
    command->SetGraphicsRootSignature(e.root.Get());
    command->SetPipelineState(e.pso.Get());
    ID3D12DescriptorHeap* heaps[]={e.copyHeap.Get()};
    command->SetDescriptorHeaps(1,heaps);
    command->SetGraphicsRootDescriptorTable(2,e.copyHeap->GetGPUDescriptorHandleForHeapStart());
    command->SetGraphicsRootConstantBufferView(0,e.frameBuffer->GetGPUVirtualAddress());
    command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command->IASetVertexBuffers(0,1,&e.vertexView);
    for(size_t i=0;i<e.draws.size();++i) {
        const auto& draw=e.draws[i];
        // Separate immutable slices prevent later draws from overwriting earlier
        // GPU constants. The engine fences PostDraw before next-frame reuse.
        std::memcpy(e.drawData+i*kConstantStride,&draw.constants,sizeof(draw.constants));
        command->SetGraphicsRootConstantBufferView(1,e.drawBuffer->GetGPUVirtualAddress()+i*kConstantStride);
        command->DrawInstanced(draw.count,1,draw.first,0);
    }
    e.bloom.Composite(Safe(style.bloom,0.9f,0,3));
}

void BossWaterEffectRenderer::Impl::CreatePipeline() {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2; range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER parameters[3]{};
    for (UINT i=0;i<2;++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[i].Descriptor.ShaderRegister = i;
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable = {1,&range};
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3; desc.pParameters = parameters;
    desc.NumStaticSamplers = 1; desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, error;
    const HRESULT result = D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
    if(error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
    Check(result,"Serialize Boss water VFX root signature");
    Check(dx->GetDevice()->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),
        "Create Boss water VFX root signature");
    const auto vs = dx->CompilesSharder(L"resources/shaders/BossWaterEffects.VS.hlsl", L"vs_6_0");
    const auto ps = dx->CompilesSharder(L"resources/shaders/BossWaterEffects.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso{};
    descPso.pRootSignature=root.Get(); descPso.InputLayout={inputs,3};
    descPso.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; descPso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    descPso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    descPso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; // Ribbons are single sheets, visible from either side.
    descPso.RasterizerState.FrontCounterClockwise=FALSE;
    descPso.RasterizerState.DepthClipEnable=TRUE;
    auto& blend = descPso.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_ONE; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    descPso.BlendState.IndependentBlendEnable=TRUE;
    descPso.BlendState.RenderTarget[1]=blend;
    descPso.BlendState.RenderTarget[1].DestBlend=D3D12_BLEND_ONE;
    descPso.BlendState.RenderTarget[1].DestBlendAlpha=D3D12_BLEND_ONE;
    descPso.DepthStencilState.DepthEnable=TRUE; descPso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    descPso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    descPso.NumRenderTargets=2; descPso.RTVFormats[0]=descPso.RTVFormats[1]=kSceneColorFormat; descPso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    descPso.SampleDesc.Count=1; descPso.SampleMask=D3D12_DEFAULT_SAMPLE_MASK;
    descPso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&descPso,IID_PPV_ARGS(&pso)),"Create Boss water VFX PSO");
}

bool BossWaterEffectRenderer::Impl::Capture(ID3D12Resource* color, ID3D12Resource* depth) {
    if (!color || !depth) return false;
    const auto colorDesc=color->GetDesc(), depthDesc=depth->GetDesc();
    if (colorDesc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depthDesc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        colorDesc.Format!=kSceneColorFormat || depthDesc.Format!=DXGI_FORMAT_R32_TYPELESS ||
        colorDesc.Width!=depthDesc.Width || colorDesc.Height!=depthDesc.Height ||
        colorDesc.SampleDesc.Count!=1 || depthDesc.SampleDesc.Count!=1 ||
        colorDesc.MipLevels!=1 || depthDesc.MipLevels!=1 ||
        colorDesc.DepthOrArraySize!=1 || depthDesc.DepthOrArraySize!=1) return false;
    if (!colorCopy || colorCopy->GetDesc().Width!=colorDesc.Width || colorCopy->GetDesc().Height!=colorDesc.Height) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto c=colorDesc, d=depthDesc; c.Flags=d.Flags=D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> newColor, newDepth;
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&c,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newColor)),"Create Boss water VFX color snapshot");
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newDepth)),"Create Boss water VFX depth snapshot");
        colorCopy=std::move(newColor); depthCopy=std::move(newDepth);
        colorCopy->SetName(L"Boss water VFX HDR refraction snapshot");
        depthCopy->SetName(L"Boss water VFX depth snapshot");
        auto handle=copyHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels=1; view.Format=kSceneColorFormat;
        dx->GetDevice()->CreateShaderResourceView(colorCopy.Get(),&view,handle);
        handle.ptr+=descriptorSize; view.Format=DXGI_FORMAT_R32_FLOAT;
        dx->GetDevice()->CreateShaderResourceView(depthCopy.Get(),&view,handle);
    }
    auto transition = [&](ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
        dx->TransitionResource(resource,a,b);
    };
    transition(color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(colorCopy.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    transition(depthCopy.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    dx->GetCommandList()->CopyResource(colorCopy.Get(),color);
    dx->GetCommandList()->CopyResource(depthCopy.Get(),depth);
    transition(colorCopy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(depthCopy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(depth,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    return true;
}
