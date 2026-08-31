#include "ParticleTestScene.h"
#include "ParticleTestSceneSupport.h"

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
#include <regex>

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
void ParticleTestScene::AddEditorObject_(GameApp& app, const std::string& modelPath)
{
    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "EffectObject_%02d", item.id);
    item.name = name;
    item.modelPath = modelPath.empty() ? "cube/cube.obj" : modelPath;
    item.geometryType = -1;
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetModel(item.modelPath);
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::EnsurePlayerAttackEditor_(GameApp& app)
{
    EnsurePlayerSpecialTimelineDefaults_();

    for (int i = static_cast<int>(editorObjects_.size()) - 1; i >= 0; --i) {
        const EditorObject& item = editorObjects_[i];
        const bool defaultBlenderCube =
            item.name.rfind("EffectObject_", 0) == 0 &&
            item.modelPath == "cube/cube.obj" &&
            item.geometryType < 0 &&
            item.keyframes.empty() &&
            !item.attachToBone;
        if (!defaultBlenderCube) {
            continue;
        }

        editorObjects_.erase(editorObjects_.begin() + i);
        if (playerAttackObjectIndex_ == i) {
            playerAttackObjectIndex_ = -1;
        } else if (playerAttackObjectIndex_ > i) {
            --playerAttackObjectIndex_;
        }
        if (selectedEditorObject_ == i) {
            selectedEditorObject_ = -1;
        } else if (selectedEditorObject_ > i) {
            --selectedEditorObject_;
        }
    }

    if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
        EditorObject& player = editorObjects_[playerAttackObjectIndex_];
        if (player.name == "PlayerAttack_Player" && player.modelPath == "Player/player.gltf") {
			if (!playerSpecialPreviewOriginInitialized_) {
				playerSpecialPreviewOrigin_ = player.position - previewSpecialPositionOffset_;
				playerSpecialPreviewOriginInitialized_ = true;
			}
            playerAttackEditorEnabled_ = true;
            selectedEditorObject_ = playerAttackObjectIndex_;
            EvaluatePlayerSpecialTimeline_();
            return;
        }
        playerAttackObjectIndex_ = -1;
    }

    int existingPlayerIndex = -1;
    for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
        const EditorObject& item = editorObjects_[i];
        if (item.name == "PlayerAttack_Player" && item.modelPath == "Player/player.gltf") {
            if (existingPlayerIndex < 0) {
                existingPlayerIndex = i;
            }
        }
    }

    if (existingPlayerIndex >= 0) {
        for (int i = static_cast<int>(editorObjects_.size()) - 1; i >= 0; --i) {
            const EditorObject& item = editorObjects_[i];
            if (i != existingPlayerIndex && item.name == "PlayerAttack_Player" && item.modelPath == "Player/player.gltf") {
                editorObjects_.erase(editorObjects_.begin() + i);
                if (i < existingPlayerIndex) {
                    --existingPlayerIndex;
                }
            }
        }
        playerAttackObjectIndex_ = existingPlayerIndex;
        selectedEditorObject_ = playerAttackObjectIndex_;
        selectedParticleNode_ = -1;
        editorObjects_[playerAttackObjectIndex_].showBones = true;
		if (!playerSpecialPreviewOriginInitialized_) {
			playerSpecialPreviewOrigin_ = editorObjects_[playerAttackObjectIndex_].position - previewSpecialPositionOffset_;
			playerSpecialPreviewOriginInitialized_ = true;
		}
        SyncEditorObjectBones_(editorObjects_[playerAttackObjectIndex_]);
        playerAttackEditorEnabled_ = true;
        EvaluatePlayerAttackHitbox_();
        EvaluatePlayerSpecialTimeline_();
        return;
    }

    AddEditorObject_(app, "Player/player.gltf");
    playerAttackObjectIndex_ = selectedEditorObject_;
    if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
        EditorObject& player = editorObjects_[playerAttackObjectIndex_];
        player.name = "PlayerAttack_Player";
        player.position = { 0.0f, 0.0f, 0.0f };
		playerSpecialPreviewOrigin_ = player.position;
		playerSpecialPreviewOriginInitialized_ = true;
        player.rotation = { 0.0f, 0.0f, 0.0f };
        player.scale = { 1.0f, 1.0f, 1.0f };
        player.showBones = true;
        ApplyEditorObjectTransform_(player);
        SyncEditorObjectBones_(player);
    }

    if (playerAttackHitboxKeyframes_.empty()) {
        currentPlayerAttackHitbox_ = {
            0.0f,
            { 1.0f, 1.0f, 0.0f },
            { 0.6f, 0.8f, 0.5f },
            true
        };
        previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
        playerAttackHitboxKeyframes_.push_back(currentPlayerAttackHitbox_);
        playerAttackHitboxKeyframes_.push_back({
            0.18f,
            { 1.3f, 1.0f, 0.0f },
            { 0.8f, 0.85f, 0.5f },
            true
        });
        timelineDuration_ = std::max(timelineDuration_, 0.45f);
    }

    playerAttackEditorEnabled_ = true;
    EvaluatePlayerAttackHitbox_();
    EvaluatePlayerSpecialTimeline_();
}

