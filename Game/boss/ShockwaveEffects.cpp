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

    BossWaterEffectRenderer renderer;
    BossWaterEffectRenderer::Style style{ 0.9f, 0.9f, 0.85f, 4.5f };
    ShockwaveSettings settings{};
    std::vector<RockBurst> bursts;
    Vector3 center{}, areaScale{ 1, 1, 1 };
    float groundY = 0.0f, radius = 0.0f, elapsed = 0.0f, time = 0.0f;
    float stationaryAge = 0.0f, inactiveAge = 0.0f;
    uint64_t rockEventCount = 0;
    bool enabled = true, soloPreview = false, hasWave = false, waveActive = false;

    float RingStrength() const {
        if (!enabled || !hasWave || radius <= 0.001f) return 0.0f;
        // The actual radius may remain at max while rocks are emitted. Let its
        // water crest dissipate there rather than leaving a stationary disk.
        const float stoppedFade = 1.0f - Smooth((stationaryAge - 0.08f) / 0.8f);
        const float endedFade = waveActive ? 1.0f : 1.0f - Smooth(inactiveAge / kRingTailLifetime);
        const float opening = 0.35f + 0.65f * Smooth(elapsed / 0.07f);
        return stoppedFade * endedFade * opening;
    }

    void BuildRing(float strength) {
        if (strength <= 0.001f) return;
        const float width = 0.11f + std::min(radius, 30.0f) * 0.0085f;
        std::array<Point, 129> crest{};
        for (size_t i = 0; i < crest.size(); ++i) {
            const float angle = kTau * static_cast<float>(i) / static_cast<float>(crest.size() - 1);
            // Keep every point on the gameplay radius in XZ. Only the crest's
            // height ripples, so the decorative wave does not outrun the attack.
            crest[i] = { { center.x + std::cos(angle) * radius * areaScale.x,
                groundY + 0.30f + std::sin(angle * 4.0f + time * 2.4f) * 0.045f,
                center.z + std::sin(angle) * radius * areaScale.z }, width };
        }
        renderer.Ribbon(crest, { 0.12f, 0.84f, 1.0f, 0.76f * strength }, strength, time * 0.75f);

        // A weaker trailing ripple stays inside the authoritative pressure rim.
        const float innerRadius = std::max(0.0f, radius - 0.55f - radius * 0.035f);
        if (innerRadius > 0.1f) {
            std::array<Point, 73> residue{};
            for (size_t i = 0; i < residue.size(); ++i) {
                const float angle = kTau * static_cast<float>(i) / static_cast<float>(residue.size() - 1);
                residue[i] = { { center.x + std::cos(angle) * innerRadius * areaScale.x,
                    groundY + 0.22f + 0.025f * std::sin(angle * 5.0f - time * 1.7f),
                    center.z + std::sin(angle) * innerRadius * areaScale.z }, width * 0.58f };
            }
            renderer.Ribbon(residue, { 0.13f, 0.63f, 0.86f, 0.36f * strength }, strength * 0.38f, -time * 0.4f);
        }

        // Sparse suspended droplets lift the rim without filling the area with
        // particles or drawing a second apparent attack boundary.
        for (int i = 0; i < 18; ++i) {
            const float angle = kTau * static_cast<float>(i) / 18.0f;
            const float pulse = 0.5f + 0.5f * std::sin(time * 3.0f + i * 2.39996323f);
            const float dropletRadius = std::max(0.0f, radius - width * 0.7f);
            const Vector3 position{ center.x + std::cos(angle) * dropletRadius * areaScale.x,
                groundY + 0.42f + pulse * 0.18f,
                center.z + std::sin(angle) * dropletRadius * areaScale.z };
            renderer.Billboard(position, 0.045f + pulse * 0.025f,
                { 0.27f, 0.95f, 1.0f, 0.6f }, Particle::Mote,
                strength * (0.15f + 0.24f * pulse), { 0, 0.3f, 0 });
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
    e.center = {};
    e.areaScale = { 1, 1, 1 };
    e.groundY = e.radius = e.elapsed = e.time = e.stationaryAge = e.inactiveAge = 0.0f;
    e.rockEventCount = 0;
    e.hasWave = e.waveActive = false;
}

void ShockwaveEffects::OnTrigger(const Shockwave& wave, const ShockwaveSettings& settings, float groundY,
    const Vector3& areaScale) {
    Reset();
    auto& e = *impl_;
    if (!e.enabled || !wave.IsActive() || !FinitePosition(wave.GetCenter()) || !std::isfinite(groundY)) return;
    e.settings = settings;
    e.center = wave.GetCenter();
    e.groundY = Safe(groundY, 0.0f, -100000.0f, 100000.0f);
    e.radius = Safe(wave.GetRadius(), 0.0f, 0.0f, 500.0f);
    e.areaScale = { Safe(areaScale.x, 1.0f, 0.01f, 100.0f), 1.0f, Safe(areaScale.z, 1.0f, 0.01f, 100.0f) };
    e.hasWave = e.waveActive = true;
}

void ShockwaveEffects::Update(float dt, const Shockwave& wave, const Vector3& areaScale) {
    auto& e = *impl_;
    if (!e.enabled || !std::isfinite(dt) || dt < 0.0f) return;
    // Huge finite timesteps expire tails in one update without overflowing ages.
    const float step = std::min(dt, 10000.0f);
    e.time = std::fmod(e.time + step, 1024.0f);
    for (auto& burst : e.bursts) burst.age += step;
    std::erase_if(e.bursts, [](const Impl::RockBurst& burst) { return burst.age >= kRockLifetime; });
    if (!e.hasWave) return;

    e.areaScale = { Safe(areaScale.x, 1.0f, 0.01f, 100.0f), 1.0f, Safe(areaScale.z, 1.0f, 0.01f, 100.0f) };

    e.elapsed = std::min(e.elapsed + step, 10000.0f);
    const float nextRadius = Safe(wave.GetRadius(), e.radius, 0.0f, 500.0f);
    if (step > 0.0f) {
        e.stationaryAge = nextRadius > e.radius + 0.00001f ? 0.0f : std::min(e.stationaryAge + step, 10000.0f);
    }
    e.radius = nextRadius;
    e.waveActive = wave.IsActive();
    e.inactiveAge = e.waveActive ? 0.0f : std::min(e.inactiveAge + step, 10000.0f);
    if (!e.waveActive && e.inactiveAge >= kRingTailLifetime) e.hasWave = false;
}

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
    e.BuildRing(e.RingStrength());
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
        ImGui::PopItemWidth();
        ImGui::TextWrapped("A transparent pressure rim follows the attack radius. Rocks release short upward water glints.");
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
    stats.active = e.enabled && (e.RingStrength() > 0.001f || !e.bursts.empty());
    stats.finite = rendererStats.finite && std::isfinite(e.radius) && FinitePosition(e.center);
    stats.radius = e.radius;
    return stats;
}
