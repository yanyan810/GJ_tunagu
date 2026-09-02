#include "BossTestScene.h"

#include "Camera.h"
#include "DebugCamera.h"
#include "GameApp.h"
#include "Input.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include <algorithm>
#include <cmath>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

BossTestScene::BossTestScene() = default;
BossTestScene::~BossTestScene() = default;

namespace {
std::unique_ptr<Object3d> CreateObject(
    GameApp& app, Camera* camera, const char* modelPath,
    const Vector3& position, const Vector3& rotation, const Vector3& scale) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(app.ObjCom(), app.Dx());
    object->SetCamera(camera);
    object->SetModel(modelPath);
    object->SetTranslate(position);
    object->SetRotate(rotation);
    object->SetScale(scale);
    // Test-scene helpers must remain readable regardless of the gameplay light setup.
    object->SetEnableLighting(0);
    object->Update(0.0f);
    return object;
}
}

void BossTestScene::OnEnter(GameApp& app) {
    camera_ = std::make_unique<Camera>();
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInput(app.GetInput());
    debugCamera_->SetPosition({ 0.0f, 12.0f, -35.0f });
    debugCamera_->SetRotation({ 0.15f, 0.0f, 0.0f });
    debugCamera_->SetMoveSpeed(20.0f);

    camera_->SetTranslate(debugCamera_->GetPosition());
    camera_->SetRotate(debugCamera_->GetRotation());
    camera_->SetFarClip(2000.0f);
    camera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());

    CreateTestField_(app);
    CreateTemporaryBoss_(app);
    mineSpawnPoints_.resize(3);
    mineSpawnPoints_[0].Settings().localPosition = { -0.8f, 0.0f, 0.0f };
    mineSpawnPoints_[0].Settings().scatterDirection = { -1.0f, 0.0f, 0.0f };
    mineSpawnPoints_[1].Settings().localPosition = { 0.8f, 0.0f, 0.0f };
    mineSpawnPoints_[1].Settings().scatterDirection = { 1.0f, 0.0f, 0.0f };
    mineSpawnPoints_[2].Settings().localPosition = { 0.0f, 0.0f, -0.8f };
    mineSpawnPoints_[2].Settings().scatterDirection = { 0.0f, 0.0f, -1.0f };
    RebuildMineDebugObjects_(app);
    if (app.GetInput()) {
        // Start in UI mode. F1 explicitly switches to captured mouse-look mode.
        app.GetInput()->SetCameraControlEnabled(false);
    }
}

void BossTestScene::OnExit(GameApp& app) {
    if (app.GetInput()) {
        app.GetInput()->SetCameraControlEnabled(false);
    }
    boss_.reset();
    mines_.clear();
    pendingMineEmissions_.clear();
    minePointDebugObjects_.clear();
    mineSpawnPoints_.clear();
    distanceMarkers_.clear();
    originMarker_.reset();
    floor_.reset();
    debugCamera_.reset();
    camera_.reset();
}

void BossTestScene::CreateTestField_(GameApp& app) {
    floor_ = CreateObject(app, camera_.get(), "plane.obj",
        { 0.0f, 0.0f, 25.0f }, { 0.0f, 0.0f, 0.0f }, { 100.0f, 1.0f, 100.0f });
    floor_->SetMaterialColor({ 0.12f, 0.20f, 0.24f, 1.0f });
	appliedCausticsPreset_ = causticsPreset_;
	floor_->SetCausticsTexture(
		causticsPreset_ == CausticsPreset::DeepBroad
		? "resources/UnderwaterCausticsDeepBroadAtlas.png"
		: "resources/UnderwaterCausticsAtlas.png");
	ApplyCausticsSettings_();

    originMarker_ = CreateObject(app, camera_.get(), "axis.obj",
        { 0.0f, 0.05f, 0.0f }, {}, { 5.0f, 5.0f, 5.0f });

    for (int z = 10; z <= 80; z += 10) {
        for (int x : { -20, 0, 20 }) {
            auto marker = CreateObject(app, camera_.get(), "cube/cube.obj",
                { static_cast<float>(x), 0.5f, static_cast<float>(z) }, {}, { 0.35f, 1.0f, 0.35f });
            marker->SetMaterialColor(z % 20 == 0
                ? Vector4{ 0.2f, 0.8f, 1.0f, 1.0f }
                : Vector4{ 0.4f, 0.5f, 0.55f, 1.0f });
            distanceMarkers_.push_back(std::move(marker));
        }
    }
}

