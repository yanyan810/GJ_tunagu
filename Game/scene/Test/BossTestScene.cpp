#include "BossTestScene.h"

#include "Camera.h"
#include "DebugCamera.h"
#include "GameApp.h"
#include "Input.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "GeometryGenerator.h"
#include "ModelManager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

BossTestScene::BossTestScene() = default;
BossTestScene::~BossTestScene() = default;

namespace {
constexpr int kAnchorPredictionRingCount = 9;
constexpr int kAnchorChainHardLimit = 256;
constexpr float kDegreesToRadians = 0.01745329251994329577f;

struct AnchorCollisionBoxTemplate {
    Vector3 localCenter;
    Vector3 halfSize;
    Vector3 localRotation;
};

const AnchorCollisionBoxTemplate kAnchorCollisionBoxes[] = {
    { { 0.0f, 3.35f, 0.17f }, { 0.62f, 4.55f, 0.80f }, {} },
    { { -2.85f, 0.45f, 0.17f }, { 2.65f, 0.62f, 0.80f }, { 0.0f, 0.0f, -0.48f } },
    { {  2.85f, 0.45f, 0.17f }, { 2.65f, 0.62f, 0.80f }, { 0.0f, 0.0f,  0.48f } },
    { { -5.05f, 2.35f, 0.17f }, { 0.75f, 1.15f, 0.80f }, { 0.0f, 0.0f, -0.35f } },
    { {  5.05f, 2.35f, 0.17f }, { 0.75f, 1.15f, 0.80f }, { 0.0f, 0.0f,  0.35f } }
};

Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2]
    };
}

float Length(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vector3 Normalize(const Vector3& value) {
    const float length = Length(value);
    return length > 0.00001f ? value * (1.0f / length) : Vector3{};
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// The generated torus lies in XZ. Keep its elongated local Z axis along the
// chain while local Y controls the plane normal used for alternating links.
Vector3 TorusRotation(const Vector3& direction, const Vector3& normal) {
    const Vector3 localZ = Normalize(direction);
    const Vector3 localY = Normalize(normal);
    const Vector3 localX = Normalize(Cross(localY, localZ));

    const float rotateY = std::asin(std::clamp(-localX.z, -1.0f, 1.0f));
    const float cosY = std::cos(rotateY);
    if (std::abs(cosY) > 0.0001f) {
        return {
            std::atan2(localY.z, localZ.z),
            rotateY,
            std::atan2(localX.y, localX.x)
        };
    }
    return { std::atan2(-localZ.y, localY.y), rotateY, 0.0f };
}
constexpr const char* kMineSettingsPath = "resources/Data/BossAttacks.json";

Model::ModelData MakeBossTestPrimitiveModelData(const std::vector<Model::VertexData>& vertices) {
    Model::ModelData data{};
    data.materials.push_back({ "" });
    Model::MeshData mesh{};
    mesh.materialIndex = 0;
    mesh.vertices = vertices;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(vertices.size());
    data.meshes.push_back(std::move(mesh));
    data.indices.resize(vertices.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) data.indices[i] = i;
    data.rootNode.name = "BossTestPrimitiveRoot";
    data.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
    data.rootNode.meshIndices.push_back(0);
    return data;
}

Model* GetBossTestPrimitiveModel(const char* key, bool torus) {
    if (Model* model = ModelManager::GetInstance()->FindModel(key)) return model;
    const auto vertices = torus
        ? GeometryGenerator::GenerateTorusTriList(64, 16, 1.0f, 0.045f)
        : GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    return ModelManager::GetInstance()->CreatePrimitiveModel(
        key, MakeBossTestPrimitiveModelData(vertices));
}

Model* GetBossTestChainTorusModel() {
    constexpr const char* key = "BossTest_AnchorChainTorus";
    if (Model* model = ModelManager::GetInstance()->FindModel(key)) return model;
    const auto vertices = GeometryGenerator::GenerateTorusTriList(48, 16, 1.0f, 0.22f);
    return ModelManager::GetInstance()->CreatePrimitiveModel(
        key, MakeBossTestPrimitiveModelData(vertices));
}

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


std::unique_ptr<Object3d> CreatePrimitiveObject(
    GameApp& app, Camera* camera, Model* model,
    const Vector3& position, const Vector3& rotation, const Vector3& scale) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(app.ObjCom(), app.Dx());
    object->SetCamera(camera);
    object->SetModel(model);
    object->SetTranslate(position);
    object->SetRotate(rotation);
    object->SetScale(scale);
    object->SetEnableLighting(0);
    object->Update(0.0f);
    return object;
}
}