void ParticleTestScene::AddGeometryObject_(GameApp& app, int geometryType)
{
    geometryType = std::clamp(geometryType, 0, kGeometryCount - 1);

    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "%s_%02d", kGeometryNames[geometryType], item.id);
    item.name = name;
    item.modelPath = std::string("geometry:") + kGeometryNames[geometryType];
    item.geometryType = geometryType;
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetModel(GetOrCreateEditorGeometryModel(geometryType));
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::PasteEditorObject_(GameApp& app)
{
    if (!hasCopiedObject_) {
        return;
    }

    EditorObject item;
    item.id = nextEditorObjectId_++;
    
    std::string baseName = copiedObject_.name;
    try {
        std::regex copyRegex("(_Copy(_\\d+)?)+$");
        baseName = std::regex_replace(baseName, copyRegex, "");
    } catch (...) {
        // Fallback in case of regex error
    }

    char name[128]{};
    std::snprintf(name, sizeof(name), "%s_Copy_%02d", baseName.c_str(), item.id);
    item.name = name;
    item.modelPath = copiedObject_.modelPath;
    item.geometryType = copiedObject_.geometryType;
    item.position = copiedObject_.position + Vector3{ 0.5f, 0.0f, 0.0f };
    item.rotation = copiedObject_.rotation;
    item.scale = copiedObject_.scale;
    item.color = copiedObject_.color;
    item.texturePath = copiedObject_.texturePath;
    item.blendMode = copiedObject_.blendMode;
    item.billboard = copiedObject_.billboard;
    item.bloomPostEffect = copiedObject_.bloomPostEffect;
    item.outlineBloomPostEffect = copiedObject_.outlineBloomPostEffect;
    item.bloomColor = copiedObject_.bloomColor;
    item.outlineBloomColor = copiedObject_.outlineBloomColor;
    item.showBones = copiedObject_.showBones;
    item.selectedBone = copiedObject_.selectedBone;
    item.bonePoses = copiedObject_.bonePoses;
    item.keyframes = copiedObject_.keyframes;
    for (auto& key : item.keyframes) {
        key.position += Vector3{ 0.5f, 0.0f, 0.0f };
    }
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    if (item.geometryType >= 0) {
        item.object->SetModel(GetOrCreateEditorGeometryModel(item.geometryType));
    } else {
        item.object->SetModel(item.modelPath);
    }
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::DuplicateSelectedObject_(GameApp& app)
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    const EditorObjectSnapshot src = CaptureSelectedObject_();
    if (src.geometryType >= 0) {
        AddGeometryObject_(app, src.geometryType);
    } else {
        AddEditorObject_(app, src.modelPath);
    }
    EditorObject& dst = editorObjects_.back();
    dst.position = src.position + Vector3{ 0.5f, 0.0f, 0.0f };
    dst.geometryType = src.geometryType;
    dst.modelPath = src.modelPath;
    if (dst.object) {
        if (dst.geometryType >= 0) {
            dst.object->SetModel(GetOrCreateEditorGeometryModel(dst.geometryType));
        } else {
            dst.object->SetModel(dst.modelPath);
        }
    }
    dst.rotation = src.rotation;
    dst.scale = src.scale;
    dst.color = src.color;
    dst.texturePath = src.texturePath;
    dst.blendMode = src.blendMode;
    dst.billboard = src.billboard;
    dst.bloomPostEffect = src.bloomPostEffect;
    dst.outlineBloomPostEffect = src.outlineBloomPostEffect;
    dst.bloomColor = src.bloomColor;
    dst.outlineBloomColor = src.outlineBloomColor;
    dst.showBones = src.showBones;
    dst.selectedBone = src.selectedBone;
    dst.bonePoses = src.bonePoses;
    dst.attachToBone = src.attachToBone;
    dst.attachParentId = src.attachParentId;
    dst.attachJointName = src.attachJointName;
    dst.attachOffset = src.attachOffset;
    dst.attachRotation = src.attachRotation;
    dst.attachScale = src.attachScale;
    dst.keyframes = src.keyframes;
    ApplyEditorObjectTransform_(dst);
}

