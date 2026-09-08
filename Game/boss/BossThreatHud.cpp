#include "BossThreatHud.h"
#include "BossThreatHudMotion.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "SceneColorFormat.h"
#include <xaudio2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"xaudio2.lib")

namespace {
using Microsoft::WRL::ComPtr;
using namespace BossAttackGuidance;
constexpr float kPi=3.14159265359f;
constexpr size_t kVertexCapacity=256; // At most three compact indicators, no text mesh.
struct ThreatHudVertex {
    Vector2 position, local;
    Vector4 color;
    Vector4 shape; // kind, radius, stroke width, arc fraction
};
void Check(HRESULT hr,const char* what) {if(FAILED(hr)) throw std::runtime_error(what);}
class Sonar {
    ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master=nullptr;
    std::array<IXAudio2SourceVoice*,4> voices{};
    std::array<std::vector<int16_t>,3> tones;
    size_t next=0;
public:
    ~Sonar() {Silence();for(auto* v:voices) if(v) v->DestroyVoice();if(master) master->DestroyVoice();}
    void Initialize() {
        if(FAILED(XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR))) return;
        if(FAILED(engine->CreateMasteringVoice(&master,2,48000))) return;
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=48000;
        format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=96000;
        for(auto& voice:voices) if(FAILED(engine->CreateSourceVoice(&voice,&format))) voice=nullptr;
        for(int kind=0;kind<3;++kind) {
            auto& samples=tones[kind];samples.resize(9600);
            double phase=0;
            for(size_t i=0;i<samples.size();++i) {
                const float t=static_cast<float>(i)/48000;
                const float envelope=std::sin(kPi*std::min(1.0f,t/.008f)*.5f)*std::exp(-t*24);
                phase+=2*kPi*((kind==1?1060:kind==2?460:730)+t*280)/48000;
                samples[i]=static_cast<int16_t>(std::sin(phase)*envelope*9000);
            }
        }
    }
    void Silence() {for(auto* voice:voices) if(voice) {voice->Stop();voice->FlushSourceBuffers();}}
    void Play(float pan,float height,bool locked) {
        if(!master) return;
        auto* voice=voices[next++%voices.size()];if(!voice) return;
        const auto& samples=tones[height>4?1:height<-4?2:0];if(samples.empty()) return;
        voice->Stop();voice->FlushSourceBuffers();
        const float angle=(std::clamp(pan,-1.0f,1.0f)+1)*kPi*.25f;
        const float gains[]{std::cos(angle),std::sin(angle)};
        voice->SetOutputMatrix(master,1,2,gains);
        voice->SetVolume(locked?.48f:.32f);
        voice->SetFrequencyRatio(locked?1.18f:1.0f);
        XAUDIO2_BUFFER buffer{};buffer.AudioBytes=static_cast<UINT32>(samples.size()*sizeof(int16_t));
        buffer.pAudioData=reinterpret_cast<const BYTE*>(samples.data());buffer.Flags=XAUDIO2_END_OF_STREAM;
        if(SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) voice->Start();
    }
};
}

