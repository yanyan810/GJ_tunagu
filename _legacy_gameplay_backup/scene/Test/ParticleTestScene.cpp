#include "ParticleTestScene.h"
#include "ParticleTestSceneSupport.h"
#include "TestSceneBattleLayout.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"
#include "Effect/EffectManager.h"

#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gParticleTestEditorModeSwitcherVisible;
extern int gParticleTestEditorMode;
extern std::vector<std::string> gParticleTestBlenderHierarchyNames;
extern int gParticleTestBlenderHierarchySelected;
extern bool gParticleTestBlenderHierarchySelectionChanged;
extern bool gParticleTestAnimationCameraPreviewVisible;
extern bool gParticleTestAnimationCameraPreviewSwapped;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;
using namespace ParticleTestSceneSupport;

namespace {
struct VertexPositionKey {
    long long x = 0;
    long long y = 0;
    long long z = 0;

    bool operator==(const VertexPositionKey& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct VertexPositionKeyHash {
    size_t operator()(const VertexPositionKey& key) const
    {
        size_t seed = std::hash<long long>{}(key.x);
        seed ^= std::hash<long long>{}(key.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= std::hash<long long>{}(key.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

VertexPositionKey MakeVertexPositionKey(const Vector3& pos)
{
    constexpr float kScale = 10000.0f;
    return {
        static_cast<long long>(std::llround(pos.x * kScale)),
        static_cast<long long>(std::llround(pos.y * kScale)),
        static_cast<long long>(std::llround(pos.z * kScale)),
    };
}
}

void ParticleTestScene::OnEnter(GameApp& app)
{
	playerSpecialPreviewOrigin_ = { 0.0f, 0.0f, 0.0f };
	playerSpecialPreviewBaseRotation_ = { 0.0f, 0.0f, 0.0f };
	playerSpecialPreviewOriginInitialized_ = false;
    if (auto* input = app.GetInput()) {
        input->SetCameraControlEnabled(false);
    }

    app.Render()->SetMode(PostEffectMode::FullScreen);

    TextureManager::GetInstance()->LoadTexture("resources/circle.png");
    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

    editorCameraPosition_ = { 0.0f, 3.0f, -20.0f };
    editorCameraRotation_ = { 0.0f, 0.0f, 0.0f };
    editorCameraControlActive_ = false;
    editorCameraPanActive_ = false;
    shiftMovementActive_ = false;

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate(editorCameraPosition_);
    camera_->SetRotate(editorCameraRotation_);
    camera_->Update();

    animationCamera_ = std::make_unique<Camera>();
    ApplyAnimationCamera_();
    gamePreviewCamera_ = std::make_unique<Camera>();
    gamePreviewCamera_->SetTranslate({ 0.0f, 20.0f, -50.0f });
    gamePreviewCamera_->SetRotate({ 0.35f, 0.0f, 0.0f });
    gamePreviewCamera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());
	EffectManager::GetInstance()->Initialize();
	EffectManager::GetInstance()->SetGraphicsResources(app.ObjCom(), app.Dx(), camera_.get());

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/ground.obj");
    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetEnableLighting(0);

    editorParticle_ = std::make_unique<Particle>();
    editorParticle_->Initialize(app.ParticleCom(), app.Dx(), app.Srv());
    editorParticle_->SetModel("plane.obj");
    editorParticle_->SetCamera(camera_.get());
    editorParticle_->SetBlendMode(ParticleCommon::BlendMode::kBlendModeAdd);
    editorParticle_->SetMaterialColor({ 1, 1, 1, 1 });

    playerAttackHitboxCube_ = std::make_unique<Object3d>();
    playerAttackHitboxCube_->Initialize(app.ObjCom(), app.Dx());
    playerAttackHitboxCube_->SetCamera(GetSceneCamera_());
    playerAttackHitboxCube_->SetModel("cube/cube.obj");
    playerAttackHitboxCube_->SetEnableLighting(0);
    playerAttackHitboxCube_->SetMaterialColor({ 0.1f, 1.0f, 0.25f, 0.35f });
    playerAttackHitboxCube_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    bossDummy_ = std::make_unique<Object3d>();
    bossDummy_->Initialize(app.ObjCom(), app.Dx());
    bossDummy_->SetCamera(GetSceneCamera_());
    bossDummy_->SetModel("enemy/boss/boss.gltf");
    bossDummy_->SetEnableLighting(0);
    bossDummy_->SetScale({ 2.0f, 2.0f, 2.0f });
    bossDummy_->SetMaterialColor({ 0.35f, 0.65f, 1.0f, 0.42f });
    bossDummy_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    bossDummyHitboxCube_ = std::make_unique<Object3d>();
    bossDummyHitboxCube_->Initialize(app.ObjCom(), app.Dx());
    bossDummyHitboxCube_->SetCamera(GetSceneCamera_());
    bossDummyHitboxCube_->SetModel("cube/cube.obj");
    bossDummyHitboxCube_->SetEnableLighting(0);
    bossDummyHitboxCube_->SetMaterialColor({ 0.15f, 0.55f, 1.0f, 0.25f });
    bossDummyHitboxCube_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    AddEditorObject_(app, editorModelPath_);

#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = true;
    gParticleTestEditorMode = static_cast<int>(editorMode_);
#endif
    lastTimelineTime_ = (timelineTime_ == 0.0f) ? -0.001f : timelineTime_;
    previousTimelineTime_ = timelineTime_;

    auto* pm = ParticleManager::GetInstance();
    if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
        pm->ClearGroups();
        for (auto& node : particleNodes_) {
			if (!node.isEffectNode) pm->LoadAdditional(node.particleFileName, "");
            node.hasEmitted = false;
        }
    } else {
        ReloadParticleJson_();
    }
    lastEditorMode_ = editorMode_;
}

void ParticleTestScene::OnExit(GameApp&)
{
	EffectManager::GetInstance()->Finalize();
    editorObjects_.clear();
    bossDummyHitboxCube_.reset();
    bossDummy_.reset();
    playerAttackHitboxCube_.reset();
    editorParticle_.reset();
    ground_.reset();
    animationCamera_.reset();
    gamePreviewCamera_.reset();
    camera_.reset();
#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = false;
    gParticleTestBlenderHierarchyNames.clear();
    gParticleTestBlenderHierarchySelected = -1;
    gParticleTestBlenderHierarchySelectionChanged = false;
    gParticleTestAnimationCameraPreviewVisible = false;
    gParticleTestAnimationCameraPreviewSwapped = false;
#endif
}

void ParticleTestScene::Update(GameApp& app, float dt)
{
    UpdateCameraControls_(app, dt);

    const Input* input = app.GetInput();
    if (!input) {
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        RequestChangeScene_("Title");
        return;
    }

    if (pendingDeleteSelectedObject_) {
        pendingDeleteSelectedObject_ = false;
        DeleteSelectedObject_();
    }

    reloadCooldown_ = std::max(0.0f, reloadCooldown_ - dt);
    if (editorMode_ == EditorMode::Particle && input->IsKeyTrigger(DIK_R) && reloadCooldown_ <= 0.0f) {
        ReloadParticleJson_();
        reloadCooldown_ = 0.2f;
    }

    if (editorMode_ == EditorMode::Particle && input->IsKeyTrigger(DIK_SPACE)) {
        SpawnHitEffectPreview_();
    }

    // モード切り替え時のクリアと再ロード
    if (editorMode_ != lastEditorMode_) {
        auto* pm = ParticleManager::GetInstance();
        pm->ClearAllParticles();
        if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
            pm->ClearGroups();
            for (auto& node : particleNodes_) {
				if (!node.isEffectNode) pm->LoadAdditional(node.particleFileName, "");
                node.hasEmitted = false;
            }
        } else {
            ReloadParticleJson_();
        }
        lastEditorMode_ = editorMode_;
    }

    float particleDt = 0.0f;
    bool timeJumped = false;

    if (pendingTimelineRebuild_) {
        pendingTimelineRebuild_ = false;
        RebuildParticleTimeline_(pendingTimelineRebuildTime_);
        particleDt = 0.0f;
        timeJumped = true;
    }

    if (!timeJumped && timelinePlaying_) {
        float nextTime = timelineTime_ + dt;
        if (nextTime > timelineDuration_) {
            if (timelineLoop_ && timelineDuration_ > 0.0f) {
                timelineTime_ = std::fmod(nextTime, timelineDuration_);
                RebuildParticleTimeline_(timelineTime_);
                timeJumped = true;
            } else {
                timelineTime_ = timelineDuration_;
                timelinePlaying_ = false;
            }
        } else {
            timelineTime_ = nextTime;
        }
        if (timeJumped) {
            particleDt = 0.0f;
        } else {
            EvaluateTimeline_(true);
            particleDt = dt;
        }
    } else if (!timeJumped) {
        if (timelineTime_ != previousTimelineTime_) {
            RebuildParticleTimeline_(timelineTime_);
            particleDt = 0.0f;
            timeJumped = true;
        }
    }

    if (!timeJumped) {
        lastTimelineTime_ = (timelineTime_ == 0.0f) ? -0.001f : timelineTime_;
    }
    KeepTimelineTimeInView_();
    previousTimelineTime_ = timelineTime_;

	if (playerAttackEditorEnabled_ && playerAttackObjectIndex_ >= 0 &&
		playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
		editorObjects_[playerAttackObjectIndex_].visible =
			editorMode_ != EditorMode::PlayerAttack || timelinePlaying_;
	}

    if (camera_) {
        camera_->Update();
    }
    ApplyAnimationCamera_();
    ApplyCameraToEditorObjects_();
    if (editorMode_ == EditorMode::Particle) {
        particleDt = dt;
    }
    if (GetSceneCamera_()) {
        ParticleManager::GetInstance()->Update(particleDt, *GetSceneCamera_());
    }

	if (editorMode_ == EditorMode::PlayerAttack && matchTestSceneLayout_) {
		const Vector3 expectedPlayer = TestSceneBattleLayout::kPlayerSpawn;
		const Vector3 expectedBoss = TestSceneBattleLayout::kBossSpawn;
		const bool layoutChanged =
			LengthVector3(playerSpecialPreviewOrigin_ - expectedPlayer) > 0.0001f ||
			LengthVector3(bossDummyPosition_ - expectedBoss) > 0.0001f ||
			LengthVector3(bossDummyHalfSize_ - TestSceneBattleLayout::kBossBodyHalfSize) > 0.0001f;
		if (layoutChanged) {
			playerSpecialPreviewOrigin_ = expectedPlayer;
			bossDummyPosition_ = expectedBoss;
			bossDummyHalfSize_ = TestSceneBattleLayout::kBossBodyHalfSize;
			playerSpecialPreviewOriginInitialized_ = true;
			ApplyPlayerSpecialPreviewPosition_();
			SyncPlayerSpecialPreviewNodes_();
			RequestTimelineRebuild_(timelineTime_);
		}
	}
	if (playerAttackEditorEnabled_) {
		const Vector3 playerPosition = playerSpecialPreviewOrigin_ + previewSpecialPositionOffset_ +
			Vector3{ 0.0f, 0.0f, previewSpecialVisualZOffset_ };
		auto* effectManager = EffectManager::GetInstance();
		for (auto& node : particleNodes_) {
			if (!node.isEffectNode || !node.followOwnerMovement) continue;
			node.position = playerPosition + node.ownerOffset;
			effectManager->SetActiveEffectWorldPosition(node.name, node.position);
		}
	}
	if (gamePreviewCamera_) {
		const Vector3 physicalPlayer = playerSpecialPreviewOrigin_ + previewSpecialPositionOffset_;
		const Vector3 target{
			(physicalPlayer.x + bossDummyPosition_.x) * 0.5f,
			(physicalPlayer.y + bossDummyPosition_.y) * 0.5f,
			(physicalPlayer.z + bossDummyPosition_.z) * 0.5f
		};
		const float dx = physicalPlayer.x - bossDummyPosition_.x;
		const float dz = physicalPlayer.z - bossDummyPosition_.z;
		const float battleDistance = std::sqrt(dx * dx + dz * dz);
		const float cameraDistance = std::clamp(42.0f + battleDistance * 1.15f, 42.0f, 70.0f);
		gamePreviewCamera_->SetTranslate({ target.x, 20.0f, target.z - cameraDistance });
		gamePreviewCamera_->SetRotate({ 0.35f, 0.0f, 0.0f });
		gamePreviewCamera_->Update();
	}
	EffectManager::GetInstance()->Update(timelinePlaying_ ? dt : 0.0f);

    if (ground_) {
        ground_->Update(dt);
    }

    for (auto& item : editorObjects_) {
        if (item.object) {
            if (!item.attachToBone) {
                ApplyEditorObjectTransform_(item);
            }
            item.object->Update(dt);
        }
    }

    for (auto& item : editorObjects_) {
        if (!item.object || !item.attachToBone || item.attachParentId == item.id || item.attachJointName.empty()) {
            continue;
        }

        Object3d* parentObject = nullptr;
        for (auto& parent : editorObjects_) {
            if (parent.id == item.attachParentId && parent.object) {
                parentObject = parent.object.get();
                break;
            }
        }

        if (parentObject && parentObject->AttachObjectToJoint(*item.object, item.attachJointName, item.attachOffset, item.attachRotation, item.attachScale)) {
            item.object->SetCamera(GetSceneCamera_());
            item.object->SetMaterialColor(item.color);
            item.object->SetBlendMode(item.blendMode);
            if (!item.texturePath.empty()) {
                item.object->SetTexture(item.texturePath);
            } else {
                item.object->ClearTextureOverride();
            }
            item.object->Update(dt);
        } else {
            ApplyEditorObjectTransform_(item);
            item.object->Update(dt);
        }
    }

    if (playerAttackHitboxCube_) {
        Vector3 playerBase{};
        if (previewPlayerAttackHitbox_.followPlayerMovement &&
            playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
            playerBase = editorObjects_[playerAttackObjectIndex_].position;
            // Visual Z belongs only to the model. Keep the hitbox on the physical lane.
            playerBase.z -= previewSpecialVisualZOffset_;
        } else if (!previewPlayerAttackHitbox_.followPlayerMovement) {
            playerBase = playerSpecialPreviewOrigin_;
        }
        playerAttackHitboxCube_->SetTranslate(playerBase + previewPlayerAttackHitbox_.offset);
        playerAttackHitboxCube_->SetScale(previewPlayerAttackHitbox_.halfSize);
        playerAttackHitboxCube_->Update(dt);
    }

    if (bossDummy_) {
        bossDummy_->SetTranslate(bossDummyPosition_);
        bossDummy_->SetScale({ 2.0f, 2.0f, 2.0f });
        bossDummy_->Update(dt);
    }
    if (bossDummyHitboxCube_) {
        bossDummyHitboxCube_->SetTranslate({
            bossDummyPosition_.x,
            bossDummyPosition_.y + bossDummyHalfSize_.y,
            bossDummyPosition_.z
        });
        bossDummyHitboxCube_->SetScale(bossDummyHalfSize_);
        bossDummyHitboxCube_->Update(dt);
    }

    if (editorMode_ == EditorMode::Particle && editorParticle_) {
        editorParticle_->Update();
    }
}

void ParticleTestScene::DrawRender(GameApp& app)
{
    DrawSceneContent_(app);
}

void ParticleTestScene::DrawPreview(GameApp& app)
{
    if (!useAnimationCameraPreview_) {
        return;
    }

    Camera* previewCamera = GetPreviewCamera_();
    Camera* sceneCamera = GetSceneCamera_();
    if (!previewCamera) {
        return;
    }

    if (ground_) {
        ground_->SetCamera(previewCamera);
    }
    if (editorParticle_) {
        editorParticle_->SetCamera(previewCamera);
    }
    for (auto& item : editorObjects_) {
        if (item.object) {
            item.object->SetCamera(previewCamera);
        }
    }
    if (playerAttackHitboxCube_) {
        playerAttackHitboxCube_->SetCamera(previewCamera);
    }
    if (bossDummy_) bossDummy_->SetCamera(previewCamera);
    if (bossDummyHitboxCube_) bossDummyHitboxCube_->SetCamera(previewCamera);

    DrawSceneContent_(app);

    if (ground_) {
        ground_->SetCamera(sceneCamera);
    }
    if (editorParticle_) {
        editorParticle_->SetCamera(sceneCamera);
    }
    for (auto& item : editorObjects_) {
        if (item.object) {
            item.object->SetCamera(sceneCamera);
        }
    }
    if (playerAttackHitboxCube_) {
        playerAttackHitboxCube_->SetCamera(sceneCamera);
    }
    if (bossDummy_) bossDummy_->SetCamera(sceneCamera);
    if (bossDummyHitboxCube_) bossDummyHitboxCube_->SetCamera(sceneCamera);
}

void ParticleTestScene::DrawPostEffectTargets(GameApp& app)
{
    Vector4 layerBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 layerOutlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        const auto& selected = editorObjects_[selectedEditorObject_];
        if (selected.bloomPostEffect || selected.outlineBloomPostEffect) {
            layerBloomColor = selected.bloomColor;
            layerOutlineBloomColor = selected.outlineBloomColor;
        }
    } else {
        for (const auto& item : editorObjects_) {
            if (item.bloomPostEffect || item.outlineBloomPostEffect) {
                layerBloomColor = item.bloomColor;
                layerOutlineBloomColor = item.outlineBloomColor;
                break;
            }
        }
    }
    app.Render()->SetObjectLayerBloomColor(layerBloomColor);
    app.Render()->SetObjectLayerOutlineBloomColor(layerOutlineBloomColor);
    for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
        auto& item = editorObjects_[i];
        const bool isSelected = item.selected || (i == selectedEditorObject_);
        if (item.visible && item.active && (item.bloomPostEffect || item.outlineBloomPostEffect || isSelected) && item.object) {
            // This pass is the silhouette mask used by OutlineBloom.PS.  Drawing the
            // inverse-hull outline into the mask produces detached triangles on meshes
            // with split (hard-edge) vertices, so always draw the original mesh here.
            item.object->SetEnableOutline(false);
            item.object->Update(0.0f);
            item.object->Draw();
        }
    }
}

bool ParticleTestScene::HasObjectBloomTargets() const
{
    return std::any_of(editorObjects_.begin(), editorObjects_.end(), [](const EditorObject& item) {
        return item.visible && item.active && item.bloomPostEffect && item.object;
    });
}

bool ParticleTestScene::HasObjectOutlineBloomTargets() const
{
    for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
        const auto& item = editorObjects_[i];
        const bool isSelected = item.selected || (i == selectedEditorObject_);
        if (item.visible && item.active && (item.outlineBloomPostEffect || isSelected) && item.object) {
            return true;
        }
    }
    return false;
}