ParticleTestScene::EditorObjectSnapshot ParticleTestScene::CaptureSelectedObject_() const
{
    EditorObjectSnapshot snapshot;
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return snapshot;
    }

    const EditorObject& item = editorObjects_[selectedEditorObject_];
    snapshot.id = item.id;
    snapshot.name = item.name;
    snapshot.modelPath = item.modelPath;
    snapshot.geometryType = item.geometryType;
    snapshot.position = item.position;
    snapshot.rotation = item.rotation;
    snapshot.scale = item.scale;
    snapshot.color = item.color;
    snapshot.texturePath = item.texturePath;
    snapshot.blendMode = item.blendMode;
    snapshot.billboard = item.billboard;
    snapshot.bloomPostEffect = item.bloomPostEffect;
    snapshot.outlineBloomPostEffect = item.outlineBloomPostEffect;
    snapshot.bloomColor = item.bloomColor;
    snapshot.outlineBloomColor = item.outlineBloomColor;
    snapshot.showBones = item.showBones;
    snapshot.selectedBone = item.selectedBone;
    snapshot.bonePoses = item.bonePoses;
    snapshot.attachToBone = item.attachToBone;
    snapshot.attachParentId = item.attachParentId;
    snapshot.attachJointName = item.attachJointName;
    snapshot.attachOffset = item.attachOffset;
    snapshot.attachRotation = item.attachRotation;
    snapshot.attachScale = item.attachScale;
    snapshot.keyframes = item.keyframes;
    snapshot.visible = item.visible;
    return snapshot;
}

void ParticleTestScene::CopySelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }
    copiedObject_ = CaptureSelectedObject_();
    hasCopiedObject_ = true;
	hasCopiedModelKeyframe_ = false;
}

void ParticleTestScene::RequestDeleteSelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    if (!pendingDeleteSelectedObject_) {
        PushUndoSnapshot_();
    }

    pendingDeleteSelectedObject_ = true;
    activeViewportGizmoAxis_ = -1;
    transformDragActive_ = false;
    transformDragChanged_ = false;
}

void ParticleTestScene::DeleteSelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    editorObjects_.erase(editorObjects_.begin() + selectedEditorObject_);
    if (editorObjects_.empty()) {
        selectedEditorObject_ = -1;
    } else {
        selectedEditorObject_ = std::min(selectedEditorObject_, static_cast<int>(editorObjects_.size()) - 1);
    }
}

void ParticleTestScene::ApplyEditorObjectTransform_(EditorObject& item)
{
    if (!item.object) {
        return;
    }
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetTranslate(item.position);
    item.object->SetRotate(item.rotation);
    item.object->SetBillboard(item.billboard);
    item.object->SetScale(item.scale);
    item.object->SetMaterialColor(item.color);
    item.object->SetBlendMode(item.blendMode);
    if (!item.texturePath.empty()) {
        item.object->SetTexture(item.texturePath);
    } else {
        item.object->ClearTextureOverride();
    }
    ApplyEditorObjectBonePose_(item);
}

