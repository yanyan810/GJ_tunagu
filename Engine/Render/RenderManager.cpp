#include "RenderManager.h"

#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "TextureManager.h"
#include "FrameProfiler.h"

#include <cassert>
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

static const char* kEffectNames[] = {
    "FullScreen (No Effect)",
    "Grayscale",
    "Vignette",
    "Bloom",
    "GaussianBlurX (Horizontal)",
    "GaussianBlurY (Vertical)",
    "GaussianBlur (Linear)",
    "Outline (Depth & Normal)",
    "RadialBlur",
    "Dissolve",
    "Random",
    "Outline Bloom",
    "Luminance Based Outline",
    "Luminance Outline Mask (Internal)",
    "Depth Fog",
    "Light Shaft",
};

static const wchar_t* kEffectPSPaths[] = {
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Grayscale.PS.hlsl",
    L"resources/shaders/Vignette.PS.hlsl",
    L"resources/shaders/Bloom.PS.hlsl",
    L"resources/shaders/GaussianBlurX.PS.hlsl",
    L"resources/shaders/GaussianBlurY.PS.hlsl",
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Outline.PS.hlsl",
    L"resources/shaders/RadialBlur.PS.hlsl",
    L"resources/shaders/Dissolve.PS.hlsl",
    L"resources/shaders/Random.PS.hlsl",
    L"resources/shaders/OutlineBloom.PS.hlsl",
    L"resources/shaders/LuminanceBasedOutline.PS.hlsl",
    L"resources/shaders/LuminanceOutlineMask.PS.hlsl",
    L"resources/shaders/DepthFog.PS.hlsl",
    L"resources/shaders/LightShaft.PS.hlsl",
};

void RenderManager::Initialize(DirectXCommon* dx, SrvManager* srv)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;

    offscreen_ = std::make_unique<OffscreenPass>();
    postBuffers_[0] = std::make_unique<OffscreenPass>();
    postBuffers_[1] = std::make_unique<OffscreenPass>();
    particlePostLayer_ = std::make_unique<OffscreenPass>();
    particlePostBuffer_ = std::make_unique<OffscreenPass>();
    compositeBuffer_ = std::make_unique<OffscreenPass>();
    compositeBuffer2_ = std::make_unique<OffscreenPass>();
    previewBuffer_ = std::make_unique<OffscreenPass>();
    objectPostLayer_ = std::make_unique<OffscreenPass>();
    objectPostBuffer_ = std::make_unique<OffscreenPass>();
    sceneDisplayBuffer_ = std::make_unique<OffscreenPass>();
    previewDisplayBuffer_ = std::make_unique<OffscreenPass>();

    Vector4 clearColor = { 0.08f, 0.085f, 0.09f, 1.0f };
    offscreen_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        clearColor,
        2
    );

    Vector4 postClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    postBuffers_[0]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        3
    );
    postBuffers_[1]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        4
    );
    Vector4 particleLayerClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    particlePostLayer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        particleLayerClearColor,
        5
    );
    particlePostBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        6
    );
    compositeBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        7
    );
    compositeBuffer2_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        8
    );
    previewBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        clearColor,
        9
    );
    objectPostLayer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        particleLayerClearColor,
        10
    );
    objectPostBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        kSceneColorFormat,
        postClearColor,
        11
    );

    CreateCopyImageRootSignature();

    sceneDisplayBuffer_->Initialize(dx_, srv_, WinApp::kClientWidth,
        WinApp::kClientHeight, kDisplayColorFormat, postClearColor, 12);
    previewDisplayBuffer_->Initialize(dx_, srv_, WinApp::kClientWidth,
        WinApp::kClientHeight, kDisplayColorFormat, clearColor, 13);

    gaussianFilterCB_ = dx_->CreateBufferResource((sizeof(GaussianFilterParameter) + 0xff) & ~0xff);
    gaussianFilterCB_->Map(0, nullptr, reinterpret_cast<void**>(&gaussianFilterCBData_));
    gaussianFilterCBData_->sigma = sigma_;

    outlineCB_ = dx_->CreateBufferResource((sizeof(OutlineParameter) + 0xff) & ~0xff);
    outlineCB_->Map(0, nullptr, reinterpret_cast<void**>(&outlineCBData_));
    outlineCBData_->color = outlineColor_;
    outlineCBData_->thickness = outlineThickness_;
    outlineCBData_->threshold = outlineThreshold_;

    radialBlurCB_ = dx_->CreateBufferResource((sizeof(RadialBlurParameter) + 0xff) & ~0xff);
    radialBlurCB_->Map(0, nullptr, reinterpret_cast<void**>(&radialBlurCBData_));
    radialBlurCBData_->center = radialBlurCenter_;
    radialBlurCBData_->numSamples = radialBlurNumSamples_;
    radialBlurCBData_->blurWidth = radialBlurWidth_;

    dissolveCB_ = dx_->CreateBufferResource((sizeof(DissolveParameter) + 0xff) & ~0xff);
    dissolveCB_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveCBData_));
    dissolveCBData_->edgeColor = dissolveEdgeColor_;
    dissolveCBData_->threshold = dissolveThreshold_;
    dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
    dissolveCBData_->backgroundColor = dissolveBackgroundColor_;

    randomCB_ = dx_->CreateBufferResource((sizeof(RandomParameter) + 0xff) & ~0xff);
    randomCB_->Map(0, nullptr, reinterpret_cast<void**>(&randomCBData_));
    randomCBData_->time = 0.0f;

    depthFogCB_ = dx_->CreateBufferResource((sizeof(DepthFogParameter) + 0xff) & ~0xff);
    depthFogCB_->Map(0, nullptr, reinterpret_cast<void**>(&depthFogCBData_));
    depthFogParameters_.color = depthFogColor_;
    depthFogParameters_.enabled = 1.0f;
    depthFogParameters_.startDistance = depthFogStartDistance_;
    depthFogParameters_.endDistance = depthFogEndDistance_;
    depthFogParameters_.density = depthFogDensity_;
    depthFogParameters_.maxOpacity = depthFogMaxOpacity_;
    depthFogParameters_.nearClip = 0.1f;
    depthFogParameters_.farClip = 1000.0f;
    depthFogParameters_.backgroundOpacity = depthFogBackgroundOpacity_;
    depthFogParameters_.farBackgroundBlendStartRatio =
        depthFogFarBackgroundBlendStartRatio_;
    depthFogParameters_.background = underwaterBackgroundParameters_;
    depthFogParameters_.extinctionDistanceRGB =
        depthFogExtinctionDistanceRGB_;
    depthFogParameters_.underwaterMediumEnabled =
        underwaterMediumEnabled_ ? 1.0f : 0.0f;

    lightShaftParameters_.inverseViewProjection = Matrix4x4::MakeIdentity4x4();
    lightShaftParameters_.lightUv = { 0.5f, 0.5f };
    lightShaftParameters_.lightColor = { 0.78f, 0.94f, 1.0f };
    lightShaftParameters_.density = 0.85f;
    lightShaftParameters_.numSamples = 48;
    lightShaftParameters_.decay = 0.96f;
    lightShaftParameters_.weight = 0.030f;
    lightShaftParameters_.exposure = 0.35f;
    lightShaftParameters_.nearClip = 0.1f;
    lightShaftParameters_.farClip = 1000.0f;
    lightShaftParameters_.occlusionDepthRange = 120.0f;
    lightShaftParameters_.waterSurfaceTolerance = 1.5f;
    lightShaftParameters_.sourceRadius = 0.85f;
    lightShaftParameters_.offscreenFadeDistance = 0.35f;
    lightShaftCB_ = dx_->CreateBufferResource(
        (sizeof(LightShaftParameters) + 0xff) & ~0xff);
    lightShaftCB_->Map(
        0, nullptr, reinterpret_cast<void**>(&lightShaftCBData_));
    *lightShaftCBData_ = lightShaftParameters_;

    bloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    bloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&bloomCBData_));
    bloomCBData_->color = bloomColor_;
    bloomCBData_->intensity = bloomIntensity_;
    bloomCBData_->threshold = bloomThreshold_;
    bloomCBData_->alpha = bloomAlpha_;
    bloomCBData_->_pad = 0.0f;

    objectBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    objectBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&objectBloomCBData_));
    objectBloomCBData_->color = objectLayerBloomColor_;
    objectBloomCBData_->intensity = bloomIntensity_;
    objectBloomCBData_->threshold = bloomThreshold_;
    objectBloomCBData_->alpha = bloomAlpha_;
    objectBloomCBData_->_pad = 0.0f;

    objectOutlineBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    objectOutlineBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&objectOutlineBloomCBData_));
    objectOutlineBloomCBData_->color = objectLayerOutlineBloomColor_;
    objectOutlineBloomCBData_->intensity = bloomIntensity_;
    objectOutlineBloomCBData_->threshold = bloomThreshold_;
    objectOutlineBloomCBData_->alpha = bloomAlpha_;
    objectOutlineBloomCBData_->_pad = 0.0f;

    particleBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    particleBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&particleBloomCBData_));
    particleBloomCBData_->color = particleLayerBloomColor_;
    particleBloomCBData_->intensity = bloomIntensity_;
    particleBloomCBData_->threshold = bloomThreshold_;
    particleBloomCBData_->alpha = bloomAlpha_;
    particleBloomCBData_->_pad = 0.0f;

    particleOutlineBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    particleOutlineBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&particleOutlineBloomCBData_));
    particleOutlineBloomCBData_->color = particleLayerOutlineBloomColor_;
    particleOutlineBloomCBData_->intensity = bloomIntensity_;
    particleOutlineBloomCBData_->threshold = bloomThreshold_;
    particleOutlineBloomCBData_->alpha = bloomAlpha_;
    particleOutlineBloomCBData_->_pad = 0.0f;

    TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
    noiseSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex("resources/noise0.png");

    depthSrvIndex_ = srv_->Allocate();
    srv_->CreateSRVTexture2D(depthSrvIndex_, dx_->GetDepthStencilResource(), DXGI_FORMAT_R32_FLOAT, 1);

    for (int i = 0; i < kEffectCount; ++i) {
        CreatePipelineState(kEffectPSPaths[i], pipelineStates_[i]);
    }
    CreatePipelineState(L"resources/shaders/AdditiveComposite.PS.hlsl", additiveCompositePSO_);
    CreatePipelineState(L"resources/shaders/ToneMap.PS.hlsl", toneMapPSO_, kDisplayColorFormat);
}