void BossTestScene::ApplyCausticsSettings_() {
	if (!floor_) {
		return;
	}
	if (causticsPreset_ != appliedCausticsPreset_) {
		appliedCausticsPreset_ = causticsPreset_;
		floor_->SetCausticsTexture(
			causticsPreset_ == CausticsPreset::DeepBroad
			? "resources/UnderwaterCausticsDeepBroadAtlas.png"
			: "resources/UnderwaterCausticsAtlas.png");
	}
	floor_->SetCausticsSettings(
		causticsEnabled_, causticsScale_, causticsIntensity_,
		{ 0.75f, 0.92f, 1.0f });
	floor_->SetCausticsAnimationSettings(
		causticsAnimationEnabled_, causticsPlaybackTime_,
		causticsLoopDuration_, 24, 6, 4);
}

void BossTestScene::CreateTemporaryBoss_(GameApp& app) {
    // Replace only this model path when the final ship asset becomes available.
    boss_ = CreateObject(app, camera_.get(), "cube/cube.obj",
        bossPosition_, bossRotation_, bossScale_);
    boss_->SetMaterialColor({ 0.55f, 0.18f, 0.12f, 1.0f });
}

void BossTestScene::ApplyBossTransform_() {
    if (!boss_) return;
    boss_->SetTranslate(bossPosition_);
    boss_->SetRotate(bossRotation_);
    boss_->SetScale(bossScale_);
}

void BossTestScene::ResetTestObjects_() {
    bossPosition_ = { 0.0f, 3.0f, 25.0f };
    bossRotation_ = {};
    bossScale_ = { 12.0f, 3.0f, 24.0f };
    ApplyBossTransform_();
    mines_.clear();
    pendingMineEmissions_.clear();
    nextMineEmission_ = 0;
}

void BossTestScene::AddMineSpawnPoint_(GameApp& app) {
    mineSpawnPoints_.emplace_back();
    selectedMineSpawnPoint_ = static_cast<int>(mineSpawnPoints_.size()) - 1;
    RebuildMineDebugObjects_(app);
}

void BossTestScene::RemoveSelectedMineSpawnPoint_() {
    if (mineSpawnPoints_.empty()) return;
    const int removeIndex = selectedMineSpawnPoint_;
    mineSpawnPoints_.erase(mineSpawnPoints_.begin() + removeIndex);
    if (removeIndex < static_cast<int>(minePointDebugObjects_.size())) {
        minePointDebugObjects_.erase(minePointDebugObjects_.begin() + removeIndex);
    }
    selectedMineSpawnPoint_ = std::clamp(selectedMineSpawnPoint_, 0,
        std::max(0, static_cast<int>(mineSpawnPoints_.size()) - 1));
}

void BossTestScene::RebuildMineDebugObjects_(GameApp& app) {
    minePointDebugObjects_.clear();
    minePointDebugObjects_.reserve(mineSpawnPoints_.size());
    for (size_t i = 0; i < mineSpawnPoints_.size(); ++i) {
        MinePointDebugObjects debug;
        debug.origin = CreateObject(app, camera_.get(), "cube/cube.obj", {}, {}, { 0.8f, 0.8f, 0.8f });
        debug.origin->SetMaterialColor({ 1.0f, 0.75f, 0.05f, 1.0f });
        for (int segment = 0; segment < 6; ++segment) {
            auto object = CreateObject(app, camera_.get(), "cube/cube.obj", {}, {}, { 0.18f, 0.18f, 0.18f });
            object->SetMaterialColor({ 1.0f, 0.35f, 0.05f, 1.0f });
            debug.direction.push_back(std::move(object));
        }
        for (int edge = 0; edge < 12; ++edge) {
            auto object = CreateObject(app, camera_.get(), "cube/cube.obj", {}, {}, { 0.1f, 0.1f, 0.1f });
            object->SetMaterialColor({ 0.15f, 1.0f, 0.45f, 1.0f });
            debug.rangeEdges.push_back(std::move(object));
        }
        minePointDebugObjects_.push_back(std::move(debug));
    }
}