Camera* ParticleTestScene::GetSceneCamera_() const
{
    if (editorMode_ == EditorMode::PlayerAttack && useGameCameraPreview_ && gamePreviewCamera_) {
        return gamePreviewCamera_.get();
    }
    if (useAnimationCameraPreview_ && animationCameraPreviewSwapped_ && animationCamera_) {
        return animationCamera_.get();
    }
    return camera_.get();
}

Camera* ParticleTestScene::GetPreviewCamera_() const
{
    if (animationCameraPreviewSwapped_ && camera_) {
        return camera_.get();
    }
    if (animationCamera_) {
        return animationCamera_.get();
    }
    return camera_.get();
}

void ParticleTestScene::ApplyCameraToEditorObjects_()
{
    Camera* sceneCamera = GetSceneCamera_();
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
    if (playerAttackHitboxCube_) playerAttackHitboxCube_->SetCamera(sceneCamera);
    if (bossDummy_) bossDummy_->SetCamera(sceneCamera);
    if (bossDummyHitboxCube_) bossDummyHitboxCube_->SetCamera(sceneCamera);
    EffectManager::GetInstance()->SetCamera(sceneCamera);
}

void ParticleTestScene::ApplyAnimationCamera_()
{
    if (!animationCamera_) {
        return;
    }
    animationCamera_->SetTranslate(animationCameraPosition_);
    animationCamera_->SetRotate(animationCameraRotation_);
    animationCamera_->SetFovY(animationCameraFovY_);
    animationCamera_->Update();
}

void ParticleTestScene::SyncEditorObjectBones_(EditorObject& item)
{
    if (!item.object || !item.object->HasSkinningModel()) {
        item.selectedBone = 0;
        item.bonePoses.clear();
        return;
    }

    const Model::Skeleton* skeleton = item.object->GetSkeleton();
    if (!skeleton) {
        item.selectedBone = 0;
        item.bonePoses.clear();
        return;
    }

    std::vector<EditorBonePose> next;
    next.reserve(skeleton->joints.size());
    for (const auto& joint : skeleton->joints) {
        EditorBonePose pose;
        pose.name = joint.name;
        auto it = std::find_if(item.bonePoses.begin(), item.bonePoses.end(), [&](const EditorBonePose& current) {
            return current.name == joint.name;
        });
        if (it != item.bonePoses.end()) {
            pose.translate = it->translate;
            pose.rotate = it->rotate;
            pose.scale = it->scale;
        }
        next.push_back(std::move(pose));
    }

    item.bonePoses = std::move(next);
    if (item.selectedBone >= static_cast<int>(item.bonePoses.size())) {
        item.selectedBone = item.bonePoses.empty() ? 0 : static_cast<int>(item.bonePoses.size()) - 1;
    }
}

void ParticleTestScene::ApplyEditorObjectBonePose_(EditorObject& item)
{
    if (!item.object || !item.object->HasSkinningModel()) {
        return;
    }

    SyncEditorObjectBones_(item);
    item.object->ResetManualJointTransforms();
    for (int i = 0; i < static_cast<int>(item.bonePoses.size()); ++i) {
        const EditorBonePose& pose = item.bonePoses[i];
        item.object->SetManualJointTransform(i, pose.translate, pose.rotate, pose.scale);
    }
}

void ParticleTestScene::SortKeyframes_(EditorObject& item)
{
    std::sort(item.keyframes.begin(), item.keyframes.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) {
        return a.time < b.time;
    });
}

void ParticleTestScene::SortCameraKeyframes_()
{
    std::sort(cameraKeyframes_.begin(), cameraKeyframes_.end(), [](const CameraKeyframe& a, const CameraKeyframe& b) {
        return a.time < b.time;
    });
}