void RenderManager::SetClearColor(const Vector4& color)
{
    if (offscreen_) {
        offscreen_->SetClearColor(color);
    }
}

void RenderManager::SetMode(PostEffectMode mode)
{
    currentMode_ = mode;
    ClearEffects();

    if (mode != PostEffectMode::FullScreen) {
        enabledEffects_[static_cast<int>(mode)] = true;
    }
}

void RenderManager::SetEffectEnabled(PostEffectMode mode, bool enabled)
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return;
    }
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        return;
    }

    enabledEffects_[index] = enabled;
    currentMode_ = PostEffectMode::FullScreen;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (enabledEffects_[i]) {
            currentMode_ = static_cast<PostEffectMode>(i);
            break;
        }
    }
}

bool RenderManager::IsEffectEnabled(PostEffectMode mode) const
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return false;
    }
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        return false;
    }
    return enabledEffects_[index];
}

void RenderManager::ClearEffects()
{
    enabledEffects_.fill(false);
}

void RenderManager::SetRadialBlurParameters(const Vector2& center, int32_t numSamples, float blurWidth)
{
    radialBlurCenter_ = {
        std::clamp(center.x, 0.0f, 1.0f),
        std::clamp(center.y, 0.0f, 1.0f)
    };
    radialBlurNumSamples_ = std::max(1, numSamples);
    radialBlurWidth_ = std::max(0.0f, blurWidth);

    if (radialBlurCBData_) {
        radialBlurCBData_->center = radialBlurCenter_;
        radialBlurCBData_->numSamples = radialBlurNumSamples_;
        radialBlurCBData_->blurWidth = radialBlurWidth_;
    }
}

void RenderManager::SetDissolveTransition(float threshold, const Vector4& color, float edgeWidth)
{
    dissolveThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    dissolveBackgroundColor_ = color;
    dissolveEdgeColor_ = color;
    dissolveEdgeWidth_ = std::max(0.0f, edgeWidth);

    if (dissolveCBData_) {
        dissolveCBData_->threshold = dissolveThreshold_;
        dissolveCBData_->backgroundColor = dissolveBackgroundColor_;
        dissolveCBData_->edgeColor = dissolveEdgeColor_;
        dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
    }
}

void RenderManager::SetUnderwaterBackgroundParameters(
    const UnderwaterBackgroundParameters& parameters)
{
    underwaterBackgroundParameters_ = parameters;
    if (depthFogCBData_) {
        depthFogParameters_.background = underwaterBackgroundParameters_;
    }
}

