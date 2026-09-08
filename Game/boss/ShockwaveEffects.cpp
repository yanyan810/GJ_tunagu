#include "ShockwaveEffects.h"
#include "BossWaterEffectRenderer.h"
#include "Shockwave.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr float kTau = 6.28318530718f;
constexpr float kRockLifetime = 0.85f;
constexpr float kRingTailLifetime = 0.45f;
constexpr size_t kMaxBursts = 32;
using Point = BossWaterEffectRenderer::Point;
using Particle = BossWaterEffectRenderer::Particle;

float Safe(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
float Smooth(float value) {
    const float t = Safe(value, 0.0f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
bool FinitePosition(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && std::abs(value.x) <= 100000.0f && std::abs(value.y) <= 100000.0f
        && std::abs(value.z) <= 100000.0f;
}
Vector3 HorizontalUnit(const Vector3& value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.z)) return { 1, 0, 0 };
    const float length = std::hypot(value.x, value.z);
    return std::isfinite(length) && length > 0.0001f
        ? Vector3{ value.x / length, 0, value.z / length } : Vector3{ 1, 0, 0 };
}
}

struct ShockwaveEffects::Impl {
    struct RockBurst {
        Vector3 origin{}, outward{ 1, 0, 0 };
        float age = 0.0f, seed = 0.0f;
    };
    struct Pulse {
        Vector3 center{}, areaScale{ 1, 1, 1 };
        float planeY = 0.0f, radius = 0.0f, elapsed = 0.0f;
        float stationaryAge = 0.0f, inactiveAge = 0.0f;
        bool hasWave = false, waveActive = false;
    };

    BossWaterEffectRenderer renderer;
    BossWaterEffectRenderer::Style style{ 1.15f, 0.9f, 1.0f, 4.0f };
    Appearance appearance{};
    std::array<Pulse, kMaxPulses> pulses{};
    std::vector<RockBurst> bursts;
    float time = 0.0f;
    uint64_t rockEventCount = 0;
    bool enabled = true, soloPreview = false;

    float RingStrength(const Pulse& pulse) const {
        if (!enabled || !pulse.hasWave || pulse.radius <= 0.001f) return 0.0f;
        // The actual radius may remain at max while rocks are emitted. Let its
        // water crest dissipate there rather than leaving a stationary disk.
        const float stoppedFade = 1.0f - Smooth((pulse.stationaryAge - 0.08f) / 0.8f);
        const float endedFade = pulse.waveActive ? 1.0f : 1.0f - Smooth(pulse.inactiveAge / kRingTailLifetime);
        const float opening = 0.35f + 0.65f * Smooth(pulse.elapsed / 0.07f);
        return stoppedFade * endedFade * opening;
    }

