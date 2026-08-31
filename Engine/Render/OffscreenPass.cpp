#include "OffscreenPass.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <cassert>

void OffscreenPass::Initialize(
    DirectXCommon* dx,
    SrvManager* srv,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const Vector4& clearColor,
    uint32_t rtvIndex)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;
    width_ = width;
    height_ = height;
    format_ = format;
    clearColor_ = clearColor;
    rtvIndex_ = rtvIndex;

    resource_ = dx_->CreateRenderTextureResource(
        width_,
        height_,
        format_,
        clearColor_
    );

    dx_->CreateRenderTextureRTV(
        resource_.Get(),
        rtvIndex_,
        format_
    );

    srvIndex_ = srv_->Allocate();
    srv_->CreateSRVTexture2D(
        srvIndex_,
        resource_.Get(),
        format_,
        1
    );
}

void OffscreenPass::Begin()
{
    TransitionToRenderTarget();
    dx_->PreDrawRenderTexture(rtvIndex_, clearColor_);
}

void OffscreenPass::BeginForPostEffect()
{
    TransitionToRenderTarget();
    dx_->PreDrawPostEffectBuffer(rtvIndex_);
}

void OffscreenPass::BeginForPostEffectClear()
{
    TransitionToRenderTarget();
    dx_->PreDrawPostEffectBuffer(rtvIndex_, clearColor_);
}

void OffscreenPass::BeginOverlayClear()
{
    TransitionToRenderTarget();
    dx_->PreDrawRenderTextureNoDepthClear(rtvIndex_, clearColor_);
}

void OffscreenPass::End()
{
    TransitionToShaderResource();
}

void OffscreenPass::TransitionToRenderTarget()
{
    if (currentState_ == D3D12_RESOURCE_STATE_RENDER_TARGET) {
        return;
    }
    dx_->TransitionResource(resource_.Get(), currentState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

void OffscreenPass::TransitionToShaderResource()
{
    if (currentState_ == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        return;
    }
    dx_->TransitionResource(resource_.Get(), currentState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}