void RenderManager::SetLightShaftParameters(
    const LightShaftParameters& parameters,
    D3D12_GPU_DESCRIPTOR_HANDLE transmissionTexture)
{
    lightShaftParameters_ = parameters;
    lightShaftTransmissionTexture_ = transmissionTexture;
}

void RenderManager::SetUnderwaterMediumParameters(
    const UnderwaterMediumParameters& parameters)
{
    depthFogParameters_.medium = parameters;
    auto& medium = depthFogParameters_.medium;
    medium.shaftIntensity = std::clamp(medium.shaftIntensity, 0.0f, 0.5f);
    medium.depthLightRange = std::max(medium.depthLightRange, 1.0f);
    medium.shaftScale = std::max(medium.shaftScale, 0.0001f);
    medium.shaftSamples = medium.shaftSamples <= 0
        ? 0 : std::clamp(medium.shaftSamples, 4, 12);
}

void RenderManager::SetOceanLightingParameters(
    const OceanLightingParameters& parameters,
    D3D12_GPU_DESCRIPTOR_HANDLE causticsTexture)
{
    depthFogParameters_.oceanLighting = parameters;
    auto& lighting = depthFogParameters_.oceanLighting;
    lighting.contactStrength = std::clamp(lighting.contactStrength, 0.0f, 0.6f);
    lighting.contactRadius = std::clamp(lighting.contactRadius, 0.1f, 5.0f);
    lighting.contactBias = std::max(lighting.contactBias, 0.02f);
    lighting.surfaceExclusion = std::max(lighting.surfaceExclusion, 0.1f);
    lighting.causticsDispersion = std::clamp(lighting.causticsDispersion, 0.0f, 0.012f);
    oceanCausticsTexture_ = causticsTexture;
}

void RenderManager::SetOceanShadowParameters(const OceanShadowParameters& parameters,
    D3D12_GPU_DESCRIPTOR_HANDLE shadowTexture)
{
    depthFogParameters_.oceanShadow = parameters;
    auto& settings = depthFogParameters_.oceanShadow.settings;
    settings.x = std::max(settings.x, 1.0f / 16384.0f);
    settings.y = std::max(settings.y, 0.0f);
    settings.z = std::clamp(settings.z, 0.0f, 1.0f);
    oceanShadowTexture_ = shadowTexture;
}

void RenderManager::SetExposureEV(float exposureEV)
{
    if (std::isfinite(exposureEV)) {
        exposureEV_ = std::clamp(exposureEV, -4.0f, 4.0f);
    }
}

void RenderManager::SetUnderwaterFogParameters(float startDistance,
    const Vector3& extinctionDistanceRGB, float maxOpacity)
{
    depthFogStartDistance_ = std::max(startDistance, 0.0f);
    depthFogExtinctionDistanceRGB_ = {
        std::max(extinctionDistanceRGB.x, 0.001f),
        std::max(extinctionDistanceRGB.y, 0.001f),
        std::max(extinctionDistanceRGB.z, 0.001f)
    };
    depthFogMaxOpacity_ = std::clamp(maxOpacity, 0.0f, 1.0f);
    depthFogParameters_.startDistance = depthFogStartDistance_;
    depthFogParameters_.extinctionDistanceRGB = depthFogExtinctionDistanceRGB_;
    depthFogParameters_.maxOpacity = depthFogMaxOpacity_;
}

void RenderManager::BeginOffscreen()
{
    assert(offscreen_);
    offscreenRecording_ = true;
    worldEffectsFogApplied_ = false;
    worldEffectsFogParameters_ = {};
    offscreen_->Begin();
}

void RenderManager::EndOffscreen()
{
    assert(offscreen_);
    offscreenRecording_ = false;
    offscreen_->End();
}