void BossTestScene::OnEnter(GameApp& app) {
    LoadMineSettings_();
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
    shockwaveVisual_ = CreateObject(app, camera_.get(), "ring.obj", {}, {}, { 1.0f, 1.0f, 1.0f });
    shockwaveVisual_->SetMaterialColor({ 0.1f, 0.8f, 1.0f, 0.8f });
    Model* anchorOrbitTorus = GetBossTestPrimitiveModel("BossTest_AnchorOrbitTorus", true);
    anchorObject_ = CreateObject(app, camera_.get(), "ancor/anchor.obj", {}, {}, { 1.0f, 1.0f, 1.0f });
    anchorWarningRing_ = CreatePrimitiveObject(app, camera_.get(), anchorOrbitTorus, {}, {}, { 1.0f, 1.0f, 1.0f });
    anchorWarningRing_->SetMaterialColor({ 1.0f, 0.18f, 0.05f, 0.85f });
    anchorCollisionDebug_.reserve(std::size(kAnchorCollisionBoxes));
    for (size_t i = 0; i < std::size(kAnchorCollisionBoxes); ++i) {
        auto box = CreateObject(app, camera_.get(), "cube/cube.obj", {}, {}, { 1.0f, 1.0f, 1.0f });
        box->SetMaterialColor({ 0.15f, 1.0f, 0.3f, 0.35f });
        anchorCollisionDebug_.push_back(std::move(box));
    }
    anchorOrbitDebug_.reserve(kAnchorPredictionRingCount);
    for (int i = 0; i < kAnchorPredictionRingCount; ++i) {
        auto ring = CreatePrimitiveObject(app, camera_.get(), anchorOrbitTorus, {}, {}, { 1.0f, 1.0f, 1.0f });
        ring->SetMaterialColor({ 0.1f, 0.75f, 1.0f, 0.65f });
        anchorOrbitDebug_.push_back(std::move(ring));
    }
    anchorPositionDebug_ = CreateObject(app, camera_.get(), "ancor/anchor.obj", {}, {}, { 1.0f, 1.0f, 1.0f });
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
    shockwaveRocks_.clear();
    shockwaveVisual_.reset();
    anchorCollisionDebug_.clear();
    anchorPositionDebug_.reset();
    anchorOrbitDebug_.clear();
    anchorChainLinks_.clear();
    anchorChainVisibleCount_ = 0;
    anchorWarningRing_.reset();
    anchorObject_.reset();
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
    ResetShockwave_();
    ResetAnchor_();
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
    if (mineSpawnPoints_.empty()) return;
    const Matrix4x4 bossWorld = Matrix4x4::MakeAffineMatrix(bossScale_, bossRotation_, bossPosition_);
    auto newEmissions = mineSpawnPoints_[selectedMineSpawnPoint_].GenerateSamples(
        bossWorld, bossRotation_, random_);
    const bool queueWasEmpty = pendingMineEmissions_.empty();
    pendingMineEmissions_.insert(
        pendingMineEmissions_.end(), newEmissions.begin(), newEmissions.end());
    if (queueWasEmpty) {
        nextMineEmission_ = 0;
        mineLaunchTimer_ = 0.0f;
    }
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

void BossTestScene::TriggerShockwave_() {
    shockwaveAffectedMines_.clear();
    const Vector3 center = bossPosition_ + shockwavePositionOffset_;
    shockwave_.Trigger(center, center.y, shockwaveSettings_, random_);
}

void BossTestScene::ResetShockwave_() {
    shockwave_.Reset();
    shockwaveRocks_.clear();
    shockwaveAffectedMines_.clear();
}

void BossTestScene::TriggerAnchor_() {
    anchorAttack_.Trigger(bossPosition_ + anchorCenterOffset_, anchorSettings_);
}

void BossTestScene::ResetAnchor_() {
    anchorAttack_.Reset();
}

void BossTestScene::UpdateAnchor_(GameApp& app, float dt) {
    const Vector3 anchorCenter = bossPosition_ + anchorCenterOffset_;
    if (anchorAttack_.IsRunning()) anchorAttack_.SetCenter(anchorCenter);
    anchorAttack_.Update(dt);
    // AnchorAttack owns the complete model orientation. Do not compose the
    // orbit angle or model offset a second time in the test scene.
    const Vector3 anchorVisualRotation = anchorAttack_.GetSelfRotation();
    const float anchorOverallScale = std::max(0.01f, anchorSettings_.overallScale);
    const Vector3 anchorFinalScale{
        std::max(0.01f, anchorSettings_.modelScale.x) * anchorOverallScale,
        std::max(0.01f, anchorSettings_.modelScale.y) * anchorOverallScale,
        std::max(0.01f, anchorSettings_.modelScale.z) * anchorOverallScale
    };
    const Matrix4x4 anchorWorld = Matrix4x4::MakeAffineMatrix(
        anchorFinalScale, anchorVisualRotation, anchorAttack_.GetPosition());
    if (anchorWarningRing_) {
        const float pulse = anchorAttack_.GetWarningPulseScale();
        const float radius = std::max(0.0f, anchorSettings_.radius) * pulse;
        anchorWarningRing_->SetTranslate(anchorAttack_.IsRunning() ? anchorAttack_.GetCenter() : anchorCenter);
        anchorWarningRing_->SetScale({
            radius,
            std::max(0.01f, anchorSettings_.warningRing.thickness),
            radius
        });
        const float brightness = 0.55f + (pulse - 1.0f) * 1.5f;
        anchorWarningRing_->SetMaterialColor({ 1.0f, std::clamp(brightness, 0.05f, 0.85f), 0.03f, 0.85f });
        anchorWarningRing_->Update(dt);
    }
    if (anchorObject_) {
        anchorObject_->SetTranslate(anchorAttack_.GetPosition());
        anchorObject_->SetRotate(anchorVisualRotation);
        anchorObject_->SetScale(anchorFinalScale);
        anchorObject_->Update(dt);
    }
    if (!anchorCollisionDebug_.empty()) {
        for (size_t i = 0; i < anchorCollisionDebug_.size() && i < std::size(kAnchorCollisionBoxes); ++i) {
            const auto& source = kAnchorCollisionBoxes[i];
            auto& box = anchorCollisionDebug_[i];
            box->SetTranslate(TransformPoint(source.localCenter, anchorWorld));
            box->SetRotate(anchorVisualRotation + source.localRotation);
            box->SetScale({
                source.halfSize.x * anchorFinalScale.x,
                source.halfSize.y * anchorFinalScale.y,
                source.halfSize.z * anchorFinalScale.z
            });
            box->Update(dt);
        }
    }
    if (!anchorOrbitDebug_.empty()) {
        const float radius = std::max(0.0f, anchorSettings_.radius);
        const float width = std::max(0.0f, anchorSettings_.predictionLineWidth);
        for (size_t i = 0; i < anchorOrbitDebug_.size(); ++i) {
            const float t = anchorOrbitDebug_.size() > 1
                ? static_cast<float>(i) / static_cast<float>(anchorOrbitDebug_.size() - 1)
                : 0.5f;
            const float ringRadius = std::max(0.01f, radius + (t - 0.5f) * width);
            anchorOrbitDebug_[i]->SetTranslate(anchorCenter);
            anchorOrbitDebug_[i]->SetScale({ ringRadius, 0.12f, ringRadius });
            anchorOrbitDebug_[i]->Update(dt);
        }
    }
    if (anchorPositionDebug_) {
        const float overallScale = std::max(0.01f, anchorSettings_.overallScale);
        const Vector3 previewPosition = anchorAttack_.IsRunning()
            ? anchorAttack_.GetPosition()
            : anchorCenter + Vector3{ std::max(0.0f, anchorSettings_.radius), 0.0f, 0.0f };
        anchorPositionDebug_->SetTranslate(previewPosition);
        anchorPositionDebug_->SetRotate(anchorSettings_.modelRotationOffset);
        anchorPositionDebug_->SetScale({
            std::max(0.01f, anchorSettings_.modelScale.x) * overallScale,
            std::max(0.01f, anchorSettings_.modelScale.y) * overallScale,
            std::max(0.01f, anchorSettings_.modelScale.z) * overallScale
        });
        anchorPositionDebug_->Update(dt);
    }

    anchorChainVisibleCount_ = 0;
    const bool chainAttackVisible = anchorAttack_.IsAnchorVisible();
    const bool chainDebugPreview = showAnchorChainPreview_ &&
        anchorAttack_.GetState() == AnchorAttack::State::Inactive;
    if (chainAttackVisible || chainDebugPreview) {
        const int maxLinks = std::clamp(anchorSettings_.chain.maxLinks, 0, kAnchorChainHardLimit);
        Model* torusModel = GetBossTestChainTorusModel();
        while (static_cast<int>(anchorChainLinks_.size()) < maxLinks) {
            auto link = CreatePrimitiveObject(app, camera_.get(), torusModel, {}, {}, { 1.0f, 1.0f, 1.0f });
            link->SetMaterialColor({ 0.42f, 0.46f, 0.52f, 1.0f });
            anchorChainLinks_.push_back(std::move(link));
        }

        const Vector3 start = chainAttackVisible
            ? anchorAttack_.GetCenter() + anchorSettings_.spawnLocalPosition
            : anchorCenter + anchorSettings_.spawnLocalPosition;
        Vector3 end{};
        if (chainAttackVisible) {
            end = TransformPoint(anchorSettings_.chain.anchorLocalAttachPosition, anchorWorld) +
                anchorSettings_.chain.endOffset;
        } else {
            const Vector3 previewAnchorPosition =
                anchorCenter + Vector3{ std::max(0.0f, anchorSettings_.radius), 0.0f, 0.0f };
            const Matrix4x4 previewAnchorWorld = Matrix4x4::MakeAffineMatrix(
                anchorFinalScale, anchorSettings_.modelRotationOffset, previewAnchorPosition);
            end = TransformPoint(anchorSettings_.chain.anchorLocalAttachPosition, previewAnchorWorld) +
                anchorSettings_.chain.endOffset;
        }
        const Vector3 delta = end - start;
        const float distance = Length(delta);
        const float spacing = std::max(0.05f, anchorSettings_.chain.spacing);
        const Vector3 direction = Normalize(delta);
        const int uncappedRequiredLinks = distance > 0.0001f
            ? static_cast<int>(std::ceil(distance / spacing)) + 1
            : 0;
        const int requiredLinks = std::min(maxLinks, uncappedRequiredLinks);
        anchorChainVisibleCount_ = static_cast<size_t>(std::max(0, requiredLinks));

        if (anchorChainVisibleCount_ > 0) {
            Vector3 firstNormal = Cross(direction, { 0.0f, 1.0f, 0.0f });
            if (Length(firstNormal) < 0.001f) firstNormal = Cross(direction, { 1.0f, 0.0f, 0.0f });
            firstNormal = Normalize(firstNormal);
            const Vector3 secondNormalBase = Normalize(Cross(direction, firstNormal));
            const float alternateRadians = anchorSettings_.chain.alternateRotationDegrees * kDegreesToRadians;
            const float cosine = std::cos(alternateRadians);
            const float sine = std::sin(alternateRadians);
            const Vector3 alternateNormal = firstNormal * cosine + secondNormalBase * sine;

            for (size_t i = 0; i < anchorChainVisibleCount_; ++i) {
                float fromAnchor = 0.0f;
                if (uncappedRequiredLinks <= maxLinks) {
                    // Keep existing links at stable spacing from the anchor.
                    // Only the final link is clamped to the ship point, so a
                    // new link emerges there overlapped instead of popping in
                    // halfway along the chain.
                    fromAnchor = std::min(distance, spacing * static_cast<float>(i));
                } else {
                    // If Max Links is reached, retain both endpoint contacts.
                    const float t = anchorChainVisibleCount_ > 1
                        ? static_cast<float>(i) / static_cast<float>(anchorChainVisibleCount_ - 1)
                        : 1.0f;
                    fromAnchor = distance * t;
                }
                auto& link = anchorChainLinks_[i];
                link->SetTranslate(end - direction * fromAnchor);
                link->SetRotate(TorusRotation(direction, (i % 2) == 0 ? firstNormal : alternateNormal));
                link->SetScale({
                    std::max(0.01f, anchorSettings_.chain.scale.x),
                    std::max(0.01f, anchorSettings_.chain.scale.y),
                    std::max(0.01f, anchorSettings_.chain.scale.z)
                });
                link->Update(dt);
            }
        }
    }
}

void BossTestScene::UpdateShockwave_(GameApp& app, float dt) {
    const bool waveWasActive = shockwave_.IsActive();
    shockwave_.Update(dt);

    for (const auto& spawn : shockwave_.ConsumeRockSpawns()) {
        ShockwaveRockSpawn scaledSpawn = spawn;
        const Vector3 relative = spawn.position - shockwave_.GetCenter();
        scaledSpawn.position = shockwave_.GetCenter() + Vector3{
            relative.x * std::max(0.01f, shockwaveAreaScale_.x),
            relative.y * std::max(0.01f, shockwaveAreaScale_.y),
            relative.z * std::max(0.01f, shockwaveAreaScale_.z)
        };
        const Vector3 scaledDirection{
            spawn.outwardDirection.x * std::max(0.01f, shockwaveAreaScale_.x),
            0.0f,
            spawn.outwardDirection.z * std::max(0.01f, shockwaveAreaScale_.z)
        };
        scaledSpawn.outwardDirection = Matrix4x4::Normalize(scaledDirection);
        auto rock = std::make_unique<ShockwaveRock>();
        rock->Initialize(
            app.ObjCom(), app.Dx(), camera_.get(), scaledSpawn, shockwaveSettings_.rock, random_);
        shockwaveRocks_.push_back(std::move(rock));
    }

    for (auto& rock : shockwaveRocks_) rock->Update(dt);
    std::erase_if(shockwaveRocks_, [](const std::unique_ptr<ShockwaveRock>& rock) {
        return !rock || !rock->IsAlive();
    });

    const float completedRadius = std::max(
        shockwaveSettings_.radiusStart, shockwaveSettings_.radiusMax);
    const bool waveFullyExpanded = shockwave_.GetRadius() >= completedRadius;
    if (waveWasActive && waveFullyExpanded) {
        std::uniform_real_distribution<float> delay(
            std::min(shockwaveSettings_.mineTrigger.delayMin, shockwaveSettings_.mineTrigger.delayMax),
            std::max(shockwaveSettings_.mineTrigger.delayMin, shockwaveSettings_.mineTrigger.delayMax));
        const Vector3 center = shockwave_.GetCenter();
        const float radius = shockwave_.GetRadius();
        const float radiusSquared = radius * radius;
        const float scaleX = std::max(0.01f, shockwaveAreaScale_.x);
        const float scaleZ = std::max(0.01f, shockwaveAreaScale_.z);
        for (auto& mine : mines_) {
            if (shockwaveAffectedMines_.contains(mine.get())) continue;
            const Vector3 delta = mine->GetPosition() - center;
            const float horizontalDistanceSquared =
                (delta.x / scaleX) * (delta.x / scaleX) +
                (delta.z / scaleZ) * (delta.z / scaleZ);
            if (horizontalDistanceSquared <= radiusSquared) {
                mine->TriggerExplosion(std::max(0.0f, delay(random_)));
                shockwaveAffectedMines_.insert(mine.get());
            }
        }
    }

    if (shockwaveVisual_) {
        const Vector3 previewCenter = bossPosition_ + shockwavePositionOffset_;
        shockwaveVisual_->SetTranslate(shockwave_.IsActive() ? shockwave_.GetCenter() : previewCenter);
        const float radius = shockwave_.IsActive()
            ? shockwave_.GetRadius()
            : std::max(0.0f, shockwaveSettings_.radiusMax);
        shockwaveVisual_->SetScale({
            radius * std::max(0.01f, shockwaveAreaScale_.x),
            radius * std::max(0.01f, shockwaveAreaScale_.y),
            radius * std::max(0.01f, shockwaveAreaScale_.z)
        });
        shockwaveVisual_->Update(dt);
    }
}

void BossTestScene::ApplyExplosionSettingsToMines_() {
    for (auto& mine : mines_) {
        mine->SetExplosionRadius(mineMotionSettings_.explosionRadius);
        mine->SetDamage(mineMotionSettings_.damage);
        mine->SetMoveSpeedDamage(mineMotionSettings_.moveSpeedDamage);
    }
}

bool BossTestScene::SaveMineSettings_() {
    try {
        const std::filesystem::path path(kMineSettingsPath);
        std::filesystem::create_directories(path.parent_path());
        nlohmann::json root;
        root["version"] = 1;
        root["mine"]["explosion"] = {
            { "damage", mineMotionSettings_.damage },
            { "moveSpeedDamage", mineMotionSettings_.moveSpeedDamage },
            { "explosionRadius", mineMotionSettings_.explosionRadius },
            { "normalFuseTime", normalMineFuseTime_ },
            { "chainReactionFuseTime", chainReactionFuseTime_ },
            { "triggerAllFuseJitter", triggerAllFuseJitter_ }
        };
        const auto& rock = shockwaveSettings_.rock;
        root["shockwave"] = {
            { "positionOffset", { shockwavePositionOffset_.x, shockwavePositionOffset_.y, shockwavePositionOffset_.z } },
            { "areaScale", { shockwaveAreaScale_.x, shockwaveAreaScale_.y, shockwaveAreaScale_.z } },
            { "radiusStart", shockwaveSettings_.radiusStart },
            { "radiusMax", shockwaveSettings_.radiusMax },
            { "expansionSpeed", shockwaveSettings_.expansionSpeed },
            { "duration", shockwaveSettings_.duration },
            { "rock", {
                { "spawnCount", rock.spawnCount }, { "spawnInterval", rock.spawnInterval },
                { "spawnRadiusMin", rock.spawnRadiusMin }, { "spawnRadiusMax", rock.spawnRadiusMax },
                { "spawnHeightOffset", rock.spawnHeightOffset },
                { "scaleMin", rock.scaleMin }, { "scaleMax", rock.scaleMax },
                { "launchPowerMin", rock.launchPowerMin }, { "launchPowerMax", rock.launchPowerMax },
                { "horizontalPower", rock.horizontalPower }, { "gravity", rock.gravity },
                { "drag", rock.drag }, { "lifetime", rock.lifetime },
                { "damage", rock.damage }, { "moveSpeedDamage", rock.moveSpeedDamage }
            } },
            { "mineTrigger", {
                { "delayMin", shockwaveSettings_.mineTrigger.delayMin },
                { "delayMax", shockwaveSettings_.mineTrigger.delayMax }
            } }
        };
        root["anchor"] = {
            { "centerOffset", { anchorCenterOffset_.x, anchorCenterOffset_.y, anchorCenterOffset_.z } },
            { "overallScale", anchorSettings_.overallScale },
            { "modelScale", { anchorSettings_.modelScale.x, anchorSettings_.modelScale.y, anchorSettings_.modelScale.z } },
            { "modelRotationOffset", { anchorSettings_.modelRotationOffset.x, anchorSettings_.modelRotationOffset.y, anchorSettings_.modelRotationOffset.z } },
            { "followOrbitRotation", anchorSettings_.followOrbitRotation },
            { "orbitRotationMultiplier", anchorSettings_.orbitRotationMultiplier },
            { "radius", anchorSettings_.radius },
            { "predictionLineWidth", anchorSettings_.predictionLineWidth },
            { "spawnLocalPosition", { anchorSettings_.spawnLocalPosition.x, anchorSettings_.spawnLocalPosition.y, anchorSettings_.spawnLocalPosition.z } },
            { "dropDuration", anchorSettings_.dropDuration },
            { "waitTime", anchorSettings_.waitTime },
            { "pullUpDuration", anchorSettings_.pullUpDuration },
            { "startAngularSpeed", anchorSettings_.startAngularSpeed },
            { "angularAcceleration", anchorSettings_.angularAcceleration },
            { "maxAngularSpeed", anchorSettings_.maxAngularSpeed },
            { "rotationDirection", anchorSettings_.rotationDirection },
            { "verticalAmplitude", anchorSettings_.verticalAmplitude },
            { "verticalFrequency", anchorSettings_.verticalFrequency },
            { "duration", anchorSettings_.duration },
            { "selfRotationSpeed", anchorSettings_.selfRotationSpeed },
            { "collisionRadius", anchorSettings_.collisionRadius },
            { "damage", anchorSettings_.damage },
            { "moveSpeedDamage", anchorSettings_.moveSpeedDamage },
            { "warningRing", {
                { "previewTime", anchorSettings_.warningRing.previewTime },
                { "thickness", anchorSettings_.warningRing.thickness },
                { "pulseSpeed", anchorSettings_.warningRing.pulseSpeed },
                { "pulseAmount", anchorSettings_.warningRing.pulseAmount }
            } },
            { "chain", {
                { "spacing", anchorSettings_.chain.spacing },
                { "scale", { anchorSettings_.chain.scale.x, anchorSettings_.chain.scale.y, anchorSettings_.chain.scale.z } },
                { "anchorLocalAttachPosition", { anchorSettings_.chain.anchorLocalAttachPosition.x, anchorSettings_.chain.anchorLocalAttachPosition.y, anchorSettings_.chain.anchorLocalAttachPosition.z } },
                { "endOffset", { anchorSettings_.chain.endOffset.x, anchorSettings_.chain.endOffset.y, anchorSettings_.chain.endOffset.z } },
                { "alternateRotationDegrees", anchorSettings_.chain.alternateRotationDegrees },
                { "maxLinks", anchorSettings_.chain.maxLinks }
            } }
        };
        std::ofstream output(path, std::ios::trunc);
        if (!output) throw std::runtime_error("could not open output file");
        output << root.dump(4) << '\n';
        mineSettingsStatus_ = "Saved: " + path.generic_string();
        return true;
    } catch (const std::exception& exception) {
        mineSettingsStatus_ = std::string("Save failed: ") + exception.what();
        return false;
    }
}

bool BossTestScene::LoadMineSettings_() {
    try {
        const std::filesystem::path path(kMineSettingsPath);
        std::ifstream input(path);
        if (!input) {
            mineSettingsStatus_ = "Using defaults (settings file not found).";
            return false;
        }
        const nlohmann::json root = nlohmann::json::parse(input);
        const auto explosion = root.value("mine", nlohmann::json::object())
            .value("explosion", nlohmann::json::object());
        mineMotionSettings_.damage = std::max(0.0f, explosion.value("damage", mineMotionSettings_.damage));
        mineMotionSettings_.moveSpeedDamage = std::max(
            0.0f, explosion.value("moveSpeedDamage", mineMotionSettings_.moveSpeedDamage));
        mineMotionSettings_.explosionRadius = std::max(
            0.1f, explosion.value("explosionRadius", mineMotionSettings_.explosionRadius));
        normalMineFuseTime_ = std::max(0.0f, explosion.value("normalFuseTime", normalMineFuseTime_));
        chainReactionFuseTime_ = std::max(
            0.0f, explosion.value("chainReactionFuseTime", chainReactionFuseTime_));
        triggerAllFuseJitter_ = std::max(
            0.0f, explosion.value("triggerAllFuseJitter", triggerAllFuseJitter_));
        const auto shockwave = root.value("shockwave", nlohmann::json::object());
        const auto positionOffset = shockwave.value("positionOffset", nlohmann::json::array());
        if (positionOffset.is_array() && positionOffset.size() >= 3) {
            shockwavePositionOffset_ = { positionOffset[0].get<float>(), positionOffset[1].get<float>(), positionOffset[2].get<float>() };
        }
        const auto areaScale = shockwave.value("areaScale", nlohmann::json::array());
        if (areaScale.is_array() && areaScale.size() >= 3) {
            shockwaveAreaScale_ = {
                std::max(0.01f, areaScale[0].get<float>()),
                std::max(0.01f, areaScale[1].get<float>()),
                std::max(0.01f, areaScale[2].get<float>())
            };
        }
        shockwaveSettings_.radiusStart = shockwave.value("radiusStart", shockwaveSettings_.radiusStart);
        shockwaveSettings_.radiusMax = shockwave.value("radiusMax", shockwaveSettings_.radiusMax);
        shockwaveSettings_.expansionSpeed = shockwave.value("expansionSpeed", shockwaveSettings_.expansionSpeed);
        shockwaveSettings_.duration = shockwave.value("duration", shockwaveSettings_.duration);
        const auto rock = shockwave.value("rock", nlohmann::json::object());
        auto& rockSettings = shockwaveSettings_.rock;
        rockSettings.spawnCount = rock.value("spawnCount", rockSettings.spawnCount);
        rockSettings.spawnInterval = rock.value("spawnInterval", rockSettings.spawnInterval);
        rockSettings.spawnRadiusMin = rock.value("spawnRadiusMin", rockSettings.spawnRadiusMin);
        rockSettings.spawnRadiusMax = rock.value("spawnRadiusMax", rockSettings.spawnRadiusMax);
        rockSettings.spawnHeightOffset = rock.value("spawnHeightOffset", rockSettings.spawnHeightOffset);
        rockSettings.scaleMin = rock.value("scaleMin", rockSettings.scaleMin);
        rockSettings.scaleMax = rock.value("scaleMax", rockSettings.scaleMax);
        rockSettings.launchPowerMin = rock.value("launchPowerMin", rockSettings.launchPowerMin);
        rockSettings.launchPowerMax = rock.value("launchPowerMax", rockSettings.launchPowerMax);
        rockSettings.horizontalPower = rock.value("horizontalPower", rockSettings.horizontalPower);
        rockSettings.gravity = rock.value("gravity", rockSettings.gravity);
        rockSettings.drag = rock.value("drag", rockSettings.drag);
        rockSettings.lifetime = rock.value("lifetime", rockSettings.lifetime);
        rockSettings.damage = rock.value("damage", rockSettings.damage);
        rockSettings.moveSpeedDamage = rock.value("moveSpeedDamage", rockSettings.moveSpeedDamage);
        const auto mineTrigger = shockwave.value("mineTrigger", nlohmann::json::object());
        shockwaveSettings_.mineTrigger.delayMin = mineTrigger.value(
            "delayMin", shockwaveSettings_.mineTrigger.delayMin);
        shockwaveSettings_.mineTrigger.delayMax = mineTrigger.value(
            "delayMax", shockwaveSettings_.mineTrigger.delayMax);
        const auto anchor = root.value("anchor", nlohmann::json::object());
        const auto anchorCenterOffset = anchor.value("centerOffset", nlohmann::json::array());
        if (anchorCenterOffset.is_array() && anchorCenterOffset.size() >= 3) {
            anchorCenterOffset_ = {
                anchorCenterOffset[0].get<float>(),
                anchorCenterOffset[1].get<float>(),
                anchorCenterOffset[2].get<float>()
            };
        }
        const auto anchorModelScale = anchor.value("modelScale", nlohmann::json::array());
        anchorSettings_.overallScale = std::max(0.01f, anchor.value("overallScale", anchorSettings_.overallScale));
        if (anchorModelScale.is_array() && anchorModelScale.size() >= 3) {
            anchorSettings_.modelScale = {
                std::max(0.01f, anchorModelScale[0].get<float>()),
                std::max(0.01f, anchorModelScale[1].get<float>()),
                std::max(0.01f, anchorModelScale[2].get<float>())
            };
        }
        anchorSettings_.radius = anchor.value("radius", anchorSettings_.radius);
        anchorSettings_.predictionLineWidth = std::max(
            0.0f, anchor.value("predictionLineWidth", anchorSettings_.predictionLineWidth));
        const auto anchorSpawnLocalPosition = anchor.value("spawnLocalPosition", nlohmann::json::array());
        if (anchorSpawnLocalPosition.is_array() && anchorSpawnLocalPosition.size() >= 3) {
            anchorSettings_.spawnLocalPosition = {
                anchorSpawnLocalPosition[0].get<float>(),
                anchorSpawnLocalPosition[1].get<float>(),
                anchorSpawnLocalPosition[2].get<float>()
            };
        }
        anchorSettings_.dropDuration = std::max(0.0f, anchor.value("dropDuration", anchorSettings_.dropDuration));
        anchorSettings_.waitTime = std::max(0.0f, anchor.value("waitTime", anchorSettings_.waitTime));
        anchorSettings_.pullUpDuration = std::max(0.0f, anchor.value("pullUpDuration", anchorSettings_.pullUpDuration));
        anchorSettings_.startAngularSpeed = anchor.value("startAngularSpeed", anchorSettings_.startAngularSpeed);
        anchorSettings_.angularAcceleration = anchor.value("angularAcceleration", anchorSettings_.angularAcceleration);
        anchorSettings_.maxAngularSpeed = anchor.value("maxAngularSpeed", anchorSettings_.maxAngularSpeed);
        anchorSettings_.rotationDirection = anchor.value("rotationDirection", anchorSettings_.rotationDirection);
        anchorSettings_.verticalAmplitude = anchor.value("verticalAmplitude", anchorSettings_.verticalAmplitude);
        anchorSettings_.verticalFrequency = anchor.value("verticalFrequency", anchorSettings_.verticalFrequency);
        anchorSettings_.duration = anchor.value("duration", anchorSettings_.duration);
        anchorSettings_.selfRotationSpeed = anchor.value("selfRotationSpeed", anchorSettings_.selfRotationSpeed);
        anchorSettings_.collisionRadius = anchor.value("collisionRadius", anchorSettings_.collisionRadius);
        anchorSettings_.damage = anchor.value("damage", anchorSettings_.damage);
        anchorSettings_.moveSpeedDamage = anchor.value("moveSpeedDamage", anchorSettings_.moveSpeedDamage);
        const auto warningRing = anchor.value("warningRing", nlohmann::json::object());
        anchorSettings_.warningRing.previewTime = warningRing.value(
            "previewTime", anchorSettings_.warningRing.previewTime);
        anchorSettings_.warningRing.thickness = warningRing.value(
            "thickness", anchorSettings_.warningRing.thickness);
        anchorSettings_.warningRing.pulseSpeed = warningRing.value(
            "pulseSpeed", anchorSettings_.warningRing.pulseSpeed);
        anchorSettings_.warningRing.pulseAmount = warningRing.value(
            "pulseAmount", anchorSettings_.warningRing.pulseAmount);
        const auto chain = anchor.value("chain", nlohmann::json::object());
        anchorSettings_.chain.spacing = std::max(0.05f, chain.value("spacing", anchorSettings_.chain.spacing));
        const auto chainScale = chain.value("scale", nlohmann::json::array());
        if (chainScale.is_array() && chainScale.size() >= 3) {
            anchorSettings_.chain.scale = {
                std::max(0.01f, chainScale[0].get<float>()),
                std::max(0.01f, chainScale[1].get<float>()),
                std::max(0.01f, chainScale[2].get<float>())
            };
        }
        const auto chainAttachPosition = chain.value("anchorLocalAttachPosition", nlohmann::json::array());
        if (chainAttachPosition.is_array() && chainAttachPosition.size() >= 3) {
            anchorSettings_.chain.anchorLocalAttachPosition = {
                chainAttachPosition[0].get<float>(),
                chainAttachPosition[1].get<float>(),
                chainAttachPosition[2].get<float>()
            };
        }
        const auto anchorModelRotationOffset = anchor.value("modelRotationOffset", nlohmann::json::array());
        if (anchorModelRotationOffset.is_array() && anchorModelRotationOffset.size() >= 3) {
            anchorSettings_.modelRotationOffset = {
                anchorModelRotationOffset[0].get<float>(),
                anchorModelRotationOffset[1].get<float>(),
                anchorModelRotationOffset[2].get<float>()
            };
        }
        anchorSettings_.followOrbitRotation = anchor.value(
            "followOrbitRotation", anchorSettings_.followOrbitRotation);
        anchorSettings_.orbitRotationMultiplier = anchor.value(
            "orbitRotationMultiplier", anchorSettings_.orbitRotationMultiplier);
        const auto chainEndOffset = chain.value("endOffset", nlohmann::json::array());
        if (chainEndOffset.is_array() && chainEndOffset.size() >= 3) {
            anchorSettings_.chain.endOffset = {
                chainEndOffset[0].get<float>(),
                chainEndOffset[1].get<float>(),
                chainEndOffset[2].get<float>()
            };
        }
        anchorSettings_.chain.alternateRotationDegrees = chain.value(
            "alternateRotationDegrees", anchorSettings_.chain.alternateRotationDegrees);
        anchorSettings_.chain.maxLinks = std::clamp(
            chain.value("maxLinks", anchorSettings_.chain.maxLinks), 0, kAnchorChainHardLimit);
        ApplyExplosionSettingsToMines_();
        mineSettingsStatus_ = "Loaded: " + path.generic_string();
        return true;
    } catch (const std::exception& exception) {
        mineSettingsStatus_ = std::string("Load failed: ") + exception.what();
        return false;
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
        shockwaveAffectedMines_.clear();
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
    if (pendingTriggerShockwave_) {
        TriggerShockwave_();
        pendingTriggerShockwave_ = false;
    }
    if (pendingResetShockwave_) {
        ResetShockwave_();
        pendingResetShockwave_ = false;
    }
    if (pendingTriggerAnchor_) {
        TriggerAnchor_();
        pendingTriggerAnchor_ = false;
    }
    if (pendingResetAnchor_) {
        ResetAnchor_();
        pendingResetAnchor_ = false;
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
    UpdateShockwave_(app, dt);
    UpdateAnchor_(app, dt);
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
    if (showShockwaveRange_ && shockwaveVisual_) shockwaveVisual_->Draw();
    for (const auto& rock : shockwaveRocks_) rock->Draw();
    if (anchorAttack_.IsWarningVisible() && anchorWarningRing_) anchorWarningRing_->Draw();
    if (anchorAttack_.IsAnchorVisible() && anchorObject_) anchorObject_->Draw();
    for (size_t i = 0; i < anchorChainVisibleCount_; ++i) anchorChainLinks_[i]->Draw();
    if (showAnchorOrbitRange_) {
        for (const auto& ring : anchorOrbitDebug_) ring->Draw();
    }
    if (showAnchorPosition_ && anchorAttack_.GetState() == AnchorAttack::State::Inactive && anchorPositionDebug_) {
        anchorPositionDebug_->Draw();
    }
    if (showAnchorCollision_ && anchorAttack_.IsAnchorVisible()) {
        for (const auto& box : anchorCollisionDebug_) box->Draw();
    }
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
        ImGui::TextDisabled("W/S forward/back / A/D left/right / E up / Q down");
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
        if (ImGui::TreeNodeEx("Mine", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        bool explosionSettingsChanged = false;
        explosionSettingsChanged |= ImGui::DragFloat(
            "Damage", &mineMotionSettings_.damage, 0.5f, 0.0f, 10000.0f);
        explosionSettingsChanged |= ImGui::DragFloat(
            "Move Speed Damage", &mineMotionSettings_.moveSpeedDamage, 0.1f, 0.0f, 10000.0f);
        explosionSettingsChanged |= ImGui::DragFloat(
            "Explosion Radius", &mineMotionSettings_.explosionRadius, 0.1f, 0.1f, 100.0f);
        if (explosionSettingsChanged) {
            ApplyExplosionSettingsToMines_();
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
        ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Shockwave", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Shockwave Range", &showShockwaveRange_);
            ImGui::DragFloat3("Position Offset##Shockwave", &shockwavePositionOffset_.x, 0.1f, -1000.0f, 1000.0f);
            ImGui::DragFloat3("Area Scale##Shockwave", &shockwaveAreaScale_.x, 0.05f, 0.01f, 100.0f);
            ImGui::DragFloat("Radius Start##Shockwave", &shockwaveSettings_.radiusStart, 0.1f, 0.0f, 500.0f);
            ImGui::DragFloat("Radius Max##Shockwave", &shockwaveSettings_.radiusMax, 0.25f, 0.0f, 1000.0f);
            ImGui::DragFloat("Expansion Speed##Shockwave", &shockwaveSettings_.expansionSpeed, 0.25f, 0.0f, 500.0f);
            ImGui::DragFloat("Duration##Shockwave", &shockwaveSettings_.duration, 0.05f, 0.0f, 60.0f);

            if (ImGui::TreeNodeEx("Rock", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& rock = shockwaveSettings_.rock;
                ImGui::DragInt("Spawn Count##Rock", &rock.spawnCount, 1.0f, 0, 512);
                ImGui::DragFloat("Spawn Interval##Rock", &rock.spawnInterval, 0.01f, 0.0f, 5.0f);
                ImGui::DragFloat("Spawn Radius Min##Rock", &rock.spawnRadiusMin, 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Spawn Radius Max##Rock", &rock.spawnRadiusMax, 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Spawn Height Offset##Rock", &rock.spawnHeightOffset, 0.05f, -100.0f, 100.0f);
                ImGui::DragFloat("Scale Min##Rock", &rock.scaleMin, 0.05f, 0.01f, 100.0f);
                ImGui::DragFloat("Scale Max##Rock", &rock.scaleMax, 0.05f, 0.01f, 100.0f);
                ImGui::DragFloat("Launch Power Min##Rock", &rock.launchPowerMin, 0.1f, 0.0f, 500.0f);
                ImGui::DragFloat("Launch Power Max##Rock", &rock.launchPowerMax, 0.1f, 0.0f, 500.0f);
                ImGui::DragFloat("Horizontal Power##Rock", &rock.horizontalPower, 0.1f, 0.0f, 500.0f);
                ImGui::DragFloat("Gravity##Rock", &rock.gravity, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Drag##Rock", &rock.drag, 0.01f, 0.0f, 20.0f);
                ImGui::DragFloat("Lifetime##Rock", &rock.lifetime, 0.1f, 0.0f, 60.0f);
                ImGui::DragFloat("Damage##Rock", &rock.damage, 0.5f, 0.0f, 10000.0f);
                ImGui::DragFloat("Move Speed Damage##Rock", &rock.moveSpeedDamage, 0.1f, 0.0f, 10000.0f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Mine Trigger", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Trigger Delay Min", &shockwaveSettings_.mineTrigger.delayMin, 0.01f, 0.0f, 10.0f);
                ImGui::DragFloat("Trigger Delay Max", &shockwaveSettings_.mineTrigger.delayMax, 0.01f, 0.0f, 10.0f);
                ImGui::TreePop();
            }
            if (ImGui::Button("Trigger Shockwave")) pendingTriggerShockwave_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Reset Shockwave")) pendingResetShockwave_ = true;
            ImGui::Text("Radius: %.2f / Rocks: %d / Active: %s",
                shockwave_.GetRadius(), static_cast<int>(shockwaveRocks_.size()),
                shockwave_.IsActive() ? "Yes" : "No");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Anchor", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("Entrance / Exit##Anchor", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Spawn Local Position##Anchor", &anchorSettings_.spawnLocalPosition.x, 0.1f, -1000.0f, 1000.0f);
                ImGui::DragFloat("Drop Duration##Anchor", &anchorSettings_.dropDuration, 0.02f, 0.0f, 20.0f);
                ImGui::DragFloat("Wait Time##Anchor", &anchorSettings_.waitTime, 0.02f, 0.0f, 20.0f);
                ImGui::DragFloat("Pull Up Duration##Anchor", &anchorSettings_.pullUpDuration, 0.02f, 0.0f, 20.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Movement##Anchor", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Center Offset##Anchor", &anchorCenterOffset_.x, 0.1f, -1000.0f, 1000.0f);
                ImGui::DragFloat("Radius##Anchor", &anchorSettings_.radius, 0.1f, 0.0f, 500.0f);
                ImGui::DragFloat("Prediction Line Width##Anchor", &anchorSettings_.predictionLineWidth, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Anchor Overall Scale##Anchor", &anchorSettings_.overallScale, 0.01f, 0.01f, 20.0f);
                ImGui::DragFloat3("Model Scale##Anchor", &anchorSettings_.modelScale.x, 0.05f, 0.01f, 100.0f);
                ImGui::DragFloat3("Model Rotation Offset##Anchor", &anchorSettings_.modelRotationOffset.x, 0.01f, -6.283f, 6.283f);
                ImGui::Checkbox("Follow Orbit Rotation##Anchor", &anchorSettings_.followOrbitRotation);
                ImGui::DragFloat("Orbit Rotation Multiplier##Anchor", &anchorSettings_.orbitRotationMultiplier, 0.01f, -4.0f, 4.0f);
                ImGui::TextDisabled("1.0 faces the tangent; model forward correction belongs in Rotation Offset Y.");
                ImGui::DragFloat("Start Angular Speed", &anchorSettings_.startAngularSpeed, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Angular Acceleration", &anchorSettings_.angularAcceleration, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Max Angular Speed", &anchorSettings_.maxAngularSpeed, 0.05f, 0.0f, 100.0f);
                ImGui::DragInt("Rotation Direction", &anchorSettings_.rotationDirection, 1.0f, -1, 1);
                if (anchorSettings_.rotationDirection == 0) anchorSettings_.rotationDirection = 1;
                ImGui::DragFloat("Vertical Amplitude", &anchorSettings_.verticalAmplitude, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Vertical Frequency", &anchorSettings_.verticalFrequency, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Duration##Anchor", &anchorSettings_.duration, 0.05f, 0.0f, 60.0f);
                ImGui::DragFloat("Optional Self Rotation Speed", &anchorSettings_.selfRotationSpeed, 0.05f, -100.0f, 100.0f);
                ImGui::Checkbox("Show Orbit Range", &showAnchorOrbitRange_);
                ImGui::Checkbox("Show Anchor Preview", &showAnchorPosition_);
                ImGui::TextDisabled("Blue: orbit range / inactive Anchor uses its normal material");
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Warning Ring", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Preview Time", &anchorSettings_.warningRing.previewTime, 0.02f, 0.0f, 20.0f);
                ImGui::DragFloat("Thickness", &anchorSettings_.warningRing.thickness, 0.02f, 0.01f, 50.0f);
                ImGui::DragFloat("Pulse Speed", &anchorSettings_.warningRing.pulseSpeed, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Pulse Amount", &anchorSettings_.warningRing.pulseAmount, 0.01f, 0.0f, 1.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Chain##Anchor", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Show Chain Preview", &showAnchorChainPreview_);
                ImGui::TextDisabled("Preview: Spawn Point to orbit start while Inactive.");
                ImGui::DragFloat("Chain Spacing", &anchorSettings_.chain.spacing, 0.02f, 0.05f, 20.0f);
                ImGui::DragFloat3("Chain Link Scale (Width/Thickness/Length)", &anchorSettings_.chain.scale.x, 0.02f, 0.01f, 20.0f);
                ImGui::TextDisabled("X: width / Y: thickness / Z: length");
                ImGui::DragFloat3("Anchor Local Attach Position", &anchorSettings_.chain.anchorLocalAttachPosition.x, 0.05f, -20.0f, 20.0f);
                ImGui::TextDisabled("Local point inside the top eye of anchor.obj.");
                ImGui::DragFloat3("Chain End Offset", &anchorSettings_.chain.endOffset.x, 0.05f, -100.0f, 100.0f);
                ImGui::TextDisabled("Offsets the chain endpoint from the Anchor position.");
                ImGui::DragFloat("Alternate Rotation", &anchorSettings_.chain.alternateRotationDegrees, 1.0f, 0.0f, 180.0f);
                ImGui::DragInt("Max Links", &anchorSettings_.chain.maxLinks, 1.0f, 0, kAnchorChainHardLimit);
                ImGui::Text("Visible Links: %d", static_cast<int>(anchorChainVisibleCount_));
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Damage / Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Collision Radius", &anchorSettings_.collisionRadius, 0.05f, 0.01f, 100.0f);
                ImGui::DragFloat("Damage##Anchor", &anchorSettings_.damage, 0.5f, 0.0f, 10000.0f);
                ImGui::DragFloat("Move Speed Damage##Anchor", &anchorSettings_.moveSpeedDamage, 0.1f, 0.0f, 10000.0f);
                ImGui::Checkbox("Show Anchor Collision Boxes", &showAnchorCollision_);
                ImGui::TextDisabled("Green boxes: shaft, arms, and hook tips.");
                ImGui::TreePop();
            }
            if (ImGui::Button("Trigger Anchor")) pendingTriggerAnchor_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Reset Anchor")) pendingResetAnchor_ = true;
            const Vector3 anchorPosition = anchorAttack_.GetPosition();
            ImGui::Text("State: %s / State Time: %.2f", AnchorAttack::StateName(anchorAttack_.GetState()), anchorAttack_.GetStateTime());
            ImGui::Text("Speed: %.2f / Tangent Yaw: %.2f / Position: %.2f, %.2f, %.2f",
                anchorAttack_.GetCurrentAngularSpeed(),
                anchorAttack_.GetOrbitTangentYaw(),
                anchorPosition.x, anchorPosition.y, anchorPosition.z);
            ImGui::TreePop();
        }
        if (ImGui::Button("Save Boss Attack Settings")) SaveMineSettings_();
        ImGui::SameLine();
        if (ImGui::Button("Load Boss Attack Settings")) LoadMineSettings_();
        if (!mineSettingsStatus_.empty()) ImGui::TextDisabled("%s", mineSettingsStatus_.c_str());
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