void BossTestScene::UpdateMineDebugObjects_() {
    const Matrix4x4 bossWorld = Matrix4x4::MakeAffineMatrix(bossScale_, bossRotation_, bossPosition_);
    const Matrix4x4 bossRotationMatrix = Matrix4x4::RotateXYZ(bossRotation_.x, bossRotation_.y, bossRotation_.z);
    const auto rotateOffset = [&bossRotationMatrix](const Vector3& value) {
        return Vector3{
            value.x * bossRotationMatrix.m[0][0] + value.y * bossRotationMatrix.m[1][0] + value.z * bossRotationMatrix.m[2][0],
            value.x * bossRotationMatrix.m[0][1] + value.y * bossRotationMatrix.m[1][1] + value.z * bossRotationMatrix.m[2][1],
            value.x * bossRotationMatrix.m[0][2] + value.y * bossRotationMatrix.m[1][2] + value.z * bossRotationMatrix.m[2][2]
        };
    };
    for (size_t i = 0; i < mineSpawnPoints_.size() && i < minePointDebugObjects_.size(); ++i) {
        const auto& point = mineSpawnPoints_[i];
        auto& debug = minePointDebugObjects_[i];
        const Vector3 origin = point.GetWorldPosition(bossWorld);
        const Vector3 direction = point.GetWorldDirection(bossRotation_);
        const Vector3 center = point.GetScatterCenter(bossWorld, bossRotation_);
        const Vector3 range = point.Settings().scatterRange;

        debug.origin->SetTranslate(origin);
        debug.origin->SetScale(i == static_cast<size_t>(selectedMineSpawnPoint_)
            ? Vector3{ 1.2f, 1.2f, 1.2f } : Vector3{ 0.8f, 0.8f, 0.8f });
        debug.origin->Update(0.0f);
        for (size_t segment = 0; segment < debug.direction.size(); ++segment) {
            const float distance = point.Settings().scatterDistance *
                (static_cast<float>(segment) + 1.0f) / (static_cast<float>(debug.direction.size()) + 1.0f);
            debug.direction[segment]->SetTranslate(origin + direction * distance);
            debug.direction[segment]->Update(0.0f);
        }

        // Twelve thin cubes form a simple wireframe box around the scatter area.
        const float hx = std::max(0.1f, range.x * 0.5f);
        const float hy = std::max(0.1f, range.y * 0.5f);
        const float hz = std::max(0.1f, range.z * 0.5f);
        int edge = 0;
        for (float y : { -hy, hy }) for (float z : { -hz, hz }) {
            auto& object = debug.rangeEdges[edge++]; object->SetTranslate(center + rotateOffset({ 0.0f, y, z })); object->SetRotate(bossRotation_); object->SetScale({ hx, 0.08f, 0.08f }); object->Update(0.0f);
        }
        for (float x : { -hx, hx }) for (float z : { -hz, hz }) {
            auto& object = debug.rangeEdges[edge++]; object->SetTranslate(center + rotateOffset({ x, 0.0f, z })); object->SetRotate(bossRotation_); object->SetScale({ 0.08f, hy, 0.08f }); object->Update(0.0f);
        }
        for (float x : { -hx, hx }) for (float y : { -hy, hy }) {
            auto& object = debug.rangeEdges[edge++]; object->SetTranslate(center + rotateOffset({ x, y, 0.0f })); object->SetRotate(bossRotation_); object->SetScale({ 0.08f, 0.08f, hz }); object->Update(0.0f);
        }
    }
}

void BossTestScene::TestFireMines_(GameApp& app) {
    (void)app;
    mines_.clear();
    pendingMineEmissions_.clear();
    nextMineEmission_ = 0;
    mineLaunchTimer_ = 0.0f;
    if (mineSpawnPoints_.empty()) return;
    const Matrix4x4 bossWorld = Matrix4x4::MakeAffineMatrix(bossScale_, bossRotation_, bossPosition_);
    pendingMineEmissions_ = mineSpawnPoints_[selectedMineSpawnPoint_].GenerateSamples(
        bossWorld, bossRotation_, random_);
}