void ParticleTestScene::DrawSceneContent_(GameApp& app)
{
   /* if (ground_) {
        ground_->Draw();
    }*/

    for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
        auto& item = editorObjects_[i];
        if (item.visible && item.active && item.object) {
            const bool isSelected = item.selected || (i == selectedEditorObject_);
            // Selection is outlined from the object post-effect mask.  The geometric
            // inverse hull is unsuitable for editable/split-vertex meshes.
            item.object->SetEnableOutline(!isSelected && item.outlineBloomPostEffect);
            if (isSelected) {
                item.object->SetOutlineColor({ 1.0f, 0.65f, 0.0f, 1.0f });
                item.object->SetOutlineThickness(0.0f);
            } else {
                item.object->SetOutlineColor(item.outlineBloomColor);
                item.object->SetOutlineThickness(item.outlineBloomPostEffect ? 0.05f : 0.0f);
            }
            item.object->Update(0.0f);
            item.object->Draw();
        }
    }

    if (editorMode_ == EditorMode::PlayerAttack && showBossDummy_ && bossDummy_) {
        bossDummy_->Draw();
        if (showBossDummyHitbox_ && bossDummyHitboxCube_) {
            bossDummyHitboxCube_->Draw();
        }
    }

    if (playerAttackEditorEnabled_ && drawPlayerAttackHitbox_ &&
        previewPlayerAttackHitbox_.active && playerAttackHitboxCube_) {
        playerAttackHitboxCube_->Draw();
    }

    if (editorMode_ == EditorMode::Particle && editorParticle_) {
        app.ParticleCom()->SetGraphicsPipelineState();
        editorParticle_->Draw();
    }

    ParticleManager::GetInstance()->Draw(app.Dx()->GetCommandList());
	EffectManager::GetInstance()->Draw();
}