WorldEffectsFog::Scope RenderManager::BeginWorldEffects()
{
    // Only the main HDR scene has this boundary. Previews and other post layers
    // retain their existing pipeline, and no callback can outlive its scene.
    if (!offscreenRecording_ || !IsEffectEnabled(PostEffectMode::DepthFog)) {
        return WorldEffectsFog::Scope{};
    }
    if (worldEffectsFogApplied_) {
        return WorldEffectsFog::Scope{worldEffectsFogParameters_};
    }

    auto cpu = FrameProfiler::Get().ScopeCpu("World effects fog boundary");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "World effects fog boundary");
    auto* cmd = dx_->GetCommandList();
    srv_->PreDraw();
    // Detach depth before sampling it. Neither the HDR color nor the scene
    // depth may be cleared when returning to transparent effects below.
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    dx_->TransitionResource(dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // The regular post scratch target is free during scene recording. Reusing
    // it avoids another full-resolution HDR allocation or a new RTV slot.
    auto& scratch = *postBuffers_[0];
    DrawFullscreenPassToBuffer(PostEffectMode::DepthFog, offscreen_->GetSrvIndex(),
        offscreen_->GetResource(), scratch);
    scratch.TransitionToShaderResource();
    offscreen_->BeginForPostEffect();
    DrawFullscreenPass(PostEffectMode::FullScreen, scratch.GetSrvIndex());
    scratch.TransitionToRenderTarget();

    dx_->TransitionResource(dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    dx_->BindRenderTextureWithDepthNoClear(offscreen_->GetRtvIndex());
    srv_->PreDraw();

    const auto& fog = depthFogParameters_;
    auto finite = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    auto color = [&](const Vector3& value) {
        return Vector3{(std::max)(finite(value.x, 0.0f), 0.0f),
            (std::max)(finite(value.y, 0.0f), 0.0f),
            (std::max)(finite(value.z, 0.0f), 0.0f)};
    };
    const bool directionalBackground = fog.background.enabled >= 0.5f;
    const auto background = [&](const Vector4& value) {
        return color(directionalBackground ? Vector3{value.x, value.y, value.z} : fog.color);
    };
    const auto upper = background(fog.background.surfaceColor);
    const auto horizon = background(fog.background.horizonColor);
    const auto lower = background(fog.background.lowerColor);
    worldEffectsFogParameters_ = {
        {finite(fog.startDistance, 0.0f), finite(fog.endDistance, 120.0f),
            (std::max)(finite(fog.density, 0.0f), 0.0f),
            std::clamp(finite(fog.maxOpacity, 0.0f), 0.0f, 1.0f)},
        {(std::max)(finite(fog.extinctionDistanceRGB.x, 110.0f), 0.001f),
            (std::max)(finite(fog.extinctionDistanceRGB.y, 190.0f), 0.001f),
            (std::max)(finite(fog.extinctionDistanceRGB.z, 250.0f), 0.001f),
            fog.underwaterMediumEnabled >= 0.5f ? 1.0f : 0.0f},
        {upper.x, upper.y, upper.z, finite(fog.medium.waterLevelY, 28.0f)},
        {horizon.x, horizon.y, horizon.z,
            (std::max)(finite(fog.background.horizonSoftness, 0.5f), 0.001f)},
        {lower.x, lower.y, lower.z, directionalBackground
            ? (std::max)(finite(fog.background.upwardLift, 0.0f), 0.0f) : 0.0f},
        {fog.enabled >= 0.5f ? 1.0f : 0.0f, fog.medium.enabled >= 0.5f ? 1.0f : 0.0f,
            directionalBackground ? std::clamp(finite(fog.background.lowerBlend, 0.0f), 0.0f, 1.0f) : 0.0f,
            (std::max)(finite(fog.medium.depthLightRange, 180.0f), 1.0f)},
        {finite(fog.medium.cameraPosition.x, 0.0f), finite(fog.medium.cameraPosition.y, 0.0f),
            finite(fog.medium.cameraPosition.z, 0.0f), 0.0f}
    };
    worldEffectsFogApplied_ = true;
    return WorldEffectsFog::Scope{worldEffectsFogParameters_};
}

void RenderManager::BeginPreview()
{
    assert(previewBuffer_);
    previewBuffer_->Begin();
}

void RenderManager::EndPreview()
{
    assert(previewBuffer_);
    previewBuffer_->End();
    previewDisplayBuffer_->BeginForPostEffect();
    DrawToneMapPass_(previewBuffer_->GetSrvIndex());
    previewDisplayBuffer_->End();
}

void RenderManager::BeginParticlePostLayer(PostEffectMode mode)
{
    BeginParticlePostLayer(mode == PostEffectMode::BoxFilter, mode == PostEffectMode::OutlineBloom);
    particlePostEffectMode_ = mode;
}

void RenderManager::BeginParticlePostLayer(bool bloom, bool outlineBloom)
{
    assert(particlePostLayer_);
    particlePostBloom_ = bloom;
    particlePostOutlineBloom_ = outlineBloom;
    particlePostEffectMode_ = outlineBloom ? PostEffectMode::OutlineBloom :
        bloom ? PostEffectMode::BoxFilter : PostEffectMode::FullScreen;
    hasParticlePostLayer_ = bloom || outlineBloom;
    if (hasParticlePostLayer_) {
        particlePostLayer_->BeginOverlayClear();
    }
}

void RenderManager::EndParticlePostLayer()
{
    if (!hasParticlePostLayer_) {
        return;
    }
    particlePostLayer_->End();
}

void RenderManager::ClearParticlePostLayer()
{
    hasParticlePostLayer_ = false;
    particlePostEffectMode_ = PostEffectMode::FullScreen;
    particlePostBloom_ = false;
    particlePostOutlineBloom_ = false;
}

void RenderManager::BeginObjectPostLayer(bool bloom, bool outlineBloom, bool luminanceOutline)
{
    assert(objectPostLayer_);
    objectPostBloom_ = bloom;
    objectPostOutlineBloom_ = outlineBloom;
    objectPostLuminanceOutline_ = luminanceOutline;
    hasObjectPostLayer_ = bloom || outlineBloom || luminanceOutline;
    if (hasObjectPostLayer_) {
        objectPostLayer_->BeginOverlayClear();
    }
}

void RenderManager::EndObjectPostLayer()
{
    if (hasObjectPostLayer_) {
        objectPostLayer_->End();
    }
}

void RenderManager::ClearObjectPostLayer()
{
    hasObjectPostLayer_ = false;
    objectPostBloom_ = false;
    objectPostOutlineBloom_ = false;
    objectPostLuminanceOutline_ = false;
}

uint32_t RenderManager::GetOffscreenSrvIndex() const
{
    assert(offscreen_);
    return offscreen_->GetSrvIndex();
}

uint32_t RenderManager::GetPreviewSrvIndex() const
{
    assert(previewDisplayBuffer_);
    return previewDisplayBuffer_->GetSrvIndex();
}

void RenderManager::BeginBackBuffer()
{
    dx_->PreDraw();
}

void RenderManager::CreateCopyImageRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0; // t0
    range0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1; // t1
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range2{};
    range2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range2.NumDescriptors = 1;
    range2.BaseShaderRegister = 2; // t2: Light Shaft water transmission
    range2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range3{};
    range3.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range3.NumDescriptors = 1;
    range3.BaseShaderRegister = 3;
    range3.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // 8 root CBVs (16 DWORD), 4 descriptor tables (4), 4 tone-map constants.
    // Total 24 DWORD, comfortably inside the D3D12 64-DWORD limit.
    D3D12_ROOT_PARAMETER rootParams[13]{};
    // [0]: SRV (t0) Color
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &range0;

    // [1]: CBV (b0) Gaussian Filter
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // [2]: SRV (t1) Depth
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &range1;

    // [3]: CBV (b1) Outline
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[3].Descriptor.ShaderRegister = 1;
    rootParams[3].Descriptor.RegisterSpace = 0;

    // [4]: CBV (b2) RadialBlur
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[4].Descriptor.ShaderRegister = 2;
    rootParams[4].Descriptor.RegisterSpace = 0;

    // [5]: CBV (b3) Dissolve
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[5].Descriptor.ShaderRegister = 3;
    rootParams[5].Descriptor.RegisterSpace = 0;

    // [6]: CBV (b4) Random
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[6].Descriptor.ShaderRegister = 4;
    rootParams[6].Descriptor.RegisterSpace = 0;

    // [7]: CBV (b5) Bloom
    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[7].Descriptor.ShaderRegister = 5;
    rootParams[7].Descriptor.RegisterSpace = 0;

    // [8]: CBV (b6) Depth Fog. Appended to preserve all existing indices.
    rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[8].Descriptor.ShaderRegister = 6;
    rootParams[8].Descriptor.RegisterSpace = 0;

    // [9]: CBV (b7) Light Shaft. Appended to preserve all existing indices.
    rootParams[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[9].Descriptor.ShaderRegister = 7;
    rootParams[9].Descriptor.RegisterSpace = 0;

    // [10]: SRV (t2). Existing post-effect root indices stay unchanged.
    rootParams[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[10].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[10].DescriptorTable.pDescriptorRanges = &range2;

    rootParams[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[11].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[11].DescriptorTable.pDescriptorRanges = &range3;

    rootParams[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[12].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[12].Constants.ShaderRegister = 8;
    rootParams[12].Constants.Num32BitValues = 4;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(rootParams);
    desc.pParameters = rootParams;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob,
        &errBlob);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0,
        sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&copyImageRootSignature_));
    assert(SUCCEEDED(hr));
}

void RenderManager::CreatePipelineState(
    const wchar_t* psPath,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO,
    DXGI_FORMAT targetFormat)
{
    auto vs = dx_->CompileShader(L"resources/shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompileShader(psPath, L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = copyImageRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = targetFormat;
    desc.SampleDesc.Count = 1;
    desc.InputLayout.pInputElementDescs = nullptr;
    desc.InputLayout.NumElements = 0;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&outPSO));
    assert(SUCCEEDED(hr));
}