    void BuildRing(const Pulse& pulse, float strength) {
        if (strength <= 0.001f) return;
        const auto& center = pulse.center;
        const auto& areaScale = pulse.areaScale;
        const float radius = pulse.radius;
        const float planeY = pulse.planeY;
        const float width = appearance.thickness * (0.85f + 0.15f * std::min(radius / 30.0f, 1.0f));
        const float light = strength * appearance.intensity;
        std::array<Point, 129> crest{};
        std::array<Point, 129> water{};
        std::array<Point, 129> core{};
        const float waterRadius = std::max(0.0f, radius - width * 0.45f);
        for (size_t i = 0; i < crest.size(); ++i) {
            const float angle = kTau * static_cast<float>(i) / static_cast<float>(crest.size() - 1);
            // Warm rim and neutral core share the exact gameplay radius. The
            // broad jelly body trails inside it, never forming an earlier hit.
            const float crestY = planeY + std::sin(angle * 4.0f + time * 2.4f) * 0.035f;
            const Vector3 position{ center.x + std::cos(angle) * radius * areaScale.x,
                crestY, center.z + std::sin(angle) * radius * areaScale.z };
            crest[i] = { position, width * 0.38f };
            core[i] = { position, std::max(0.08f, width * 0.18f) };
            water[i] = { { center.x + std::cos(angle) * waterRadius * areaScale.x,
                crestY, center.z + std::sin(angle) * waterRadius * areaScale.z }, width };
        }
        // These are camera-facing ribbons, retaining visible vertical coverage
        // when a swimmer looks almost parallel to the wave's horizontal plane.
        const float danger = appearance.dangerMix;
        const Vector4 dangerTint{ 0.12f + 1.33f * danger, 0.84f - 0.42f * danger,
            1.0f - 0.88f * danger, 0.88f * strength };
        renderer.Ribbon(water, { 0.07f, 0.70f, 1.0f, 0.66f * strength }, light * 0.8f, time * 0.75f);
        renderer.Ribbon(crest, dangerTint, light * 1.4f, time * 0.75f);
        renderer.Ribbon(core, { 1.0f, 0.96f, 0.89f, 0.92f * strength }, light * 1.8f, time * 0.75f);

        // A weaker trailing ripple stays inside the authoritative pressure rim.
        const float innerRadius = std::max(0.0f, radius - 0.55f - radius * 0.035f);
        if (innerRadius > 0.1f) {
            std::array<Point, 73> residue{};
            for (size_t i = 0; i < residue.size(); ++i) {
                const float angle = kTau * static_cast<float>(i) / static_cast<float>(residue.size() - 1);
                residue[i] = { { center.x + std::cos(angle) * innerRadius * areaScale.x,
                    planeY - 0.06f + 0.025f * std::sin(angle * 5.0f - time * 1.7f),
                    center.z + std::sin(angle) * innerRadius * areaScale.z }, width * 0.58f };
            }
            renderer.Ribbon(residue, { 0.13f, 0.63f, 0.86f, 0.28f * strength }, light * 0.25f, -time * 0.4f);
        }

        // Sparse suspended droplets lift the rim without filling the area with
        // particles or drawing a second apparent attack boundary.
        for (int i = 0; i < 18; ++i) {
            const float angle = kTau * static_cast<float>(i) / 18.0f;
            const float dropletPulse = 0.5f + 0.5f * std::sin(time * 3.0f + i * 2.39996323f);
            const float dropletRadius = std::max(0.0f, radius - width * 0.7f);
            const Vector3 position{ center.x + std::cos(angle) * dropletRadius * areaScale.x,
                planeY + 0.20f + dropletPulse * 0.18f,
                center.z + std::sin(angle) * dropletRadius * areaScale.z };
            renderer.Billboard(position, 0.045f + dropletPulse * 0.025f,
                { 0.27f, 0.95f, 1.0f, 0.6f }, Particle::Mote,
                light * (0.15f + 0.24f * dropletPulse), { 0, 0.3f, 0 });
        }
    }

    void BuildRock(const RockBurst& burst) {
        const float age = burst.age;
        const float life = age / kRockLifetime;
        const float fade = 1.0f - Smooth(life);
        const Vector3 tangent{ -burst.outward.z, 0, burst.outward.x };

        if (age < 0.26f) {
            renderer.Billboard(burst.origin + Vector3{ 0, 0.16f, 0 }, 0.19f + age * 2.3f,
                { 0.60f, 0.95f, 0.87f, 0.62f }, Particle::Halo,
                (1.0f - Smooth(age / 0.26f)) * 0.7f);
        }

        // Two open splash arcs leave the actual spawned rock readable inside.
        const float arcFade = Smooth(age / 0.035f) * (1.0f - Smooth(age / 0.55f));
        if (arcFade > 0.001f) {
            for (int arc = 0; arc < 2; ++arc) {
                std::array<Point, 13> path{};
                const Vector3 across = arc == 0 ? tangent : burst.outward;
                for (size_t i = 0; i < path.size(); ++i) {
                    const float u = static_cast<float>(i) / static_cast<float>(path.size() - 1);
                    const float angle = u * kTau * 0.5f;
                    path[i] = { burst.origin + across * (std::cos(angle) * (0.22f + age * 1.4f))
                        + Vector3{ 0, 0.12f + std::sin(angle) * (0.2f + age * 2.0f), 0 },
                        0.04f + 0.025f * std::sin(angle) };
                }
                renderer.Ribbon(path, { 0.20f, 0.86f, 0.95f, 0.55f * arcFade }, arcFade * 0.65f, age * 1.3f);
            }
        }

        for (int i = 0; i < 7; ++i) {
            const float angle = burst.seed + static_cast<float>(i) * 2.39996323f;
            const float outwardSpeed = 0.38f + 0.12f * static_cast<float>(i % 3);
            const float rise = 1.25f + 0.22f * static_cast<float>(i % 4);
            const Vector3 sideways{ std::cos(angle) * outwardSpeed, 0, std::sin(angle) * outwardSpeed };
            const Vector3 position = burst.origin + sideways * (0.18f + age)
                + burst.outward * (age * 0.16f)
                + Vector3{ 0, 0.16f + rise * age - 0.5f * age * age, 0 };
            const Vector3 velocity = sideways + Vector3{ 0, rise - age, 0 };
            const Vector4 tint = i % 3 == 0 ? Vector4{ 0.94f, 0.83f, 0.42f, 0.66f }
                : Vector4{ 0.20f, 0.89f, 0.98f, 0.63f };
            renderer.Billboard(position, 0.045f + 0.014f * static_cast<float>(i % 3),
                tint, i % 4 == 0 ? Particle::Bubble : Particle::Mote,
                fade * Smooth(age / 0.04f) * 0.7f, velocity);
        }
    }
};