void ParticleTestScene::Draw3D(GameApp&)
{
}

void ParticleTestScene::Draw2D(GameApp&)
{
}

void ParticleTestScene::Draw(GameApp&)
{
}

void ParticleTestScene::EnsureHitEffectGroup_()
{
    auto* particleManager = ParticleManager::GetInstance();
    const std::string groupName = hitEffectGroupName_;
    if (groupName.empty()) {
        return;
    }

    if (!particleManager->HasGroup(groupName)) {
        particleManager->CreateParticleGroup(groupName, "resources/circle.png");
        particleManager->ConfigureHitEffectPreset(groupName);
    }
}

void ParticleTestScene::ReloadParticleJson_()
{
    auto* particleManager = ParticleManager::GetInstance();
    particleManager->ClearGroups();
    particleManager->Load(kParticleJson);
    EnsureHitEffectGroup_();
}

void ParticleTestScene::SpawnHitEffectPreview_()
{
    const std::string groupName = hitEffectGroupName_;
    if (groupName.empty()) {
        return;
    }

    EnsureHitEffectGroup_();
    ParticleManager::GetInstance()->Emit(
        groupName,
        hitEffectSpawnPosition_,
        static_cast<uint32_t>(std::max(1, hitEffectSpawnCount_)));
}

void ParticleTestScene::EnsureUniqueModelForObject_(EditorObject& item)
{
    if (!item.object) {
        return;
    }
    Model* currentModel = item.object->GetModel();
    if (!currentModel) {
        return;
    }

    std::string uniqueKey = "Unique_" + item.name + "_" + std::to_string(item.id);
    Model* existingUnique = ModelManager::GetInstance()->FindModel(uniqueKey);
    if (existingUnique && currentModel == existingUnique) {
        return;
    }

    Model* uniqueModel = ModelManager::GetInstance()->CreatePrimitiveModel(uniqueKey, currentModel->GetModelData());
    item.object->SetModel(uniqueModel);

    // すでにある頂点変形データを適用
    for (const auto& [idx, pos] : item.vertexOffsets) {
        uniqueModel->UpdateVertexPosition(idx, pos);
    }

    // パーティクルマネージャーの該当するグループのモデルも差し替える
    auto* pm = ParticleManager::GetInstance();
    if (item.geometryType >= 0) {
        pm->UpdateGroupModel(std::to_string(item.geometryType), 1, uniqueModel);
    } else {
        pm->UpdateGroupModel(item.modelPath, 2, uniqueModel);
    }
}