void RenderManager::DrawToneMapPass_(uint32_t srcSrvIndex)
{
    auto cpu = FrameProfiler::Get().ScopeCpu("Tone map");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Tone map");
    auto* cmd = dx_->GetCommandList();
    assert(dx_->GetCurrentRenderTargetFormat() == kDisplayColorFormat);
    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(toneMapPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(srcSrvIndex));
    const float parameters[4] = { exposureEV_, toneMapShoulderStart_, 0.0f, 0.0f };
    cmd->SetGraphicsRoot32BitConstants(12, 4, parameters, 0);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void RenderManager::DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* bloomCBOverride)
{
    auto* cmd = dx_->GetCommandList();

    const int modeIndex = static_cast<int>(mode);
    assert(modeIndex >= 0 && modeIndex < kEffectCount);
    auto cpu = FrameProfiler::Get().ScopeCpu(kEffectNames[modeIndex]);
    auto gpu = FrameProfiler::Get().ScopeGpu(cmd, kEffectNames[modeIndex]);

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(pipelineStates_[modeIndex].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(srcSrvIndex));
    // Depth or Mask (t1)
    uint32_t t1SrvIndex = (mode == PostEffectMode::Dissolve) ? noiseSrvIndex_ : depthSrvIndex_;
    cmd->SetGraphicsRootDescriptorTable(2, srv_->GetGPUDescriptionHandle(t1SrvIndex));

    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        cmd->SetGraphicsRootConstantBufferView(1, gaussianFilterCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::BoxFilter || mode == PostEffectMode::OutlineBloom) {
        ID3D12Resource* activeBloomCB = bloomCBOverride ? bloomCBOverride : bloomCB_.Get();
        cmd->SetGraphicsRootConstantBufferView(7, activeBloomCB->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Outline ||
        mode == PostEffectMode::LuminanceBasedOutline ||
        mode == PostEffectMode::LuminanceOutlineMask) {
        cmd->SetGraphicsRootConstantBufferView(3, outlineCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::RadialBlur) {
        cmd->SetGraphicsRootConstantBufferView(4, radialBlurCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Dissolve) {
        cmd->SetGraphicsRootConstantBufferView(5, dissolveCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Random) {
#ifdef USE_IMGUI
        if (randomCBData_) {
            randomCBData_->time = (float)ImGui::GetTime();
        }
#endif
        cmd->SetGraphicsRootConstantBufferView(6, randomCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::DepthFog) {
        // ImGui may edit controls after command recording. Keep those writes
        // out of the upload memory referenced by this draw. PostDraw fences
        // each submitted frame before this buffer is reused.
        *depthFogCBData_ = depthFogParameters_;
        const bool hasCaustics = oceanCausticsTexture_.ptr != 0;
        if (!hasCaustics) {
            depthFogCBData_->oceanLighting.causticsEnabled = 0.0f;
        }
        cmd->SetGraphicsRootDescriptorTable(10, hasCaustics
            ? oceanCausticsTexture_ : srv_->GetGPUDescriptionHandle(srcSrvIndex));
        const bool hasShadow = oceanShadowTexture_.ptr != 0;
        if (!hasShadow) {
            depthFogCBData_->oceanShadow.settings.w = 0.0f;
        }
        // A scalar-compatible valid t3 is bound even before an environment exists.
        cmd->SetGraphicsRootDescriptorTable(11, hasShadow
            ? oceanShadowTexture_ : srv_->GetGPUDescriptionHandle(depthSrvIndex_));
        cmd->SetGraphicsRootConstantBufferView(8, depthFogCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::LightShaft) {
        LightShaftParameters effectiveParameters = lightShaftParameters_;
        const bool hasTransmissionTexture = lightShaftTransmissionTexture_.ptr != 0;
        if (!hasTransmissionTexture) {
            effectiveParameters.transmissionEnabled = 0.0f;
        }
        // Bind a valid descriptor even before an environment has supplied its atlas.
        // The disabled shader path never samples this unused binding.
        cmd->SetGraphicsRootDescriptorTable(10, hasTransmissionTexture
            ? lightShaftTransmissionTexture_
            : srv_->GetGPUDescriptionHandle(srcSrvIndex));
        const bool fogActive =
            enabledEffects_[static_cast<int>(PostEffectMode::DepthFog)];

        if (!fogActive) {
            effectiveParameters.underwaterFactor = 0.0f;
        }
        *lightShaftCBData_ = effectiveParameters;
        cmd->SetGraphicsRootConstantBufferView(
            9, lightShaftCB_->GetGPUVirtualAddress());
    }

    cmd->DrawInstanced(3, 1, 0, 0);
}

void RenderManager::DrawAdditiveCompositePass(uint32_t baseSrvIndex, uint32_t addSrvIndex)
{
    auto cpu = FrameProfiler::Get().ScopeCpu("Additive composite");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Additive composite");
    auto* cmd = dx_->GetCommandList();

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(additiveCompositePSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(baseSrvIndex));
    cmd->SetGraphicsRootDescriptorTable(2, srv_->GetGPUDescriptionHandle(addSrvIndex));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void RenderManager::DrawFullscreenPassToBuffer(
    PostEffectMode mode,
    uint32_t srcSrvIndex,
    ID3D12Resource* srcResource,
    OffscreenPass& dst,
    ID3D12Resource* bloomCBOverride)
{
    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    dst.BeginForPostEffect();
    DrawFullscreenPass(mode, srcSrvIndex, bloomCBOverride);

    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
}

bool RenderManager::ShouldRunPostEffect_(int index, bool skipWorldFog) const
{
    const auto mode = static_cast<PostEffectMode>(index);
    return enabledEffects_[index] && mode != PostEffectMode::GaussianBlurX &&
        mode != PostEffectMode::GaussianBlurY && !(skipWorldFog && mode == PostEffectMode::DepthFog);
}

int RenderManager::FindLastEnabledPostEffect_(bool skipWorldFog) const
{
    int lastEffect = -1;
    for (int i = 1; i < kEffectCount; ++i) {
        if (ShouldRunPostEffect_(i, skipWorldFog)) lastEffect = i;
    }
    return lastEffect;
}

uint32_t RenderManager::RenderPostEffectsToBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex)
{
    const bool skipWorldFog = worldEffectsFogApplied_ && srcResource == offscreen_->GetResource();
    const int lastEffect = FindLastEnabledPostEffect_(skipWorldFog);
    int bufferIndex = 0;

    if (lastEffect < 0) {
        OffscreenPass& dst = *postBuffers_[bufferIndex];
        DrawFullscreenPassToBuffer(PostEffectMode::FullScreen, srcSrvIndex, srcResource, dst);
        dst.TransitionToShaderResource();
        return dst.GetSrvIndex();
    }

    for (int i = 1; i < kEffectCount; ++i) {
        if (!ShouldRunPostEffect_(i, skipWorldFog)) continue;

        const PostEffectMode mode = static_cast<PostEffectMode>(i);
        if (mode == PostEffectMode::GaussianBlur) {
            OffscreenPass& dstX = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurX, srcSrvIndex, srcResource, dstX);
            srcResource = dstX.GetResource();
            srcSrvIndex = dstX.GetSrvIndex();
            bufferIndex = 1 - bufferIndex;

            OffscreenPass& dstY = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource, dstY);
            srcResource = dstY.GetResource();
            srcSrvIndex = dstY.GetSrvIndex();
            if (i != lastEffect) {
                bufferIndex = 1 - bufferIndex;
            } else {
                dstY.TransitionToShaderResource();
            }
        } else {
            OffscreenPass& dst = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(mode, srcSrvIndex, srcResource, dst);
            srcResource = dst.GetResource();
            srcSrvIndex = dst.GetSrvIndex();
            if (i != lastEffect) {
                bufferIndex = 1 - bufferIndex;
            } else {
                dst.TransitionToShaderResource();
            }
        }
    }

    return srcSrvIndex;
}

uint32_t RenderManager::RenderLayerPostEffectsToBuffer_(
    ID3D12Resource* srcResource,
    uint32_t srcSrvIndex,
    bool bloom,
    bool outlineBloom,
    const Vector4& bloomColor,
    ID3D12Resource* bloomCB,
    BloomParameter* bloomCBData,
    const Vector4& outlineBloomColor,
    ID3D12Resource* outlineBloomCB,
    BloomParameter* outlineBloomCBData,
    bool luminanceOutline,
    OffscreenPass* tempCompositeBuffer)
{
    if (bloomCBData) {
        bloomCBData->color = bloomColor;
        bloomCBData->intensity = bloomIntensity_;
        bloomCBData->threshold = bloomThreshold_;
        bloomCBData->alpha = bloomAlpha_;
        bloomCBData->_pad = 0.0f;
    }
    if (outlineBloomCBData) {
        outlineBloomCBData->color = outlineBloomColor;
        outlineBloomCBData->intensity = bloomIntensity_;
        outlineBloomCBData->threshold = bloomThreshold_;
        outlineBloomCBData->alpha = bloomAlpha_;
        outlineBloomCBData->_pad = 0.0f;
    }

    if (!bloom && !outlineBloom && !luminanceOutline) {
        return srcSrvIndex;
    }

    // Bloom系は同じマスクから並列生成して加算する。
    if (bloom && outlineBloom) {
        OffscreenPass* actualTemp = tempCompositeBuffer ? tempCompositeBuffer : compositeBuffer2_.get();

        // 1. 通常ブルームを適用 (src -> particlePostBuffer_)
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::BoxFilter, srcSrvIndex, bloomCB);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        // 2. アウトラインブルームを適用 (src -> objectPostBuffer_)
        objectPostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::OutlineBloom, srcSrvIndex, outlineBloomCB);
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();

        // 3. 両者を加算合成 (particlePostBuffer_ + objectPostBuffer_ -> actualTemp)
        actualTemp->BeginForPostEffect();
        DrawAdditiveCompositePass(particlePostBuffer_->GetSrvIndex(), objectPostBuffer_->GetSrvIndex());
        actualTemp->End();
        actualTemp->TransitionToShaderResource();

        if (!luminanceOutline) {
            return actualTemp->GetSrvIndex();
        }

        // Bloomの中間結果はactualTempへ確定済みなので、particlePostBuffer_を
        // LuminanceOutline用に再利用し、3種類目として加算する。
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        objectPostBuffer_->BeginForPostEffect();
        DrawAdditiveCompositePass(actualTemp->GetSrvIndex(), particlePostBuffer_->GetSrvIndex());
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();
        return objectPostBuffer_->GetSrvIndex();
    }

    if (bloom || outlineBloom) {
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(
            bloom ? PostEffectMode::BoxFilter : PostEffectMode::OutlineBloom,
            srcSrvIndex,
            bloom ? bloomCB : outlineBloomCB);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        if (!luminanceOutline) {
            return particlePostBuffer_->GetSrvIndex();
        }

        objectPostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();

        OffscreenPass* actualTemp =
            tempCompositeBuffer ? tempCompositeBuffer : compositeBuffer2_.get();
        actualTemp->BeginForPostEffect();
        DrawAdditiveCompositePass(
            particlePostBuffer_->GetSrvIndex(), objectPostBuffer_->GetSrvIndex());
        actualTemp->End();
        actualTemp->TransitionToShaderResource();
        return actualTemp->GetSrvIndex();
    }

    // LuminanceOutlineのみ。
    particlePostBuffer_->BeginForPostEffect();
    DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
    particlePostBuffer_->End();
    particlePostBuffer_->TransitionToShaderResource();
    return particlePostBuffer_->GetSrvIndex();
}

uint32_t RenderManager::CompositeParticlePostToBuffer_(uint32_t baseSrvIndex)
{
    if (!hasParticlePostLayer_) {
        return baseSrvIndex;
    }

    // オブジェクト効果を先に合成したフレームではcompositeBuffer_がbaseそのもの。
    // そこを粒子Bloomの中間バッファにも使うと完成済みの背景を黒いマスクで
    // 上書きしてしまうため、処理済みのobjectPostLayer_を安全な作業領域にする。
    OffscreenPass* particleTempBuffer =
        hasObjectPostLayer_ ? objectPostLayer_.get() : compositeBuffer_.get();

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        particlePostLayer_->GetResource(),
        particlePostLayer_->GetSrvIndex(),
        particlePostBloom_,
        particlePostOutlineBloom_,
        particleLayerBloomColor_,
        particleBloomCB_.Get(),
        particleBloomCBData_,
        particleLayerOutlineBloomColor_,
        particleOutlineBloomCB_.Get(),
        particleOutlineBloomCBData_,
        false,
        particleTempBuffer);

    if (effectSrvIndex == particlePostLayer_->GetSrvIndex()) {
        return baseSrvIndex;
    }

    compositeBuffer2_->BeginForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
    compositeBuffer2_->End();
    compositeBuffer2_->TransitionToShaderResource();
    return compositeBuffer2_->GetSrvIndex();
}

uint32_t RenderManager::CompositeObjectPostToBuffer_(uint32_t baseSrvIndex)
{
    if (!hasObjectPostLayer_) {
        return baseSrvIndex;
    }

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        objectPostLayer_->GetResource(),
        objectPostLayer_->GetSrvIndex(),
        objectPostBloom_,
        objectPostOutlineBloom_,
        objectLayerBloomColor_,
        objectBloomCB_.Get(),
        objectBloomCBData_,
        objectLayerOutlineBloomColor_,
        objectOutlineBloomCB_.Get(),
        objectOutlineBloomCBData_,
        objectPostLuminanceOutline_,
        compositeBuffer2_.get());

    if (effectSrvIndex == objectPostLayer_->GetSrvIndex()) {
        return baseSrvIndex;
    }

    compositeBuffer_->BeginForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
    compositeBuffer_->End();
    compositeBuffer_->TransitionToShaderResource();
    return compositeBuffer_->GetSrvIndex();
}

