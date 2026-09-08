#pragma once

#include "Vector3.h"
#include <cstddef>
#include <type_traits>

// CPU-only, scoped presentation settings. Each VFX renderer copies this value
// into its own immutable frame constants; no GPU upload buffer is shared here.
namespace WorldEffectsFog {
struct Parameters {
    Vector4 fogDistance{};   // start, end, density, maximum opacity
    Vector4 fogExtinction{}; // extinction distances RGB, spectral medium enabled
    Vector4 fogSurface{};    // upper background RGB, water level
    Vector4 fogHorizon{};    // horizon background RGB, horizon softness
    Vector4 fogLower{};      // lower background RGB, upward brightness lift
    Vector4 fogOptions{};    // enabled, water half-space enabled, lower blend, depth light range
    Vector4 fogCamera{};     // matching medium camera XYZ, reserved
};
static_assert(sizeof(Parameters) == 112);
static_assert(offsetof(Parameters, fogCamera) == 96);
static_assert(std::is_trivially_copyable_v<Parameters>);

namespace Detail {
inline thread_local Parameters current{};
}

inline const Parameters& GetParameters() noexcept { return Detail::current; }

// Scopes nest on the render thread and never survive a frame or own a scene.
// Guaranteed C++17 return-value elision permits returning this non-movable RAII
// value, while accidental copies/moves cannot duplicate a restoration action.
class Scope final {
public:
    explicit Scope(const Parameters& parameters = {}) noexcept
        : previous_(Detail::current) { Detail::current = parameters; }
    ~Scope() noexcept { Detail::current = previous_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
private:
    Parameters previous_;
};
}