void ParticleTestScene::SyncParticleModelsWithEditorObjects_()
{
    auto* pm = ParticleManager::GetInstance();
    for (auto& item : editorObjects_) {
        if (!item.object || item.vertexOffsets.empty()) {
            continue;
        }
        Model* model = item.object->GetModel();
        if (model) {
            std::string uniqueKey = "Unique_" + item.name + "_" + std::to_string(item.id);
            if (model == ModelManager::GetInstance()->FindModel(uniqueKey)) {
                if (item.geometryType >= 0) {
                    pm->UpdateGroupModel(std::to_string(item.geometryType), 1, model);
                } else {
                    pm->UpdateGroupModel(item.modelPath, 2, model);
                }
            }
        }
    }
}

void ParticleTestScene::ApplyVertexOffsets_(EditorObject& item)
{
    if (!item.object) {
        return;
    }

    Model* model = item.object->GetModel();
    if (!model) {
        return;
    }

    Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
    if (!originalModel && item.geometryType >= 0) {
        originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
    }

    if (!item.vertexOffsets.empty()) {
        EnsureUniqueModelForObject_(item);
        model = item.object ? item.object->GetModel() : nullptr;
        if (!model) {
            return;
        }
    }

    if (originalModel) {
        const uint32_t vertexCount = model->GetVertexCount();
        for (uint32_t i = 0; i < vertexCount; ++i) {
            model->UpdateVertexPosition(i, originalModel->GetVertexPosition(i));
        }
    }

    for (const auto& [idx, pos] : item.vertexOffsets) {
        model->UpdateVertexPosition(idx, pos);
    }
}