void RenderManager::DrawOffscreenToBackBuffer()
{
    assert(offscreen_);
    assert(postBuffers_[0]);
    assert(postBuffers_[1]);

    auto* cmd = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    offscreen_->TransitionToRenderTarget();

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    uint32_t finalSrvIndex = RenderPostEffectsToBuffer_(
        offscreen_->GetResource(), offscreen_->GetSrvIndex());
    finalSrvIndex = CompositeObjectPostToBuffer_(finalSrvIndex);
    finalSrvIndex = CompositeParticlePostToBuffer_(finalSrvIndex);
    // Every returned source is already in PIXEL_SHADER_RESOURCE state.
    // All lighting and layer composites finish in HDR before this single conversion.
    dx_->SetBackBufferRenderTargetForPostEffect();
    DrawToneMapPass_(finalSrvIndex);

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    dx_->SetBackBufferRenderTarget();
}

uint32_t RenderManager::RenderPostEffectsForSceneTexture()
{
    assert(offscreen_);
    assert(postBuffers_[0]);
    assert(postBuffers_[1]);

    auto* cmd = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    offscreen_->TransitionToRenderTarget();

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    previewSrvIndex_ = RenderPostEffectsToBuffer_(offscreen_->GetResource(), offscreen_->GetSrvIndex());
    previewSrvIndex_ = CompositeObjectPostToBuffer_(previewSrvIndex_);
    previewSrvIndex_ = CompositeParticlePostToBuffer_(previewSrvIndex_);
    sceneDisplayBuffer_->BeginForPostEffect();
    DrawToneMapPass_(previewSrvIndex_);
    sceneDisplayBuffer_->End();
    previewSrvIndex_ = sceneDisplayBuffer_->GetSrvIndex();

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    dx_->SetBackBufferRenderTarget();
    return previewSrvIndex_;
}