struct BossThreatHud::Impl {
    DirectXCommon* dx=nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> displayPso,hdrPso;
    ComPtr<ID3D12Resource> buffer;
    ThreatHudVertex* mapped=nullptr;
    std::vector<ThreatHudVertex> vertices;
    Sonar sonar;
    uint64_t lastCue=0;
    float lastCueTime=-10, lastVisualTime=-10;
    struct VisualCue {uint64_t id=0;Phase phase=Phase::Tracking;float began=0,seen=-10;bool offscreen=false;};
    std::array<VisualCue,12> visualCues{};
    float AnimationAge(const Threat& threat,bool offscreen,float time) {
        VisualCue* cue=nullptr;
        for(auto& candidate:visualCues) if(candidate.id==threat.id&&candidate.seen>=0) {cue=&candidate;break;}
        if(!cue) cue=&*std::min_element(visualCues.begin(),visualCues.end(),
            [](const VisualCue& a,const VisualCue& b){return a.seen<b.seen;});
        if(cue->id!=threat.id||cue->phase!=threat.phase||time-cue->seen>.35f||(!cue->offscreen&&offscreen))
            cue->began=time;
        cue->id=threat.id;cue->phase=threat.phase;cue->offscreen=offscreen;cue->seen=time;
        return std::max(0.0f,time-cue->began);
    }
    // Each mark is a padded quad. The shader evaluates its actual curved edge
    // in pixel space, including derivative-based coverage and a narrow halo.
    void Mark(Vector2 center,float radius,float stroke,float fraction,float kind,
        Vector4 color,Vector2 direction={1,0}) {
        if(vertices.size()+6>kVertexCapacity) return;
        const float extent=radius+(kind>=2?14.0f:9.0f);
        const Vector4 shape{kind,radius,stroke,fraction};
        const auto vertex=[&](float x,float y) {
            const Vector2 local{x*extent,y*extent};
            return ThreatHudVertex{{center.x+local.x*direction.x-local.y*direction.y,
                center.y+local.x*direction.y+local.y*direction.x},local,color,shape};
        };
        vertices.insert(vertices.end(),{vertex(-1,-1),vertex(1,-1),vertex(-1,1),
            vertex(1,-1),vertex(1,1),vertex(-1,1)});
    }

};
BossThreatHud::BossThreatHud():impl_(std::make_unique<Impl>()) {}
BossThreatHud::~BossThreatHud()=default;
void BossThreatHud::Initialize(DirectXCommon* dx) {
    auto& e=*impl_;e.dx=dx;e.vertices.reserve(kVertexCapacity);
    D3D12_ROOT_SIGNATURE_DESC signature{};signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize threat HUD root");
    Check(dx->GetDevice()->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&e.root)),"Create threat HUD root");
    const auto vs=dx->CompilesSharder(L"resources/shaders/BossThreatHud.VS.hlsl",L"vs_6_0");
    const auto ps=dx->CompilesSharder(L"resources/shaders/BossThreatHud.PS.hlsl",L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC input[]{
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=e.root.Get();
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.InputLayout={input,4};pso.SampleMask=UINT_MAX;pso.SampleDesc.Count=1;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable=TRUE;pso.DepthStencilState.DepthEnable=FALSE;
    pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    auto& blend=pso.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
    blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;pso.NumRenderTargets=1;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.RTVFormats[0]=kDisplayColorFormat;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&e.displayPso)),"Create display threat HUD");
    pso.RTVFormats[0]=kSceneColorFormat;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&e.hdrPso)),"Create HDR threat HUD");
    e.buffer=dx->CreateBufferResource(kVertexCapacity*sizeof(ThreatHudVertex));
    Check(e.buffer->Map(0,nullptr,reinterpret_cast<void**>(&e.mapped)),"Map threat HUD");
    e.sonar.Initialize();
}
void BossThreatHud::Silence() {impl_->sonar.Silence();}
void BossThreatHud::Reset() {Silence();impl_->vertices.clear();impl_->lastCue=0;impl_->lastCueTime=-10;impl_->lastVisualTime=-10;impl_->visualCues={};}
size_t BossThreatHud::GetVertexCount() const {return impl_->vertices.size();}
void BossThreatHud::Draw(std::span<const Threat> threats,const Camera& camera,const Vector3& player,float time,bool playing) {
    auto& e=*impl_;e.vertices.clear();if(!e.dx||!Finite(player)||!std::isfinite(time)) return;
    if(time<e.lastCueTime) e.lastCueTime=-10;
    if(time<e.lastVisualTime) e.visualCues={};
    e.lastVisualTime=time;
    size_t shown=0;
    std::array<Vector2,3> edgePositions{};
    size_t edges=0;
    for(const auto& threat:threats) {
        if(shown>=3) break;
        const auto source=Project(threat.source,camera.GetViewMatrix(),camera.GetProjectionMatrix());
        const auto target=Project(threat.target,camera.GetViewMatrix(),camera.GetProjectionMatrix());
        if(!source.valid||!target.valid) continue;
        const bool tracking=threat.phase==Phase::Tracking,active=threat.phase==Phase::Active;
        // Keep the phase colors while moving from opaque labels to translucent
        // light. Countdown is the remaining arc; the open center stays clear.
        const Vector4 color=tracking?Vector4{.30f,.86f,1,.84f}:
            active?Vector4{1,.38f,.22f,.90f}:Vector4{1,.73f,.30f,.90f};
        const float height=threat.source.y-player.y;
        const float remaining=std::isfinite(threat.remaining)?std::max(0.0f,threat.remaining):0;
        const float fraction=active?1.0f:std::clamp(remaining/std::max(.01f,threat.total),0.0f,1.0f);
        const auto& bearing=source.offscreen?source:target;
        const float age=e.AnimationAge(threat,bearing.offscreen,time);
        if(bearing.offscreen) {
            const auto motion=BossThreatHudMotion::Evaluate(threat.phase,remaining,age);
            const float radius=22*motion.scale;
            const Vector4 edgeColor=tracking?Vector4{.025f,.75f,1,motion.alpha}:
                active?Vector4{1,.105f,.025f,motion.alpha}:Vector4{1,.48f,.025f,motion.alpha};
            auto p=bearing.position;
            // Nearby bearings stack inward rather than drawing over each other.
            // The chevron still points along the original camera-space bearing.
            for(size_t attempt=0;attempt<edges;++attempt) {
                bool overlaps=false;
                for(size_t i=0;i<edges;++i) overlaps|=std::hypot(p.x-edgePositions[i].x,p.y-edgePositions[i].y)<62;
                if(!overlaps) break;
                const float dx=640-bearing.position.x,dy=360-bearing.position.y;
                const float n=std::max(1.0f,std::hypot(dx,dy));
                p.x+=dx/n*62;p.y+=dy/n*62;
            }
            edgePositions[edges++]=p;
            // A shrinking light wave directs attention inward while the
            // original circle/chevron remains visible throughout the pulse.
            if(motion.waveAlpha>.001f) e.Mark(p,radius+motion.waveOffset,1.25f,1,2,
                {edgeColor.x,edgeColor.y,edgeColor.z,motion.waveAlpha});
            e.Mark(p,radius,1.45f,1,0,{.04f,.11f,.16f,.64f});
            if(fraction>.001f) e.Mark(p,radius,motion.stroke,fraction,2,edgeColor);
            e.Mark(p,8.5f*motion.scale,2.5f,1,3,edgeColor,bearing.direction);
            if(std::abs(height)>4) {
                const auto d=bearing.direction;
                // Offset to one side so this cue does not overlap a second
                // warning stacked inward along the same bearing.
                const Vector2 altitude{p.x-d.x*32-d.y*24,p.y-d.y*32+d.x*24};
                e.Mark(altitude,4.2f,1.6f,1,3,{edgeColor.x,edgeColor.y,edgeColor.z,.84f},
                    {0,height>0?-1.0f:1.0f});
            }
        }
        if(!target.offscreen) {
            // Three short smooth arcs replace the square brackets. Existing
            // world-space gel rings and beam VFX remain the main presentation.
            const float radius=tracking?16.0f:13.0f;
            for(int i=0;i<3;++i) {
                const float angle=static_cast<float>(i)*2*kPi/3;
                e.Mark(target.position,radius,1.35f,.115f+.12f*fraction,0,
                    {color.x,color.y,color.z,shown==0?.70f:.48f},{std::cos(angle),std::sin(angle)});
            }
        }
        const uint64_t cue=threat.id*4+static_cast<uint64_t>(threat.phase)+1;
        if(shown==0&&playing&&cue!=e.lastCue&&time-e.lastCueTime>=.25f) {
            e.sonar.Play(source.pan,height,!tracking);e.lastCue=cue;e.lastCueTime=time;
        }
        ++shown;
    }
    if(e.vertices.empty()) return;
    std::memcpy(e.mapped,e.vertices.data(),e.vertices.size()*sizeof(ThreatHudVertex));
    auto* command=e.dx->GetCommandList();
    command->SetGraphicsRootSignature(e.root.Get());
    command->SetPipelineState(e.dx->GetCurrentRenderTargetFormat()==kSceneColorFormat?e.hdrPso.Get():e.displayPso.Get());
    D3D12_VERTEX_BUFFER_VIEW view{e.buffer->GetGPUVirtualAddress(),static_cast<UINT>(e.vertices.size()*sizeof(ThreatHudVertex)),sizeof(ThreatHudVertex)};
    command->IASetVertexBuffers(0,1,&view);command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command->DrawInstanced(static_cast<UINT>(e.vertices.size()),1,0,0);
}