std::unordered_map<uint32_t, Vector3> ParticleTestScene::LerpVertexOffsets_(const EditorObject& item, const EffectKeyframe& a, const EffectKeyframe& b, float t) const
{
    std::unordered_map<uint32_t, Vector3> result;

    Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
    if (!originalModel && item.geometryType >= 0) {
        originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
    }

    std::unordered_set<uint32_t> indices;
    indices.reserve(a.vertexOffsets.size() + b.vertexOffsets.size());
    for (const auto& [idx, _] : a.vertexOffsets) {
        indices.insert(idx);
    }
    for (const auto& [idx, _] : b.vertexOffsets) {
        indices.insert(idx);
    }

    for (uint32_t idx : indices) {
        Vector3 from{};
        Vector3 to{};

        if (auto it = a.vertexOffsets.find(idx); it != a.vertexOffsets.end()) {
            from = it->second;
        } else if (originalModel && idx < originalModel->GetVertexCount()) {
            from = originalModel->GetVertexPosition(idx);
        } else {
            continue;
        }

        if (auto it = b.vertexOffsets.find(idx); it != b.vertexOffsets.end()) {
            to = it->second;
        } else if (originalModel && idx < originalModel->GetVertexCount()) {
            to = originalModel->GetVertexPosition(idx);
        } else {
            continue;
        }

        Vector3 value = LerpVector3(from, to, t);
        if (originalModel && idx < originalModel->GetVertexCount()) {
            const Vector3 base = originalModel->GetVertexPosition(idx);
            const float dx = value.x - base.x;
            const float dy = value.y - base.y;
            const float dz = value.z - base.z;
            if (dx * dx + dy * dy + dz * dz < 0.0000001f) {
                continue;
            }
        }
        result[idx] = value;
    }

    return result;
}

