#pragma once
#include <dxgiformat.h>

// All scene lighting and post-effect composition stays linear and unclipped.
// Only the final display pass writes to an sRGB target.
inline constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
inline constexpr DXGI_FORMAT kDisplayColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