void BossTestScene::UpdateMineLaunchQueue_(GameApp& app, float dt) {
    if (nextMineEmission_ >= pendingMineEmissions_.size()) return;

    mineLaunchTimer_ -= dt;
    const float interval = std::max(0.0f, mineLaunchInterval_);
    while (nextMineEmission_ < pendingMineEmissions_.size() && mineLaunchTimer_ <= 0.0f) {
        auto mine = std::make_unique<Mine>();
        mine->Initialize(
            app.ObjCom(), app.Dx(), camera_.get(),
            pendingMineEmissions_[nextMineEmission_], mineMotionSettings_);
        mines_.push_back(std::move(mine));
        ++nextMineEmission_;
        if (interval > 0.0f) {
            mineLaunchTimer_ += interval;
            break;
        }
    }

    if (nextMineEmission_ >= pendingMineEmissions_.size()) {
        pendingMineEmissions_.clear();
        nextMineEmission_ = 0;
    }
}

void BossTestScene::ProcessMineExplosions_() {
    std::vector<MineExplosionEvent> explosions;
    for (auto& mine : mines_) {
        MineExplosionEvent event;
        if (mine->ConsumeExplosionEvent(event)) explosions.push_back(event);
    }

    for (const auto& explosion : explosions) {
        const float radiusSquared = explosion.radius * explosion.radius;
        for (auto& other : mines_) {
            if (other->GetState() == Mine::State::Triggered ||
                other->GetState() == Mine::State::Exploded) continue;
            const Vector3 delta = other->GetPosition() - explosion.position;
            const float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            if (distanceSquared <= radiusSquared) {
                other->TriggerExplosion(chainReactionFuseTime_);
            }
        }
    }
}

void BossTestScene::TriggerAllMines_() {
    std::uniform_real_distribution<float> jitter(0.0f, std::max(0.0f, triggerAllFuseJitter_));
    for (auto& mine : mines_) {
        mine->TriggerExplosion(normalMineFuseTime_ + jitter(random_));
    }
}

void BossTestScene::Update(GameApp& app, float dt) {
    Input* input = app.GetInput();
    if (input && input->IsKeyTrigger(DIK_F2)) {
        RequestChangeScene_("Game");
        return;
    }

    // ImGui is drawn after the scene render pass. GPU-backed objects must be
    // created/destroyed here, before drawing starts, rather than in DrawImGui.
    if (pendingAddMineSpawnPoint_) {
        AddMineSpawnPoint_(app);
        pendingAddMineSpawnPoint_ = false;
    }
    if (pendingRemoveMineSpawnPoint_) {
        RemoveSelectedMineSpawnPoint_();
        pendingRemoveMineSpawnPoint_ = false;
    }
    if (pendingTestFireMines_) {
        TestFireMines_(app);
        pendingTestFireMines_ = false;
    }
    if (pendingClearTestMines_) {
        mines_.clear();
        pendingMineEmissions_.clear();
        nextMineEmission_ = 0;
        pendingClearTestMines_ = false;
    }
    if (pendingResetTestObjects_) {
        ResetTestObjects_();
        pendingResetTestObjects_ = false;
    }
    if (pendingTriggerAllMines_) {
        TriggerAllMines_();
        pendingTriggerAllMines_ = false;
    }
    if (pendingTriggerFirstMine_) {
        for (auto& mine : mines_) {
            if (mine->TriggerExplosion(normalMineFuseTime_)) break;
        }
        pendingTriggerFirstMine_ = false;
    }
    if (input && input->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }

    if (debugCamera_ && camera_) {
        debugCamera_->Update(dt);
        camera_->SetTranslate(debugCamera_->GetPosition());
        camera_->SetRotate(debugCamera_->GetRotation());
        camera_->Update();
    }

    ApplyBossTransform_();
	if (causticsAnimationEnabled_ && causticsLoopDuration_ > 0.0f) {
		causticsPlaybackTime_ = std::fmod(
			causticsPlaybackTime_ + std::max(dt, 0.0f),
			causticsLoopDuration_);
	}
	ApplyCausticsSettings_();
    UpdateMineDebugObjects_();
    UpdateMineLaunchQueue_(app, dt);
    if (floor_) floor_->Update(dt);
    if (originMarker_) originMarker_->Update(dt);
    for (auto& marker : distanceMarkers_) marker->Update(dt);
    if (boss_) boss_->Update(dt);
    for (auto& mine : mines_) mine->Update(dt);
    ProcessMineExplosions_();
}