void ParticleTestScene::UpdateVertexPositionGroup_(EditorObject& item, int baseVertexIndex, const Vector3& localDelta)
{
    EnsureUniqueModelForObject_(item);
    Model* model = item.object ? item.object->GetModel() : nullptr;
    if (!model) {
        return;
    }

    uint32_t vertexCount = model->GetVertexCount();

    // 初期モデルから基準の頂点座標を取得する
    Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
    if (!originalModel && item.geometryType >= 0) {
        originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
    }

    if (originalModel && baseVertexIndex >= 0 && baseVertexIndex < static_cast<int>(vertexCount)) {
        Vector3 origSelectedPos = originalModel->GetVertexPosition(baseVertexIndex);

        for (uint32_t i = 0; i < vertexCount; ++i) {
            Vector3 origPos = originalModel->GetVertexPosition(i);
            float dx = origPos.x - origSelectedPos.x;
            float dy = origPos.y - origSelectedPos.y;
            float dz = origPos.z - origSelectedPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < 0.0001f) { // 同じ位置と判定する閾値 (0.01m以内)
                if (!item.vertexOffsets.contains(i)) {
                    item.vertexOffsets[i] = origPos;
                }
                item.vertexOffsets[i] += localDelta;
                model->UpdateVertexPosition(i, item.vertexOffsets[i]);
            }
        }
    } else {
        // フォールバック（選択された頂点のみ移動）
        if (baseVertexIndex >= 0 && baseVertexIndex < static_cast<int>(vertexCount)) {
            if (!item.vertexOffsets.contains(baseVertexIndex)) {
                item.vertexOffsets[baseVertexIndex] = model->GetVertexPosition(baseVertexIndex);
            }
            item.vertexOffsets[baseVertexIndex] += localDelta;
            model->UpdateVertexPosition(baseVertexIndex, item.vertexOffsets[baseVertexIndex]);
        }
    }
}