ShockwaveEffects::ShockwaveEffects() : impl_(std::make_unique<Impl>()) {}
ShockwaveEffects::~ShockwaveEffects() = default;

void ShockwaveEffects::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    impl_->renderer.Initialize(dx, srv, camera);
    impl_->bursts.reserve(kMaxBursts);
    Reset();
}

void ShockwaveEffects::Reset() {
    auto& e = *impl_;
    e.renderer.Clear();
    e.bursts.clear();
    e.pulses = {};
    e.time = 0.0f;
    e.rockEventCount = 0;
}

void ShockwaveEffects::OnTrigger(const Shockwave& wave, const ShockwaveSettings& settings, float groundY,
    const Vector3& areaScale) {
    Reset();
    // The existing F2 caller supplies seabed height. Keep its old clearance;
    // volley callers supply the actual swimmer-depth plane through the slot API.
    OnTrigger(0, wave, settings, groundY + 0.30f, areaScale);
}

void ShockwaveEffects::OnTrigger(size_t slot, const Shockwave& wave,
    [[maybe_unused]] const ShockwaveSettings& settings, float planeY, const Vector3& areaScale) {
    auto& e = *impl_;
    if (slot >= e.pulses.size()) return;
    auto& pulse = e.pulses[slot];
    pulse = {};
    if (!e.enabled || !wave.IsActive() || !FinitePosition(wave.GetCenter()) || !std::isfinite(planeY)) return;
    pulse.center = wave.GetCenter();
    pulse.planeY = Safe(planeY, 0.0f, -100000.0f, 100000.0f);
    pulse.radius = Safe(wave.GetRadius(), 0.0f, 0.0f, 500.0f);
    pulse.areaScale = { Safe(areaScale.x, 1.0f, 0.01f, 100.0f), 1.0f, Safe(areaScale.z, 1.0f, 0.01f, 100.0f) };
    pulse.hasWave = pulse.waveActive = true;
}

void ShockwaveEffects::Update(float dt, const Shockwave& wave, const Vector3& areaScale) {
    Update(dt, std::span<const Shockwave>(&wave, 1), areaScale);
}

void ShockwaveEffects::Update(float dt, std::span<const Shockwave> waves, const Vector3& areaScale) {
    auto& e = *impl_;
    if (!e.enabled || !std::isfinite(dt) || dt < 0.0f) return;
    // Huge finite timesteps expire tails in one update without overflowing ages.
    const float step = std::min(dt, 10000.0f);
    e.time = std::fmod(e.time + step, 1024.0f);
    for (auto& burst : e.bursts) burst.age += step;
    std::erase_if(e.bursts, [](const Impl::RockBurst& burst) { return burst.age >= kRockLifetime; });
    for (size_t slot = 0; slot < e.pulses.size(); ++slot) {
        auto& pulse = e.pulses[slot];
        if (!pulse.hasWave) continue;
        pulse.areaScale = { Safe(areaScale.x, 1.0f, 0.01f, 100.0f), 1.0f, Safe(areaScale.z, 1.0f, 0.01f, 100.0f) };
        pulse.elapsed = std::min(pulse.elapsed + step, 10000.0f);
        const Shockwave* wave = slot < waves.size() ? &waves[slot] : nullptr;
        const float nextRadius = wave ? Safe(wave->GetRadius(), pulse.radius, 0.0f, 500.0f) : pulse.radius;
        if (step > 0.0f) {
            pulse.stationaryAge = nextRadius > pulse.radius + 0.00001f ? 0.0f : std::min(pulse.stationaryAge + step, 10000.0f);
        }
        pulse.radius = nextRadius;
        pulse.waveActive = wave && wave->IsActive();
        pulse.inactiveAge = pulse.waveActive ? 0.0f : std::min(pulse.inactiveAge + step, 10000.0f);
        if (!pulse.waveActive && pulse.inactiveAge >= kRingTailLifetime) pulse.hasWave = false;
    }
}

