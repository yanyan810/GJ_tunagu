#include "boss/BossReadability.h"
#include "Object3d.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

void BossReadability::Apply(Object3d& ship, const Camera* camera, bool damageFlash) const {
    ship.SetEnableOutline(enabled_ && camera);
    if (!enabled_ || !camera) { return; }
    // Each GLTF node gets the same render-pixel width, including nodes with
    // non-uniform or mirrored scales. Refraction bends the resulting source rim.
    ship.SetOutlinePixelWidth(widthPixels_);
    const Vector3 tint = damageFlash ? Vector3{ 1.0f, 0.15f, 0.03f } : color_;
    ship.SetOutlineColor({ tint.x * intensity_, tint.y * intensity_, tint.z * intensity_, 1.0f });
}

void BossReadability::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::SeparatorText("Boss Readability");
    ImGui::Checkbox("Refracted Boss Outline", &enabled_);
    ImGui::SliderFloat("Boss Outline Width", &widthPixels_, 1.0f, 4.0f, "%.1f render px");
    ImGui::SliderFloat("Boss Outline Radiance", &intensity_, 0.4f, 2.0f, "%.2f");
    ImGui::ColorEdit3("Boss Outline Color", &color_.x);
#endif
}