void ParticleTestScene::MoveSelectedVertices_(EditorObject& item, const Vector3& localDelta)
{
    if (std::abs(localDelta.x) <= 0.00001f &&
        std::abs(localDelta.y) <= 0.00001f &&
        std::abs(localDelta.z) <= 0.00001f) {
        return;
    }

    if (item.selectedVertexIndices.empty()) {
        UpdateVertexPositionGroup_(item, item.selectedVertexIndex, localDelta);
        return;
    }

    EnsureUniqueModelForObject_(item);
    Model* model = item.object ? item.object->GetModel() : nullptr;
    if (!model) {
        return;
    }

    const uint32_t vertexCount = model->GetVertexCount();
    Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
    if (!originalModel && item.geometryType >= 0) {
        originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
    }

    std::unordered_set<uint32_t> affectedVertices;
    if (originalModel) {
        std::unordered_set<VertexPositionKey, VertexPositionKeyHash> selectedPositionKeys;
        selectedPositionKeys.reserve(item.selectedVertexIndices.size());
        for (int selIdx : item.selectedVertexIndices) {
            if (selIdx >= 0 && selIdx < static_cast<int>(vertexCount)) {
                selectedPositionKeys.insert(MakeVertexPositionKey(originalModel->GetVertexPosition(static_cast<uint32_t>(selIdx))));
            }
        }

        affectedVertices.reserve(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            if (selectedPositionKeys.contains(MakeVertexPositionKey(originalModel->GetVertexPosition(i)))) {
                affectedVertices.insert(i);
            }
        }
    } else {
        affectedVertices.reserve(item.selectedVertexIndices.size());
        for (int selIdx : item.selectedVertexIndices) {
            if (selIdx >= 0 && selIdx < static_cast<int>(vertexCount)) {
                affectedVertices.insert(static_cast<uint32_t>(selIdx));
            }
        }
    }

    for (uint32_t i : affectedVertices) {
        if (!item.vertexOffsets.contains(i)) {
            item.vertexOffsets[i] = originalModel ? originalModel->GetVertexPosition(i) : model->GetVertexPosition(i);
        }
        item.vertexOffsets[i] += localDelta;
        model->UpdateVertexPosition(i, item.vertexOffsets[i]);
    }
}