void ShockwaveEffects::SetAppearance(const Appearance& appearance) {
    impl_->appearance = { Safe(appearance.thickness, 0.70f, 0.15f, 1.5f),
        Safe(appearance.intensity, 1.7f, 0.25f, 3.0f), Safe(appearance.dangerMix, 0.72f, 0.0f, 1.0f) };
}

ShockwaveEffects::Appearance ShockwaveEffects::GetAppearance() const { return impl_->appearance; }

void ShockwaveEffects::OnRockSpawn(const ShockwaveRockSpawn& spawn) {
    auto& e = *impl_;
    if (!e.enabled || !FinitePosition(spawn.position)) return;
    if (e.bursts.size() >= kMaxBursts) e.bursts.erase(e.bursts.begin());
    ++e.rockEventCount;
    const float seed = static_cast<float>(e.rockEventCount % 4096) * 2.39996323f;
    e.bursts.push_back({ spawn.position, HorizontalUnit(spawn.outwardDirection), 0.0f, seed });
}

void ShockwaveEffects::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e = *impl_;
    e.renderer.Begin(e.time);
    if (!e.enabled) return;
    for (const auto& pulse : e.pulses) e.BuildRing(pulse, e.RingStrength(pulse));
    for (const auto& burst : e.bursts) e.BuildRock(burst);
    e.renderer.Draw(sceneColor, sceneDepth, e.style);
}

void ShockwaveEffects::DrawImGui() {
#ifdef USE_IMGUI
    auto& e = *impl_;
    if (ImGui::TreeNode("Shockwave VFX")) {
        if (ImGui::Checkbox("Enable Shockwave VFX", &e.enabled)) Reset();
        ImGui::Checkbox("Solo Shockwave Preview", &e.soloPreview);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.52f);
        ImGui::SliderFloat("Glow##ShockwaveVFX", &e.style.glow, 0.0f, 2.5f);
        ImGui::SliderFloat("Light Spread##ShockwaveVFX", &e.style.bloom, 0.0f, 3.0f);
        ImGui::SliderFloat("Water Opacity##ShockwaveVFX", &e.style.opacity, 0.0f, 1.5f);
        ImGui::SliderFloat("Pressure Band Half Width##ShockwaveVFX", &e.appearance.thickness, 0.15f, 1.5f);
        ImGui::SliderFloat("Pressure Light##ShockwaveVFX", &e.appearance.intensity, 0.25f, 3.0f);
        ImGui::SliderFloat("Warm Danger Rim##ShockwaveVFX", &e.appearance.dangerMix, 0.0f, 1.0f);
        ImGui::PopItemWidth();
        ImGui::TextWrapped("A warm danger rim and neutral light core follow each attack radius. Cyan water trails inside; up to three pulses share one renderer.");
        ImGui::TextWrapped("Local appearance settings. Solo isolates the preview while simulation continues.");
        ImGui::TreePop();
    }
#endif
}

bool ShockwaveEffects::IsEnabled() const { return impl_->enabled; }
bool ShockwaveEffects::IsSoloPreview() const { return impl_->enabled && impl_->soloPreview; }

ShockwaveEffects::Stats ShockwaveEffects::GetStats() const {
    const auto& e = *impl_;
    const auto rendererStats = e.renderer.GetStats();
    Stats stats;
    stats.drawCount = rendererStats.drawCount;
    stats.vertexCount = rendererStats.vertexCount;
    stats.burstCount = e.bursts.size();
    stats.rockEventCount = e.rockEventCount;
    stats.finite = rendererStats.finite;
    for (size_t slot = 0; slot < e.pulses.size(); ++slot) {
        const auto& pulse = e.pulses[slot];
        stats.radii[slot] = pulse.radius;
        if (e.RingStrength(pulse) > 0.001f) ++stats.activePulseCount;
        stats.finite = stats.finite && std::isfinite(pulse.radius) && FinitePosition(pulse.center);
    }
    stats.active = e.enabled && (stats.activePulseCount > 0 || !e.bursts.empty());
    stats.radius = e.pulses[0].radius;
    return stats;
}