bool RenderManager::BeginSceneTextureOverlay()
{
    // HUD is added after tone mapping in both Development and Release.
    sceneTextureOverlayTarget_ = sceneDisplayBuffer_.get();
    if (!sceneTextureOverlayTarget_ || previewSrvIndex_ != sceneTextureOverlayTarget_->GetSrvIndex()) {
        sceneTextureOverlayTarget_ = nullptr;
        return false;
    }
    sceneTextureOverlayTarget_->BeginForPostEffect();
    return true;
}

void RenderManager::EndSceneTextureOverlay()
{
    if (!sceneTextureOverlayTarget_) {
        return;
    }

    sceneTextureOverlayTarget_->End();
    sceneTextureOverlayTarget_ = nullptr;
    dx_->SetBackBufferRenderTarget();
}

void RenderManager::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Begin("Post Effect");
    ImGui::SliderFloat("Scene exposure (EV)", &exposureEV_, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Highlight shoulder", &toneMapShoulderStart_, 0.5f, 0.9f, "%.2f");

    if (ImGui::Button("Clear Effects")) {
        SetMode(PostEffectMode::FullScreen);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Bloom Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool bloomEnabled = enabledEffects_[static_cast<int>(PostEffectMode::BoxFilter)];
        if (ImGui::Checkbox("Enable Bloom", &bloomEnabled)) {
            SetEffectEnabled(PostEffectMode::BoxFilter, bloomEnabled);
        }

        bool outlineBloomEnabled = enabledEffects_[static_cast<int>(PostEffectMode::OutlineBloom)];
        if (ImGui::Checkbox("Enable Outline Bloom", &outlineBloomEnabled)) {
            SetEffectEnabled(PostEffectMode::OutlineBloom, outlineBloomEnabled);
        }

        if (ImGui::ColorEdit4("Bloom Color / Alpha", &bloomColor_.x)) {
            bloomCBData_->color = bloomColor_;
        }
        if (ImGui::SliderFloat("Bloom Intensity", &bloomIntensity_, 0.0f, 5.0f)) {
            bloomCBData_->intensity = bloomIntensity_;
        }
        if (ImGui::SliderFloat("Bloom Threshold", &bloomThreshold_, 0.0f, 1.0f)) {
            bloomCBData_->threshold = bloomThreshold_;
        }
        if (ImGui::SliderFloat("Bloom Mix Alpha", &bloomAlpha_, 0.0f, 1.0f)) {
            bloomCBData_->alpha = bloomAlpha_;
        }
        ImGui::TextDisabled("These settings are also used by particle Bloom / Outline Bloom.");
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Depth Fog Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool depthFogEnabled = enabledEffects_[static_cast<int>(PostEffectMode::DepthFog)];
        if (ImGui::Checkbox("Enable Depth Fog", &depthFogEnabled)) {
            SetEffectEnabled(PostEffectMode::DepthFog, depthFogEnabled);
        }
        if (ImGui::ColorEdit3("Fog Color", &depthFogColor_.x)) {
            depthFogParameters_.color = depthFogColor_;
        }
        if (ImGui::DragFloat("Start Distance", &depthFogStartDistance_, 1.0f, 0.0f, 1000.0f, "%.1f")) {
            depthFogParameters_.startDistance = depthFogStartDistance_;
        }
        if (ImGui::DragFloat("End Distance", &depthFogEndDistance_, 1.0f, 0.0f, 2000.0f, "%.1f")) {
            depthFogParameters_.endDistance = depthFogEndDistance_;
        }
        if (ImGui::DragFloat("Density", &depthFogDensity_, 0.001f, 0.0f, 0.1f, "%.3f")) {
            depthFogParameters_.density = depthFogDensity_;
        }
        if (ImGui::SliderFloat("Max Opacity", &depthFogMaxOpacity_, 0.0f, 1.0f)) {
            depthFogParameters_.maxOpacity = depthFogMaxOpacity_;
        }
        if (ImGui::SliderFloat("Background Opacity", &depthFogBackgroundOpacity_, 0.0f, 1.0f)) {
            depthFogParameters_.backgroundOpacity = depthFogBackgroundOpacity_;
        }
        if (ImGui::SliderFloat(
            "Far Background Blend Start",
            &depthFogFarBackgroundBlendStartRatio_, 0.60f, 0.98f, "%.2f")) {
            depthFogParameters_.farBackgroundBlendStartRatio =
                depthFogFarBackgroundBlendStartRatio_;
        }
        if (ImGui::Checkbox(
            "Underwater Medium Model", &underwaterMediumEnabled_)) {
            depthFogParameters_.underwaterMediumEnabled =
                underwaterMediumEnabled_ ? 1.0f : 0.0f;
        }
        if (ImGui::DragFloat3(
            "Extinction Distance RGB",
            &depthFogExtinctionDistanceRGB_.x,
            1.0f, 0.0f, 1000.0f, "%.1f")) {
            depthFogParameters_.extinctionDistanceRGB =
                depthFogExtinctionDistanceRGB_;
        }
        if (ImGui::Button("Shallow Clear")) {
            depthFogExtinctionDistanceRGB_ = { 80.0f, 160.0f, 320.0f };
            depthFogParameters_.extinctionDistanceRGB =
                depthFogExtinctionDistanceRGB_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Very Clear")) {
            depthFogExtinctionDistanceRGB_ = { 120.0f, 240.0f, 480.0f };
            depthFogParameters_.extinctionDistanceRGB =
                depthFogExtinctionDistanceRGB_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Strong Test")) {
            depthFogExtinctionDistanceRGB_ = { 45.0f, 90.0f, 180.0f };
            depthFogParameters_.extinctionDistanceRGB =
                depthFogExtinctionDistanceRGB_;
        }
    }

    ImGui::Separator();
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY) ||
            i == static_cast<int>(PostEffectMode::LuminanceOutlineMask) ||
            i == static_cast<int>(PostEffectMode::DepthFog) ||
            i == static_cast<int>(PostEffectMode::LightShaft)) {
            continue;
        }
        bool enabled = enabledEffects_[i];
        if (ImGui::Checkbox(kEffectNames[i], &enabled)) {
            SetEffectEnabled(static_cast<PostEffectMode>(i), enabled);
        }

        if (static_cast<PostEffectMode>(i) == PostEffectMode::GaussianBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("Sigma", &sigma_, 0.1f, 10.0f)) {
                gaussianFilterCBData_->sigma = sigma_;
            }
            ImGui::Unindent();
        }
        if ((static_cast<PostEffectMode>(i) == PostEffectMode::BoxFilter ||
            static_cast<PostEffectMode>(i) == PostEffectMode::OutlineBloom) && enabled) {
            ImGui::PushID(i);
            ImGui::Indent();
            ImGui::TextDisabled("Use Bloom Settings above.");
            ImGui::Unindent();
            ImGui::PopID();
        }
        if ((static_cast<PostEffectMode>(i) == PostEffectMode::Outline ||
            static_cast<PostEffectMode>(i) == PostEffectMode::LuminanceBasedOutline) && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("Color", &outlineColor_.x)) {
                outlineCBData_->color = outlineColor_;
            }
            if (ImGui::SliderFloat("Thickness", &outlineThickness_, 0.1f, 10.0f)) {
                outlineCBData_->thickness = outlineThickness_;
            }
            if (ImGui::SliderFloat("Threshold", &outlineThreshold_, 0.0f, 1.0f)) {
                outlineCBData_->threshold = outlineThreshold_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::RadialBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat2("Center", &radialBlurCenter_.x, 0.0f, 1.0f)) {
                radialBlurCBData_->center = radialBlurCenter_;
            }
            if (ImGui::SliderInt("NumSamples", &radialBlurNumSamples_, 0, 50)) {
                radialBlurCBData_->numSamples = radialBlurNumSamples_;
            }
            if (ImGui::SliderFloat("BlurWidth", &radialBlurWidth_, 0.0f, 0.1f)) {
                radialBlurCBData_->blurWidth = radialBlurWidth_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::Dissolve && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("EdgeColor", &dissolveEdgeColor_.x)) {
                dissolveCBData_->edgeColor = dissolveEdgeColor_;
            }
            if (ImGui::SliderFloat("Threshold", &dissolveThreshold_, 0.0f, 1.0f)) {
                dissolveCBData_->threshold = dissolveThreshold_;
            }
            if (ImGui::SliderFloat("EdgeWidth", &dissolveEdgeWidth_, 0.0f, 0.2f)) {
                dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
            }
            if (ImGui::ColorEdit4("BackgroundColor", &dissolveBackgroundColor_.x)) {
                dissolveCBData_->backgroundColor = dissolveBackgroundColor_;
            }
            ImGui::Unindent();
        }
    }

    ImGui::Separator();
    ImGui::Text("Current Chain:");
    bool any = false;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (enabledEffects_[i]) {
            ImGui::BulletText("%s", kEffectNames[i]);
            any = true;
        }
    }
    if (!any) {
        ImGui::BulletText("%s", kEffectNames[0]);
    }

    ImGui::End();
#endif
}