void ParticleTestScene::UpdateCameraControls_(GameApp& app, float dt)
{
#ifdef USE_IMGUI
    if (!camera_ || !gHasSceneImageRect) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInsideScene =
        mouse.x >= gSceneImageMin.x && mouse.x <= gSceneImageMax.x &&
        mouse.y >= gSceneImageMin.y && mouse.y <= gSceneImageMax.y;

    // --- 物理キー状態の取得（GetAsyncKeyState併用で暴走を完全防止） ---
    // マウスボタン: ImGui AND 物理 → 片方でも離されたら即false
    const bool physRightDown  = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool physMiddleDown = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    const bool physShiftDown  = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

    const bool isRightMouseDown  = ImGui::IsMouseDown(ImGuiMouseButton_Right) && physRightDown;
    const bool isMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle) && physMiddleDown;
    // Shift: ImGuiまたは物理のどちらかがtrueなら「押している」判定（開始用）
    // ただしリリース判定は物理キーで行う
    const bool isShiftDown = (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) && physShiftDown;

    // WASDQEキー: ImGui AND 物理の両方で判定（スタック防止）
    auto isKeyPhysicallyDown = [](ImGuiKey imguiKey, int vk) -> bool {
        return ImGui::IsKeyDown(imguiKey) && (GetAsyncKeyState(vk) & 0x8000);
    };
    const bool keyW = isKeyPhysicallyDown(ImGuiKey_W, 0x57);
    const bool keyS = isKeyPhysicallyDown(ImGuiKey_S, 0x53);
    const bool keyA = isKeyPhysicallyDown(ImGuiKey_A, 0x41);
    const bool keyD = isKeyPhysicallyDown(ImGuiKey_D, 0x44);
    const bool keyQ = isKeyPhysicallyDown(ImGuiKey_Q, 0x51);
    const bool keyE = isKeyPhysicallyDown(ImGuiKey_E, 0x45);
    const bool isAnyWASDQE = keyW || keyS || keyA || keyD || keyQ || keyE;

    // --- 右クリック（ドラッグ）によるカメラ回転のアクティブ判定 ---
    EditorObject* selectedObj = nullptr;
    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        selectedObj = &editorObjects_[selectedEditorObject_];
    }
    const bool isEditMode = selectedObj && selectedObj->editMode;

    bool allowRightClickRotate = !isEditMode || isShiftDown;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouseInsideScene && allowRightClickRotate) {
        editorCameraControlActive_ = true;
    }
    if (!isRightMouseDown) {
        editorCameraControlActive_ = false;
    }

    // --- 中クリックによるPan判定 ---
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && mouseInsideScene) {
        editorCameraPanActive_ = true;
    }
    if (!isMiddleMouseDown) {
        editorCameraPanActive_ = false;
    }

    // --- 2本指回転（ホイール） ---
    bool twoFingerRotateActive = false;
    if (mouseInsideScene && !editorCameraControlActive_ && !editorCameraPanActive_ && !boxSelectActive_) {
        if (std::abs(io.MouseWheel) > 0.01f || std::abs(io.MouseWheelH) > 0.01f) {
            editorCameraRotation_.y += io.MouseWheelH * 0.02f;
            editorCameraRotation_.x -= io.MouseWheel * 0.02f;
            editorCameraRotation_.x = std::clamp(editorCameraRotation_.x, -kPi * 0.49f, kPi * 0.49f);
            twoFingerRotateActive = true;
        }
    }

    // --- Shift+WASD移動のアクティブ判定 ---
    if (isShiftDown && isAnyWASDQE && mouseInsideScene) {
        shiftMovementActive_ = true;
    }
    if (!isShiftDown || !isAnyWASDQE) {
        shiftMovementActive_ = false;
    }

    bool cameraChanged = false;

    // === 平行移動（Pan）===
    if (editorCameraPanActive_) {
        const Matrix4x4 cameraRotation = Matrix4x4::RotateXYZ(editorCameraRotation_.x, editorCameraRotation_.y, editorCameraRotation_.z);
        const Vector3 right = CameraRight(cameraRotation);
        const Vector3 up = CameraUp(cameraRotation);
        const float panSpeed = 0.02f * editorCameraMoveSpeed_;
        
        editorCameraPosition_ -= right * (io.MouseDelta.x * panSpeed);
        editorCameraPosition_ += up * (io.MouseDelta.y * panSpeed);
        cameraChanged = true;
    }

    // === 右ドラッグ回転（WASDは含めない！） ===
    if (editorCameraControlActive_) {
        editorCameraRotation_.y += io.MouseDelta.x * editorCameraLookSpeed_;
        editorCameraRotation_.x += io.MouseDelta.y * editorCameraLookSpeed_;
        editorCameraRotation_.x = std::clamp(editorCameraRotation_.x, -kPi * 0.49f, kPi * 0.49f);
        cameraChanged = true;
    }

    // === 2本指回転の反映 ===
    if (twoFingerRotateActive) {
        cameraChanged = true;
    }

    // === Shift+WASD移動（Shiftが押されている時のみ） ===
    if (shiftMovementActive_) {
        const Matrix4x4 cameraRotation = Matrix4x4::RotateXYZ(editorCameraRotation_.x, editorCameraRotation_.y, editorCameraRotation_.z);
        const Vector3 right = CameraRight(cameraRotation);
        const Vector3 up = CameraUp(cameraRotation);
        const Vector3 forward = CameraForward(cameraRotation);
        const float speed = editorCameraMoveSpeed_ * 1.0f;

        if (keyW) { editorCameraPosition_ += forward * speed; }
        if (keyS) { editorCameraPosition_ -= forward * speed; }
        if (keyD) { editorCameraPosition_ += right * speed; }
        if (keyA) { editorCameraPosition_ -= right * speed; }
        if (keyE) { editorCameraPosition_ += up * speed; }
        if (keyQ) { editorCameraPosition_ -= up * speed; }
        cameraChanged = true;
    }

    if (cameraChanged) {
        camera_->SetTranslate(editorCameraPosition_);
        camera_->SetRotate(editorCameraRotation_);
        camera_->Update();
    }
#endif
}


void ParticleTestScene::ApplyPlayerSpecialPreviewPosition_() {

    if (playerAttackObjectIndex_ < 0 ||
        playerAttackObjectIndex_ >= static_cast<int>(editorObjects_.size())) {


        return;
    }

    // playerを取得
    
	EditorObject& player = editorObjects_[playerAttackObjectIndex_];

    // 位置を設定
    
	player.position = playerSpecialPreviewOrigin_ + previewSpecialPositionOffset_ +
		Vector3{ 0.0f, 0.0f, previewSpecialVisualZOffset_ };

    // Object3Dに設定

    ApplyEditorObjectTransform_(player);
}