void BossTestScene::Draw(GameApp& /*app*/) {
    if (floor_) floor_->Draw();
    if (originMarker_) originMarker_->Draw();
    for (const auto& marker : distanceMarkers_) marker->Draw();
    if (boss_) boss_->Draw();
    for (const auto& debug : minePointDebugObjects_) {
        if (debug.origin) debug.origin->Draw();
        for (const auto& object : debug.direction) object->Draw();
        for (const auto& object : debug.rangeEdges) object->Draw();
    }
    for (const auto& mine : mines_) mine->Draw();
}

void BossTestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("Boss Test Scene");

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        float speed = debugCamera_ ? debugCamera_->GetMoveSpeed() : 0.0f;
        if (ImGui::DragFloat("Move Speed", &speed, 0.5f, 0.5f, 200.0f) && debugCamera_) {
            debugCamera_->SetMoveSpeed(speed);
        }
        const Vector3 position = debugCamera_ ? debugCamera_->GetPosition() : Vector3{};
        const Vector3 rotation = debugCamera_ ? debugCamera_->GetRotation() : Vector3{};
        ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
        ImGui::Text("Rotation: %.2f, %.2f, %.2f", rotation.x, rotation.y, rotation.z);
        ImGui::TextDisabled("WASD move / Space up / Shift down");
        ImGui::TextDisabled("F1: toggle UI cursor / mouse look, F2: return to Game");
        ImGui::Text("Mouse Look: %s",
            app.GetInput() && app.GetInput()->IsCameraControlEnabled() ? "ON" : "OFF");
    }

    if (ImGui::CollapsingHeader("Boss", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position##Boss", &bossPosition_.x, 0.1f);
        ImGui::DragFloat3("Rotation##Boss", &bossRotation_.x, 0.01f);
        ImGui::DragFloat3("Scale##Boss", &bossScale_.x, 0.1f, 0.1f, 500.0f);
    }

    if (ImGui::CollapsingHeader("Boss Attacks", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Mine Spawn Points");
        if (ImGui::Button("Add Spawn Point")) pendingAddMineSpawnPoint_ = true;
        ImGui::SameLine();
        if (mineSpawnPoints_.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Selected")) pendingRemoveMineSpawnPoint_ = true;
        if (mineSpawnPoints_.empty()) ImGui::EndDisabled();

        for (int i = 0; i < static_cast<int>(mineSpawnPoints_.size()); ++i) {
            char label[64];
            snprintf(label, sizeof(label), "MineSpawnPoint %d", i);
            if (ImGui::Selectable(label, selectedMineSpawnPoint_ == i)) selectedMineSpawnPoint_ = i;
        }

        if (!mineSpawnPoints_.empty()) {
            auto& settings = mineSpawnPoints_[selectedMineSpawnPoint_].Settings();
            ImGui::Separator();
            ImGui::DragFloat3("Local Position", &settings.localPosition.x, 0.05f);
            ImGui::DragInt("Mine Count", &settings.mineCount, 1.0f, 1, 256);
            ImGui::DragFloat3("Scatter Range", &settings.scatterRange.x, 0.25f, 0.0f, 200.0f);
            ImGui::DragFloat("Minimum Spacing", &settings.minimumSpacing, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat3("Scatter Direction", &settings.scatterDirection.x, 0.02f, -1.0f, 1.0f);
            ImGui::DragFloat("Scatter Distance", &settings.scatterDistance, 0.25f, 0.0f, 500.0f);
            ImGui::DragFloat("Scatter Angle (deg)", &settings.scatterAngleDegrees, 0.5f, 0.0f, 80.0f);
            ImGui::DragFloat("Initial Speed", &settings.initialSpeed, 0.25f, 0.0f, 200.0f);
            if (ImGui::Button("Test Fire Selected")) pendingTestFireMines_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Clear Test Mines")) pendingClearTestMines_ = true;
        }
        ImGui::SeparatorText("Mine Motion");
        ImGui::DragFloat("Damping / Drag", &mineMotionSettings_.drag, 0.02f, 0.0f, 20.0f);
        ImGui::DragFloat("Floating Transition Speed", &mineMotionSettings_.floatingTransitionSpeed, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat3("Floating Amplitude", &mineMotionSettings_.floatingAmplitude.x, 0.02f, 0.0f, 20.0f);
        ImGui::DragFloat("Floating Speed", &mineMotionSettings_.floatingSpeed, 0.02f, 0.0f, 20.0f);
        ImGui::DragFloat3("Rotation Speed", &mineMotionSettings_.rotationSpeed.x, 0.01f, -10.0f, 10.0f);
        ImGui::DragFloat("Launch Interval", &mineLaunchInterval_, 0.01f, 0.0f, 2.0f);
        ImGui::SeparatorText("Mine Explosion");
        ImGui::DragFloat("Normal Fuse Time", &normalMineFuseTime_, 0.02f, 0.0f, 10.0f);
        ImGui::DragFloat("Chain Reaction Fuse Time", &chainReactionFuseTime_, 0.02f, 0.0f, 10.0f);
        ImGui::DragFloat("Trigger All Jitter", &triggerAllFuseJitter_, 0.01f, 0.0f, 2.0f);
        if (ImGui::DragFloat("Explosion Radius", &mineMotionSettings_.explosionRadius, 0.1f, 0.1f, 100.0f)) {
            for (auto& mine : mines_) mine->SetExplosionRadius(mineMotionSettings_.explosionRadius);
        }
        if (ImGui::Button("Trigger First Mine (Chain Test)")) pendingTriggerFirstMine_ = true;
        ImGui::SameLine();
        if (ImGui::Button("Trigger All Mines")) pendingTriggerAllMines_ = true;

        int flyingCount = 0;
        int floatingCount = 0;
        int triggeredCount = 0;
        int explodedCount = 0;
        for (const auto& mine : mines_) {
            if (mine->GetState() == Mine::State::Flying) ++flyingCount;
            if (mine->GetState() == Mine::State::Floating) ++floatingCount;
            if (mine->GetState() == Mine::State::Triggered) ++triggeredCount;
            if (mine->GetState() == Mine::State::Exploded) ++explodedCount;
        }
        const int queuedCount = static_cast<int>(pendingMineEmissions_.size()) - static_cast<int>(nextMineEmission_);
        ImGui::Text("Mines: %d (Fly %d / Float %d / Trigger %d / Exploded %d / Queue %d)",
            static_cast<int>(mines_.size()), flyingCount, floatingCount,
            triggeredCount, explodedCount, std::max(0, queuedCount));
        if (ImGui::TreeNode("Mine Details")) {
            for (size_t i = 0; i < mines_.size(); ++i) {
                const Vector3& position = mines_[i]->GetPosition();
                const Vector3& velocity = mines_[i]->GetVelocity();
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%d: %s", static_cast<int>(i), Mine::StateName(mines_[i]->GetState()));
                ImGui::Text("  P %.2f, %.2f, %.2f  V %.2f, %.2f, %.2f",
                    position.x, position.y, position.z, velocity.x, velocity.y, velocity.z);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Other attacks will be added here later.");
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Reset Test Objects")) {
            pendingResetTestObjects_ = true;
        }
    }
	if (ImGui::CollapsingHeader("Underwater / Caustics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable", &causticsEnabled_);
		int preset = causticsPreset_ == CausticsPreset::DeepBroad ? 1 : 0;
		if (ImGui::Combo("Preset", &preset, "Shallow / Fine\0Deep / Broad\0")) {
			causticsPreset_ = preset == 1
				? CausticsPreset::DeepBroad
				: CausticsPreset::ShallowFine;
		}
		ImGui::DragFloat("Scale", &causticsScale_, 0.001f, 0.001f, 0.2f, "%.3f");
		ImGui::DragFloat("Intensity", &causticsIntensity_, 0.01f, 0.0f, 4.0f, "%.2f");
		ImGui::Checkbox("Animation Enabled", &causticsAnimationEnabled_);
		ImGui::DragFloat("Loop Duration", &causticsLoopDuration_, 0.1f, 0.1f, 20.0f, "%.1f sec");
	}
    ImGui::End();
#else
    (void)app;
#endif
}
