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
#include "Effect/EffectManager.h"
#include "RenderManager.h"
#include "TextureManager.h"

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
void ParticleTestScene::AddCameraKeyframe_()
{
	for (auto& key : cameraKeyframes_) {
		if (std::abs(key.time - timelineTime_) < 0.001f) {
			key.position = animationCameraPosition_;
			key.rotation = animationCameraRotation_;
			key.fovY = animationCameraFovY_;
			return;
		}
	}

	cameraKeyframes_.push_back({ timelineTime_, animationCameraPosition_, animationCameraRotation_, animationCameraFovY_ });
	SortCameraKeyframes_();
}

void ParticleTestScene::DeleteNearestCameraKeyframe_()
{
	if (cameraKeyframes_.empty()) {
		return;
	}

	if (selectedKeyframeType_ == DragTarget::CameraKeyframe &&
		selectedKeyframeIndex_ >= 0 &&
		selectedKeyframeIndex_ < static_cast<int>(cameraKeyframes_.size())) {
		cameraKeyframes_.erase(cameraKeyframes_.begin() + selectedKeyframeIndex_);
		selectedKeyframeIndex_ = -1;
		selectedKeyframeType_ = DragTarget::None;
		selectedKeyframeObjectIndex_ = -1;
		EvaluateTimeline_(false);
		return;
	}

	auto it = std::min_element(cameraKeyframes_.begin(), cameraKeyframes_.end(), [this](const CameraKeyframe& a, const CameraKeyframe& b) {
		return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
		});
	if (it != cameraKeyframes_.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
		int targetIdx = static_cast<int>(std::distance(cameraKeyframes_.begin(), it));
		cameraKeyframes_.erase(it);
		if (selectedKeyframeType_ == DragTarget::CameraKeyframe &&
			selectedKeyframeIndex_ == targetIdx) {
			selectedKeyframeIndex_ = -1;
			selectedKeyframeType_ = DragTarget::None;
			selectedKeyframeObjectIndex_ = -1;
		}
	}
}

void ParticleTestScene::AddPlayerAttackHitboxKeyframe_()
{
	for (auto& key : playerAttackHitboxKeyframes_) {
		if (std::abs(key.time - timelineTime_) < 0.001f) {
			key = currentPlayerAttackHitbox_;
			key.time = timelineTime_;
			SortPlayerAttackHitboxKeyframes_();
			return;
		}
	}

	currentPlayerAttackHitbox_.time = timelineTime_;
	playerAttackHitboxKeyframes_.push_back(currentPlayerAttackHitbox_);
	SortPlayerAttackHitboxKeyframes_();
}

void ParticleTestScene::DeleteNearestPlayerAttackHitboxKeyframe_()
{
	if (playerAttackHitboxKeyframes_.empty()) {
		return;
	}

	if (selectedKeyframeType_ == DragTarget::PlayerAttackHitboxKeyframe &&
		selectedKeyframeIndex_ >= 0 &&
		selectedKeyframeIndex_ < static_cast<int>(playerAttackHitboxKeyframes_.size())) {
		playerAttackHitboxKeyframes_.erase(playerAttackHitboxKeyframes_.begin() + selectedKeyframeIndex_);
		selectedKeyframeIndex_ = -1;
		selectedKeyframeType_ = DragTarget::None;
		selectedKeyframeObjectIndex_ = -1;
		EvaluateTimeline_(false);
		return;
	}

	auto it = std::min_element(playerAttackHitboxKeyframes_.begin(), playerAttackHitboxKeyframes_.end(), [this](const PlayerAttackHitboxKeyframe& a, const PlayerAttackHitboxKeyframe& b) {
		return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
		});
	if (it != playerAttackHitboxKeyframes_.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
		int targetIdx = static_cast<int>(std::distance(playerAttackHitboxKeyframes_.begin(), it));
		playerAttackHitboxKeyframes_.erase(it);
		if (selectedKeyframeType_ == DragTarget::PlayerAttackHitboxKeyframe &&
			selectedKeyframeIndex_ == targetIdx) {
			selectedKeyframeIndex_ = -1;
			selectedKeyframeType_ = DragTarget::None;
			selectedKeyframeObjectIndex_ = -1;
		}
	}
}

void ParticleTestScene::SortPlayerAttackHitboxKeyframes_()
{
	std::sort(playerAttackHitboxKeyframes_.begin(), playerAttackHitboxKeyframes_.end(), [](const PlayerAttackHitboxKeyframe& a, const PlayerAttackHitboxKeyframe& b) {
		return a.time < b.time;
		});
}

void ParticleTestScene::EvaluatePlayerAttackHitbox_()
{
	if (playerAttackHitboxKeyframes_.empty()) {
		previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
		return;
	}

	SortPlayerAttackHitboxKeyframes_();
	if (timelineTime_ <= playerAttackHitboxKeyframes_.front().time) {
		currentPlayerAttackHitbox_ = playerAttackHitboxKeyframes_.front();
		previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
		return;
	}
	if (timelineTime_ >= playerAttackHitboxKeyframes_.back().time) {
		currentPlayerAttackHitbox_ = playerAttackHitboxKeyframes_.back();
		previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
		return;
	}

	for (size_t i = 0; i + 1 < playerAttackHitboxKeyframes_.size(); ++i) {
		const auto& a = playerAttackHitboxKeyframes_[i];
		const auto& b = playerAttackHitboxKeyframes_[i + 1];
		if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
			const float range = std::max(0.001f, b.time - a.time);
			const float t = (timelineTime_ - a.time) / range;
			currentPlayerAttackHitbox_.time = timelineTime_;
			currentPlayerAttackHitbox_.offset = LerpVector3(a.offset, b.offset, t);
			currentPlayerAttackHitbox_.halfSize = LerpVector3(a.halfSize, b.halfSize, t);
			currentPlayerAttackHitbox_.active = a.active;
			previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
			return;
		}
	}
}

void ParticleTestScene::EnsurePlayerSpecialTimelineDefaults_()
{
	const char* attackNames[] = {
		 "NeutralSpecial",
	     "SideSpecial",
 	     "UpSpecial",
	     "DownSpecial",
	};

	for (size_t attackIndex = 0; attackIndex <playerSpecialTimelines_.size(); ++attackIndex) {

		for (size_t level = 0; level < playerSpecialTimelines_[attackIndex].size(); ++level) {


			PlayerSpecialTimeline& timeline = playerSpecialTimelines_[attackIndex][level];

			const std::string defaultName = std::string(attackNames[attackIndex]) + "_Level" + std::to_string(level);

			if (timeline.name.empty() ||
				timeline.name == "SideSpecial") {
				timeline.name = defaultName;
			}

			if (timeline.totalSec <= 0.0f) {

				timeline.totalSec = 0.45f;
			}


		}


	}

}

//選択中の攻撃種類・レベルに対応したタイムラインを返す
ParticleTestScene::PlayerSpecialTimeline& ParticleTestScene::CurrentPlayerSpecialTimeline_()
{

	//選択中の攻撃種類
	const size_t attackIndex = static_cast<size_t>(selectedPlayerSpecialAttackType_);

	//0~3に制限したレベル
	const int levelIndex = std::clamp(selectedPlayerSpecialLevel_, 0, 3);
	//選択中の攻撃種類・レベルに対応したタイムラインを返す
	return playerSpecialTimelines_[attackIndex][levelIndex];

}

//選択中の攻撃種類・レベルに対応したタイムラインを返す
const ParticleTestScene::PlayerSpecialTimeline& ParticleTestScene::CurrentPlayerSpecialTimeline_() const
{
	//選択中の攻撃種類
	const size_t attackIndex = static_cast<size_t>(selectedPlayerSpecialAttackType_);

	//0~3に制限したレベル
	const int levelIndex = std::clamp(selectedPlayerSpecialLevel_, 0, 3);
	//選択中の攻撃種類・レベルに対応したタイムラインを返す
	return playerSpecialTimelines_[attackIndex][levelIndex];

}

//選択中の攻撃種類・レベルに対応したタイムラインをソートする
void ParticleTestScene::SortCurrentPlayerSpecialTimeline_()
{
	PlayerSpecialTimeline& timeline = CurrentPlayerSpecialTimeline_();
	std::sort(timeline.hitboxes.begin(), timeline.hitboxes.end(), [](const PlayerSpecialHitboxKeyframe& a, const PlayerSpecialHitboxKeyframe& b) {
		return a.time < b.time;
		});
	std::sort(timeline.motions.begin(), timeline.motions.end(), [](const PlayerSpecialMotionKeyframe& a, const PlayerSpecialMotionKeyframe& b) {
		return a.time < b.time;
		});
	std::sort(timeline.animations.begin(), timeline.animations.end(), [](const PlayerSpecialAnimationKeyframe& a, const PlayerSpecialAnimationKeyframe& b) {
		return a.time < b.time;
		});
	std::sort(timeline.events.begin(), timeline.events.end(), [](const PlayerSpecialEventKeyframe& a, const PlayerSpecialEventKeyframe& b) {
		return a.time < b.time;
		});

	std::sort(
		timeline.positionKeyframes.begin(),
		timeline.positionKeyframes.end(),
		[](const PlayerSpecialPositionKeyframe& a,
			const PlayerSpecialPositionKeyframe& b) {
				return a.time<b.time;
		}
	);
	std::sort(timeline.opacityKeyframes.begin(), timeline.opacityKeyframes.end(),
		[](const PlayerSpecialOpacityKeyframe& a, const PlayerSpecialOpacityKeyframe& b) { return a.time < b.time; });
	std::sort(timeline.visualZKeyframes.begin(), timeline.visualZKeyframes.end(),
		[](const PlayerSpecialVisualZKeyframe& a, const PlayerSpecialVisualZKeyframe& b) { return a.time < b.time; });
	std::sort(timeline.rotationKeyframes.begin(), timeline.rotationKeyframes.end(),
		[](const PlayerSpecialRotationKeyframe& a, const PlayerSpecialRotationKeyframe& b) { return a.time < b.time; });
	std::sort(timeline.effectKeyframes.begin(), timeline.effectKeyframes.end(),
		[](const PlayerSpecialEffectKeyframe& a, const PlayerSpecialEffectKeyframe& b) { return a.time < b.time; });
	std::sort(timeline.particleKeyframes.begin(), timeline.particleKeyframes.end(),
		[](const PlayerSpecialParticleKeyframe& a, const PlayerSpecialParticleKeyframe& b) { return a.time < b.time; });

}

void ParticleTestScene::SyncPlayerSpecialPreviewNodes_()
{
	particleNodes_.clear();
	selectedParticleNode_ = -1;

	const PlayerSpecialTimeline& timeline = CurrentPlayerSpecialTimeline_();
	for (const PlayerSpecialEffectKeyframe& key : timeline.effectKeyframes) {
		if (key.jsonPath.empty()) continue;
		ParticleNode node;
		node.name = key.name.empty() ? "AttackEffect" : key.name;
		node.particleFileName = key.jsonPath;
		node.isEffectNode = true;
		node.startTime = key.time;
		node.presetDuration = std::max(0.01f, timeline.totalSec - key.time);
		node.endTime = node.startTime + node.presetDuration;
		node.followOwnerMovement =
			key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer;
		node.ownerOffset = key.offset;
		if (key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint &&
			key.movementPointIndex >= 0 &&
			key.movementPointIndex < static_cast<int>(timeline.positionKeyframes.size())) {
			node.position = playerSpecialPreviewOrigin_ +
				ResolvePlayerSpecialPositionOffset_(timeline.positionKeyframes[key.movementPointIndex]) + key.offset;
		} else {
			node.position = playerSpecialPreviewOrigin_ + key.offset;
		}
		particleNodes_.push_back(std::move(node));
	}

	auto* particleManager = ParticleManager::GetInstance();
	for (const PlayerSpecialParticleKeyframe& key : timeline.particleKeyframes) {
		if (key.jsonPath.empty()) continue;
		particleManager->LoadAdditional(key.jsonPath, "");
		ParticleNode node;
		node.name = key.name.empty() ? "AttackParticle" : key.name;
		node.particleFileName = key.jsonPath;
		node.startTime = key.time;
		node.presetDuration = std::max(0.01f, key.duration);
		node.endTime = node.startTime + node.presetDuration;
		node.position = key.position;
		node.rotation = key.rotation;
		node.scale = key.scale;
		node.emitCount = std::max(1, key.emitCount);
		particleNodes_.push_back(std::move(node));
	}

	for (const ParticleNode& node : particleNodes_) {
		timelineDuration_ = std::max(timelineDuration_, node.endTime);
	}
}

void ParticleTestScene::EvaluatePlayerSpecialTimeline_()
{
	EnsurePlayerSpecialTimelineDefaults_();
	SortCurrentPlayerSpecialTimeline_();

	// ヒットボックスの状態に関係なく位置を評価する
	EvaluatePlayerSpecialPosition_();

	EvaluatePlayerSpecialVisualZ_();
	// Gameplay position and presentation-only depth are combined only for the model.
	ApplyPlayerSpecialPreviewPosition_();
	EvaluatePlayerSpecialRotation_();
	EvaluatePlayerSpecialOpacity_();
	EvaluatePlayerSpecialAnimation_();
	// Effect keys are restored as dope-sheet nodes and emitted by EvaluateTimeline_.
	// Keeping a second direct trigger here produced duplicate effect instances.

	const PlayerSpecialTimeline& timeline = CurrentPlayerSpecialTimeline_();
	previewPlayerAttackHitbox_.time = timelineTime_;
	previewPlayerAttackHitbox_.active = false;
	// During playback the registered keys always win, even when live editing is enabled.
	if (livePreviewSpecialEdit_ && !timelinePlaying_) {
		previewPlayerAttackHitbox_.time = currentSpecialHitbox_.time;
		previewPlayerAttackHitbox_.offset = currentSpecialHitbox_.offset;
		previewPlayerAttackHitbox_.halfSize = currentSpecialHitbox_.halfSize;
		previewPlayerAttackHitbox_.active = currentSpecialHitbox_.active;
		previewPlayerAttackHitbox_.followPlayerMovement = currentSpecialHitbox_.followPlayerMovement;
		return;
	}
	for (const PlayerSpecialHitboxKeyframe& key : timeline.hitboxes) {
		const bool inRange = timelineTime_ >= key.time && timelineTime_ <= key.time + key.duration;
		if (!inRange) {
			continue;
		}

		previewPlayerAttackHitbox_.offset = key.offset;
		previewPlayerAttackHitbox_.halfSize = key.halfSize;
		previewPlayerAttackHitbox_.active = key.active;
		previewPlayerAttackHitbox_.followPlayerMovement = key.followPlayerMovement;
		return;
	}

	if (!timeline.hitboxes.empty()) {
		const PlayerSpecialHitboxKeyframe* nearest = &timeline.hitboxes.front();
		for (const PlayerSpecialHitboxKeyframe& key : timeline.hitboxes) {
			if (std::abs(key.time - timelineTime_) < std::abs(nearest->time - timelineTime_)) {
				nearest = &key;
			}
		}

		previewPlayerAttackHitbox_.offset = nearest->offset;
		previewPlayerAttackHitbox_.halfSize = nearest->halfSize;
		previewPlayerAttackHitbox_.followPlayerMovement = nearest->followPlayerMovement;
	}

}

void ParticleTestScene::EvaluatePlayerSpecialVisualZ_()
{
	const auto& keys = CurrentPlayerSpecialTimeline_().visualZKeyframes;
	previewSpecialVisualZOffset_ = 0.0f;
	if (keys.empty()) return;
	if (timelineTime_ <= keys.front().time) {
		previewSpecialVisualZOffset_ = keys.front().offsetZ;
		return;
	}
	for (size_t i = 0; i + 1 < keys.size(); ++i) {
		const auto& a = keys[i];
		const auto& b = keys[i + 1];
		if (timelineTime_ < a.time || timelineTime_ > b.time) continue;
		const float duration = std::max(0.001f, b.time - a.time);
		float t = std::clamp((timelineTime_ - a.time) / duration, 0.0f, 1.0f);
		switch (a.interpolation) {
		case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn: t *= t; break;
		case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut: t = 1.0f - (1.0f - t) * (1.0f - t); break;
		case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut:
			t = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
			break;
		case ParticleTestEditor::PlayerSpecialPositionInterpolation::Step: t = 0.0f; break;
		default: break;
		}
		previewSpecialVisualZOffset_ = a.offsetZ + (b.offsetZ - a.offsetZ) * t;
		return;
	}
	previewSpecialVisualZOffset_ = keys.back().offsetZ;
}

void ParticleTestScene::EvaluatePlayerSpecialOpacity_()
{
	if (playerAttackObjectIndex_ < 0 || playerAttackObjectIndex_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}
	const auto& keys = CurrentPlayerSpecialTimeline_().opacityKeyframes;
	float alpha = 1.0f;
	float previousTime = 0.0f;
	float previousAlpha = 1.0f;
	for (const auto& key : keys) {
		if (timelineTime_ < key.time) {
			// Each destination key decides whether the value changes instantly on
			// arrival or fades from the previous key to this key.
			if (key.interpolation == ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step) {
				alpha = previousAlpha;
			} else {
				const float duration = std::max(0.001f, key.time - previousTime);
				const float t = std::clamp((timelineTime_ - previousTime) / duration, 0.0f, 1.0f);
				alpha = previousAlpha + (key.alpha - previousAlpha) * t;
			}
			break;
		}
		previousTime = key.time;
		previousAlpha = key.alpha;
		alpha = key.alpha;
	}
	EditorObject& player = editorObjects_[playerAttackObjectIndex_];
	player.color.w = std::clamp(alpha, 0.0f, 1.0f);
	ApplyEditorObjectTransform_(player);
}

void ParticleTestScene::EvaluatePlayerSpecialAnimation_()
{
	if (playerAttackObjectIndex_ < 0 || playerAttackObjectIndex_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}
	EditorObject& player = editorObjects_[playerAttackObjectIndex_];
	if (!player.object) return;

	const auto& keys = CurrentPlayerSpecialTimeline_().animations;
	const PlayerSpecialAnimationKeyframe* activeKey = nullptr;
	for (const auto& key : keys) {
		if (key.time <= timelineTime_) activeKey = &key;
		else break;
	}
	if (!activeKey || activeKey->animationName.empty()) return;

	const bool crossed = lastTimelineTime_ < activeKey->time && timelineTime_ >= activeKey->time;
	const bool differentAnimation = player.object->GetPlayingAnimName() != activeKey->animationName;
	if (!crossed && !differentAnimation) return;

	// A key at zero is the beginning of an attack and must restart even if the
	// same animation was left playing by the previous preview.
	if (activeKey->time <= 0.001f && crossed) {
		player.object->PlayAnimation(activeKey->animationName, activeKey->loop);
	} else {
		player.object->CrossFadeTo(activeKey->animationName, activeKey->blendSec, activeKey->loop);
	}
}

void ParticleTestScene::EvaluatePlayerSpecialRotation_()
{
	if (playerAttackObjectIndex_ < 0 || playerAttackObjectIndex_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}
	const auto& keys = CurrentPlayerSpecialTimeline_().rotationKeyframes;
	Vector3 rotation = playerSpecialPreviewBaseRotation_;
	float previousTime = 0.0f;
	Vector3 previousRotation = playerSpecialPreviewBaseRotation_;
	for (const auto& key : keys) {
		if (timelineTime_ < key.time) {
			if (key.interpolation == ParticleTestEditor::PlayerSpecialRotationInterpolation::Step) {
				rotation = previousRotation;
			} else {
				const float duration = std::max(0.001f, key.time - previousTime);
				const float t = std::clamp((timelineTime_ - previousTime) / duration, 0.0f, 1.0f);
				rotation = LerpVector3(previousRotation, key.rotation, t);
			}
			break;
		}
		previousTime = key.time;
		previousRotation = key.rotation;
		rotation = key.rotation;
	}

	EditorObject& player = editorObjects_[playerAttackObjectIndex_];
	player.rotation = rotation;
	ApplyEditorObjectTransform_(player);
}

void ParticleTestScene::TriggerPlayerSpecialEffects_()
{
	if (!timelinePlaying_) {
		return;
	}
	const auto& keys = CurrentPlayerSpecialTimeline_().effectKeyframes;
	Vector3 playerPosition = playerSpecialPreviewOrigin_ + previewSpecialPositionOffset_;
	for (const auto& key : keys) {
		const bool crossed = lastTimelineTime_ < key.time && timelineTime_ >= key.time;
		if (!crossed || key.jsonPath.empty()) {
			continue;
		}
		// Loaded attack effects are also restored as dope-sheet Effect Nodes.
		// Those nodes own preview emission, so do not play the same effect twice.
		const bool restoredAsNode = std::any_of(particleNodes_.begin(), particleNodes_.end(),
			[&key](const ParticleNode& node) {
				return node.isEffectNode && std::abs(node.startTime - key.time) < 0.001f &&
					node.particleFileName == key.jsonPath;
			});
		if (restoredAsNode) continue;
		auto* effectManager = EffectManager::GetInstance();
		if (!effectManager->HasEffect(key.name)) {
			effectManager->LoadEffect(key.name, key.jsonPath);
		}
		effectManager->Play(key.name, playerPosition + key.offset);
	}
}

void ParticleTestScene::EvaluatePlayerSpecialPosition_() {

	const PlayerSpecialTimeline& timeline =
		CurrentPlayerSpecialTimeline_();

	if (livePreviewSpecialEdit_ && !timelinePlaying_) {
		previewSpecialPositionOffset_ = ResolvePlayerSpecialPositionOffset_(currentSpecialPosition_);
		return;
	}

	//キーがない場合
	if(timeline.positionKeyframes.empty()){

		previewSpecialPositionOffset_ = { 0.0f,0.0f,0.0f };
		return;
	}

	//最初のキー以前
	if (timelineTime_ <= timeline.positionKeyframes.front().time) {
		previewSpecialPositionOffset_ =
			ResolvePlayerSpecialPositionOffset_(timeline.positionKeyframes.front());
		return;
	}

	// 2つのキーの間
	for (size_t i = 0;
		i + 1 < timeline.positionKeyframes.size();
		++i) {

		const PlayerSpecialPositionKeyframe& a =
			timeline.positionKeyframes[i];

		const PlayerSpecialPositionKeyframe& b =
			timeline.positionKeyframes[i + 1];

		if (timelineTime_ >= a.time &&
			timelineTime_ <= b.time) {

			const float duration =
				std::max(0.001f, b.time - a.time);

			float t =
				(timelineTime_ - a.time) / duration;

			t = std::clamp(t, 0.0f, 1.0f);
			switch (a.interpolation) {
			case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn:
				t = t * t;
				break;
			case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut:
				t = 1.0f - (1.0f - t) * (1.0f - t);
				break;
			case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut:
				t = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
				break;
			case ParticleTestEditor::PlayerSpecialPositionInterpolation::Step:
				t = t >= 1.0f ? 1.0f : 0.0f;
				break;
			case ParticleTestEditor::PlayerSpecialPositionInterpolation::Linear:
			default:
				break;
			}

			previewSpecialPositionOffset_ = LerpVector3(
				ResolvePlayerSpecialPositionOffset_(a),
				ResolvePlayerSpecialPositionOffset_(b), t);

			return;
		}
	}

	// 最後のキー以後
	if (timelineTime_ >= timeline.positionKeyframes.back().time) {
		previewSpecialPositionOffset_ =
			ResolvePlayerSpecialPositionOffset_(timeline.positionKeyframes.back());
		return;
	}


}

void ParticleTestScene::AddKeyframeToObject_(EditorObject& item)
{
	for (auto& key : item.keyframes) {
		if (std::abs(key.time - timelineTime_) < 0.001f) {
			key.position = item.position;
			key.rotation = item.rotation;
			key.scale = item.scale;
			key.color = item.color;
			key.bloomPostEffect = item.bloomPostEffect;
			key.outlineBloomPostEffect = item.outlineBloomPostEffect;
			key.bloomColor = item.bloomColor;
			key.outlineBloomColor = item.outlineBloomColor;
			key.bonePoses = item.bonePoses;
			key.vertexOffsets = item.vertexOffsets;
			return;
		}
	}

	item.keyframes.push_back({
		timelineTime_,
		item.position,
		item.rotation,
		item.scale,
		item.color,
		item.bloomPostEffect,
		item.outlineBloomPostEffect,
		item.bloomColor,
		item.outlineBloomColor,
		item.bonePoses,
		item.vertexOffsets
		});
	SortKeyframes_(item);
}

void ParticleTestScene::AddKeyframeToSelected_()
{
	if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}

	AddKeyframeToObject_(editorObjects_[selectedEditorObject_]);
}

Vector3 ParticleTestScene::ResolvePlayerSpecialPositionOffset_(
	const PlayerSpecialPositionKeyframe& key) const
{
	if (key.space != ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget) {
		return key.offset;
	}
	// The dummy position is its feet. Gameplay lock-on uses the body center.
	const Vector3 targetCenter{
		bossDummyPosition_.x,
		bossDummyPosition_.y + bossDummyHalfSize_.y,
		bossDummyPosition_.z
	};
	return targetCenter + key.offset - playerSpecialPreviewOrigin_;
}

void ParticleTestScene::CopySelectedModelKeyframe_()
{
	if (selectedKeyframeType_ != DragTarget::ModelKeyframe ||
		selectedKeyframeObjectIndex_ < 0 ||
		selectedKeyframeObjectIndex_ >= static_cast<int>(editorObjects_.size())) return;
	const auto& keys = editorObjects_[selectedKeyframeObjectIndex_].keyframes;
	if (selectedKeyframeIndex_ < 0 || selectedKeyframeIndex_ >= static_cast<int>(keys.size())) return;
	copiedModelKeyframe_ = keys[selectedKeyframeIndex_];
	hasCopiedModelKeyframe_ = true;
	hasCopiedObject_ = false;
}

void ParticleTestScene::PasteCopiedModelKeyframe_()
{
	if (!hasCopiedModelKeyframe_ || selectedEditorObject_ < 0 ||
		selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) return;
	auto& item = editorObjects_[selectedEditorObject_];
	EffectKeyframe pasted = copiedModelKeyframe_;
	pasted.time = timelineTime_;
	// Keep pasted keys individually selectable. If the playhead is exactly on
	// another key, place the new key one small step later instead of merging it.
	while (std::any_of(item.keyframes.begin(), item.keyframes.end(), [&](const EffectKeyframe& key) {
		return std::abs(key.time - pasted.time) < 0.001f;
		})) {
		pasted.time += 0.01f;
	}
	timelineDuration_ = std::max(timelineDuration_, pasted.time);
	item.keyframes.push_back(pasted);
	SortKeyframes_(item);
	selectedKeyframeIndex_ = 0;
	for (int i = 0; i < static_cast<int>(item.keyframes.size()); ++i) {
		if (std::abs(item.keyframes[i].time - pasted.time) < 0.001f) {
			selectedKeyframeIndex_ = i;
			break;
		}
	}
	selectedKeyframeType_ = DragTarget::ModelKeyframe;
	selectedKeyframeObjectIndex_ = selectedEditorObject_;
	EvaluateTimeline_(false);
}

void ParticleTestScene::DeleteNearestKeyframeFromSelected_()
{
	if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}

	auto& keys = editorObjects_[selectedEditorObject_].keyframes;
	if (keys.empty()) {
		return;
	}

	if (selectedKeyframeType_ == DragTarget::ModelKeyframe &&
		selectedKeyframeObjectIndex_ == selectedEditorObject_ &&
		selectedKeyframeIndex_ >= 0 &&
		selectedKeyframeIndex_ < static_cast<int>(keys.size())) {
		keys.erase(keys.begin() + selectedKeyframeIndex_);
		selectedKeyframeIndex_ = -1;
		selectedKeyframeType_ = DragTarget::None;
		selectedKeyframeObjectIndex_ = -1;
		EvaluateTimeline_(false);
		return;
	}

	auto it = std::min_element(keys.begin(), keys.end(), [this](const EffectKeyframe& a, const EffectKeyframe& b) {
		return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
		});
	if (it != keys.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
		int targetIdx = static_cast<int>(std::distance(keys.begin(), it));
		keys.erase(it);
		if (selectedKeyframeType_ == DragTarget::ModelKeyframe &&
			selectedKeyframeObjectIndex_ == selectedEditorObject_ &&
			selectedKeyframeIndex_ == targetIdx) {
			selectedKeyframeIndex_ = -1;
			selectedKeyframeType_ = DragTarget::None;
			selectedKeyframeObjectIndex_ = -1;
		}
		EvaluateTimeline_(false);
	}
}

void ParticleTestScene::EvaluateTimeline_(bool emitParticles)
{
	if (playerAttackEditorEnabled_) {
		EvaluatePlayerSpecialTimeline_();
	} else {
		EvaluatePlayerAttackHitbox_();
	}

	if (!cameraKeyframes_.empty()) {
		SortCameraKeyframes_();
		if (timelineTime_ <= cameraKeyframes_.front().time) {
			animationCameraPosition_ = cameraKeyframes_.front().position;
			animationCameraRotation_ = cameraKeyframes_.front().rotation;
			animationCameraFovY_ = cameraKeyframes_.front().fovY;
		} else if (timelineTime_ >= cameraKeyframes_.back().time) {
			animationCameraPosition_ = cameraKeyframes_.back().position;
			animationCameraRotation_ = cameraKeyframes_.back().rotation;
			animationCameraFovY_ = cameraKeyframes_.back().fovY;
		} else {
			for (size_t i = 0; i + 1 < cameraKeyframes_.size(); ++i) {
				const auto& a = cameraKeyframes_[i];
				const auto& b = cameraKeyframes_[i + 1];
				if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
					const float range = std::max(0.0001f, b.time - a.time);
					const float t = (timelineTime_ - a.time) / range;
					animationCameraPosition_ = LerpVector3(a.position, b.position, t);
					animationCameraRotation_ = LerpVector3(a.rotation, b.rotation, t);
					animationCameraFovY_ = a.fovY + (b.fovY - a.fovY) * t;
					break;
				}
			}
		}
		ApplyAnimationCamera_();
	}

	for (int itemIndex = 0; itemIndex < static_cast<int>(editorObjects_.size()); ++itemIndex) {
		auto& item = editorObjects_[itemIndex];
		// PlayerAttack movement is owned exclusively by positionKeyframes.
		// Generic EffectKeyframes must not overwrite the player's evaluated position.
		if (playerAttackEditorEnabled_ && itemIndex == playerAttackObjectIndex_) {
			continue;
		}
		if (item.keyframes.empty()) {
			item.active = true;
			ApplyEditorObjectTransform_(item);
			continue;
		}

		SortKeyframes_(item);
		if (timelineTime_ < item.keyframes.front().time) {
			item.active = false;
			continue;
		}
		item.active = true;

		if (timelineTime_ <= item.keyframes.front().time) {
			item.position = item.keyframes.front().position;
			item.rotation = item.keyframes.front().rotation;
			item.scale = item.keyframes.front().scale;
			item.color = item.keyframes.front().color;
			item.bloomPostEffect = item.keyframes.front().bloomPostEffect;
			item.outlineBloomPostEffect = item.keyframes.front().outlineBloomPostEffect;
			item.bloomColor = item.keyframes.front().bloomColor;
			item.outlineBloomColor = item.keyframes.front().outlineBloomColor;
			if (!item.keyframes.front().bonePoses.empty()) {
				item.bonePoses = item.keyframes.front().bonePoses;
				ApplyEditorObjectBonePose_(item);
			}
			item.vertexOffsets = item.keyframes.front().vertexOffsets;
			ApplyVertexOffsets_(item);
			ApplyEditorObjectTransform_(item);
			continue;
		}
		if (timelineTime_ >= item.keyframes.back().time) {
			item.position = item.keyframes.back().position;
			item.rotation = item.keyframes.back().rotation;
			item.scale = item.keyframes.back().scale;
			item.color = item.keyframes.back().color;
			item.bloomPostEffect = item.keyframes.back().bloomPostEffect;
			item.outlineBloomPostEffect = item.keyframes.back().outlineBloomPostEffect;
			item.bloomColor = item.keyframes.back().bloomColor;
			item.outlineBloomColor = item.keyframes.back().outlineBloomColor;
			if (!item.keyframes.back().bonePoses.empty()) {
				item.bonePoses = item.keyframes.back().bonePoses;
				ApplyEditorObjectBonePose_(item);
			}
			item.vertexOffsets = item.keyframes.back().vertexOffsets;
			ApplyVertexOffsets_(item);
			ApplyEditorObjectTransform_(item);
			continue;
		}

		for (size_t i = 0; i + 1 < item.keyframes.size(); ++i) {
			const auto& a = item.keyframes[i];
			const auto& b = item.keyframes[i + 1];
			if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
				const float range = std::max(0.001f, b.time - a.time);
				const float t = (timelineTime_ - a.time) / range;

				float easedT = t;
				if (a.interpolationType == 1) { // Ease In
					easedT = t * t;
				} else if (a.interpolationType == 2) { // Ease Out
					easedT = t * (2.0f - t);
				} else if (a.interpolationType == 3) { // Ease In Out
					easedT = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
				}

				item.position = LerpVector3(a.position, b.position, easedT);
				item.rotation = LerpVector3(a.rotation, b.rotation, easedT);
				item.scale = LerpVector3(a.scale, b.scale, easedT);
				item.color = LerpVector4(a.color, b.color, easedT);
				item.bloomPostEffect = a.bloomPostEffect;
				item.outlineBloomPostEffect = a.outlineBloomPostEffect;
				item.bloomColor = LerpVector4(a.bloomColor, b.bloomColor, easedT);
				item.outlineBloomColor = LerpVector4(a.outlineBloomColor, b.outlineBloomColor, easedT);
				if (!a.bonePoses.empty() && a.bonePoses.size() == b.bonePoses.size()) {
					item.bonePoses = a.bonePoses;
					for (size_t boneIndex = 0; boneIndex < item.bonePoses.size(); ++boneIndex) {
						item.bonePoses[boneIndex].translate = LerpVector3(a.bonePoses[boneIndex].translate, b.bonePoses[boneIndex].translate, easedT);
						item.bonePoses[boneIndex].rotate = LerpVector3(a.bonePoses[boneIndex].rotate, b.bonePoses[boneIndex].rotate, easedT);
						item.bonePoses[boneIndex].scale = LerpVector3(a.bonePoses[boneIndex].scale, b.bonePoses[boneIndex].scale, easedT);
					}
					ApplyEditorObjectBonePose_(item);
				}
				item.vertexOffsets = LerpVertexOffsets_(item, a, b, easedT);
				ApplyVertexOffsets_(item);
				ApplyEditorObjectTransform_(item);
				break;
			}
		}
	}

	if (!emitParticles) {
		return;
	}

	for (auto& node : particleNodes_) {
		const float nodeEndTime = node.startTime + GetParticleNodeDuration_(node);
		node.endTime = nodeEndTime;
		if (timelineTime_ < node.startTime) {
			node.hasEmitted = false;
			continue;
		}
		if (timelineTime_ > nodeEndTime) {
			node.hasEmitted = true;
			continue;
		}

		bool crossed = (lastTimelineTime_ < node.startTime && timelineTime_ >= node.startTime);

		if (crossed && !node.hasEmitted) {
			EmitParticleNode_(node, 0.0f);
			node.hasEmitted = true;
		}
	}
	SyncParticleModelsWithEditorObjects_();
}

float ParticleTestScene::GetParticleNodeDuration_(const ParticleNode& node) const
{
	return std::max(0.01f, node.presetDuration);
}

void ParticleTestScene::EmitParticleNode_(const ParticleNode& node, float initialAge)
{
	if (node.isEffectNode) {
		auto* effectManager = EffectManager::GetInstance();
		// Effect JSON can contain many vertex-edit keyframes. Parsing it for every
		// preview play caused a large hitch, so reuse the loaded template.
		if (!effectManager->HasEffect(node.name)) {
			effectManager->LoadEffect(node.name, node.particleFileName);
		}
		effectManager->Play(node.name, node.position, initialAge);
		return;
	}
	auto* pm = ParticleManager::GetInstance();
	std::vector<std::string> groupNames = pm->GetGroupNamesInFile(node.particleFileName);
	for (const auto& groupName : groupNames) {
		if (pm->HasGroup(groupName)) {
			pm->EmitConfigured(groupName, node.position, 1.0f, initialAge);
		}
	}
}

void ParticleTestScene::RequestTimelineRebuild_(float targetTime)
{
	targetTime = std::clamp(targetTime, 0.0f, timelineDuration_);
	timelineTime_ = targetTime;
	KeepTimelineTimeInView_();
	pendingTimelineRebuildTime_ = targetTime;
	pendingTimelineRebuild_ = true;
	EvaluateTimeline_(false);
}

void ParticleTestScene::RebuildParticleTimeline_(float targetTime)
{
	targetTime = std::clamp(targetTime, 0.0f, timelineDuration_);
	timelineTime_ = targetTime;

	auto* pm = ParticleManager::GetInstance();
	pm->ClearAllParticles();
	// Clear the previous preview before evaluation. PlayerAttack evaluation may
	// trigger an effect key at time 0, which must survive this rebuild.
	EffectManager::GetInstance()->ClearActiveEffects();

	EvaluateTimeline_(false);
	for (auto& node : particleNodes_) {
		const float duration = GetParticleNodeDuration_(node);
		node.endTime = node.startTime + duration;
		node.hasEmitted = false;

		if (targetTime < node.startTime || targetTime > node.endTime) {
			node.hasEmitted = targetTime > node.endTime;
			continue;
		}

		EmitParticleNode_(node, targetTime - node.startTime);
		node.hasEmitted = true;
	}

	lastTimelineTime_ = (targetTime == 0.0f) ? -0.001f : targetTime;
	previousTimelineTime_ = targetTime;
}

ParticleTestScene::EditorSnapshot ParticleTestScene::CaptureEditorSnapshot_() const
{
	EditorSnapshot snapshot;
	snapshot.selectedObject = selectedEditorObject_;
	snapshot.nextObjectId = nextEditorObjectId_;
	snapshot.timelineTime = timelineTime_;
	snapshot.timelineDuration = timelineDuration_;
	snapshot.timelineLoop = timelineLoop_;
	snapshot.animationCameraPosition = animationCameraPosition_;
	snapshot.animationCameraRotation = animationCameraRotation_;
	snapshot.animationCameraFovY = animationCameraFovY_;
	snapshot.useAnimationCameraPreview = useAnimationCameraPreview_;
	snapshot.animationCameraPreviewSwapped = animationCameraPreviewSwapped_;
	snapshot.cameraKeyframes = cameraKeyframes_;
	snapshot.particleNodes = particleNodes_;
	snapshot.selectedParticleNode = selectedParticleNode_;
	snapshot.objects.reserve(editorObjects_.size());
	for (const auto& item : editorObjects_) {
		EditorObjectSnapshot object;
		object.id = item.id;
		object.name = item.name;
		object.modelPath = item.modelPath;
		object.geometryType = item.geometryType;
		object.position = item.position;
		object.rotation = item.rotation;
		object.scale = item.scale;
		object.color = item.color;
		object.texturePath = item.texturePath;
		object.blendMode = item.blendMode;
		object.billboard = item.billboard;
		object.bloomPostEffect = item.bloomPostEffect;
		object.outlineBloomPostEffect = item.outlineBloomPostEffect;
		object.bloomColor = item.bloomColor;
		object.outlineBloomColor = item.outlineBloomColor;
		object.showBones = item.showBones;
		object.selectedBone = item.selectedBone;
		object.bonePoses = item.bonePoses;
		object.attachToBone = item.attachToBone;
		object.attachParentId = item.attachParentId;
		object.attachJointName = item.attachJointName;
		object.attachOffset = item.attachOffset;
		object.attachRotation = item.attachRotation;
		object.attachScale = item.attachScale;
		object.keyframes = item.keyframes;
		object.selectedVertexIndex = item.selectedVertexIndex;
		object.selectedVertexIndices = item.selectedVertexIndices;
		object.vertexRangeStart = item.vertexRangeStart;
		object.vertexRangeEnd = item.vertexRangeEnd;
		object.vertexSelectionOffset = item.vertexSelectionOffset;
		object.vertexOffsets = item.vertexOffsets;
		object.selected = item.selected;
		object.visible = item.visible;
		snapshot.objects.push_back(std::move(object));
	}
	return snapshot;
}

void ParticleTestScene::RestoreEditorSnapshot_(GameApp& app, const EditorSnapshot& snapshot)
{
	editorObjects_.clear();
	editorObjects_.reserve(snapshot.objects.size());
	for (const auto& src : snapshot.objects) {
		EditorObject item;
		item.id = src.id;
		item.name = src.name;
		item.modelPath = src.modelPath;
		item.geometryType = src.geometryType;
		item.position = src.position;
		item.rotation = src.rotation;
		item.scale = src.scale;
		item.color = src.color;
		item.texturePath = src.texturePath;
		item.blendMode = src.blendMode;
		item.billboard = src.billboard;
		item.bloomPostEffect = src.bloomPostEffect;
		item.outlineBloomPostEffect = src.outlineBloomPostEffect;
		item.bloomColor = src.bloomColor;
		item.outlineBloomColor = src.outlineBloomColor;
		item.showBones = src.showBones;
		item.selectedBone = src.selectedBone;
		item.bonePoses = src.bonePoses;
		item.attachToBone = src.attachToBone;
		item.attachParentId = src.attachParentId;
		item.attachJointName = src.attachJointName;
		item.attachOffset = src.attachOffset;
		item.attachRotation = src.attachRotation;
		item.attachScale = src.attachScale;
		item.keyframes = src.keyframes;
		item.selectedVertexIndex = src.selectedVertexIndex;
		item.selectedVertexIndices = src.selectedVertexIndices;
		item.vertexRangeStart = src.vertexRangeStart;
		item.vertexRangeEnd = src.vertexRangeEnd;
		item.vertexSelectionOffset = src.vertexSelectionOffset;
		item.vertexOffsets = src.vertexOffsets;
		item.selected = src.selected;
		item.visible = src.visible;
		item.object = std::make_unique<Object3d>();
		item.object->Initialize(app.ObjCom(), app.Dx());
		item.object->SetIsVisible(item.visible);
		item.object->SetCamera(GetSceneCamera_());
		if (item.geometryType >= 0) {
			item.object->SetModel(GetOrCreateEditorGeometryModel(item.geometryType));
		} else {
			item.object->SetModel(item.modelPath);
		}
		item.object->SetEnableLighting(0);

		if (!item.vertexOffsets.empty()) {
			EnsureUniqueModelForObject_(item);
		}

		ApplyEditorObjectTransform_(item);
		editorObjects_.push_back(std::move(item));
	}
	selectedEditorObject_ = snapshot.selectedObject;
	if (selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
		selectedEditorObject_ = editorObjects_.empty() ? -1 : static_cast<int>(editorObjects_.size()) - 1;
	}
	nextEditorObjectId_ = snapshot.nextObjectId;
	timelineTime_ = snapshot.timelineTime;
	timelineDuration_ = std::max(0.05f, snapshot.timelineDuration);
	timelineViewDuration_ = std::clamp(timelineViewDuration_ <= 0.0f ? timelineDuration_ : timelineViewDuration_, std::min(0.05f, timelineDuration_), timelineDuration_);
	timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
	timelineLoop_ = snapshot.timelineLoop;
	animationCameraPosition_ = snapshot.animationCameraPosition;
	animationCameraRotation_ = snapshot.animationCameraRotation;
	animationCameraFovY_ = snapshot.animationCameraFovY;
	useAnimationCameraPreview_ = snapshot.useAnimationCameraPreview;
	animationCameraPreviewSwapped_ = snapshot.animationCameraPreviewSwapped;
	cameraKeyframes_ = snapshot.cameraKeyframes;
	particleNodes_ = snapshot.particleNodes;
	selectedParticleNode_ = snapshot.selectedParticleNode;

	// Blenderモード中のスナップショット復元の場合は、パーティクルグループも同期する
	if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
		auto* pm = ParticleManager::GetInstance();
		pm->ClearGroups();
		for (auto& node : particleNodes_) {
			if (node.isEffectNode) {
				EffectManager::GetInstance()->LoadEffect(node.name, node.particleFileName);
			} else {
				pm->LoadAdditional(node.particleFileName, "");
			}
			node.hasEmitted = false;
		}
	}

	ApplyAnimationCamera_();
	ApplyCameraToEditorObjects_();
	EvaluateTimeline_();
}

void ParticleTestScene::PushUndoSnapshot_(const EditorSnapshot& snapshot)
{
	undoStack_.push_back(snapshot);
	while (undoStack_.size() > kMaxUndoCount) {
		undoStack_.pop_front();
	}
	redoStack_.clear();
}

void ParticleTestScene::PushUndoSnapshot_()
{
	PushUndoSnapshot_(CaptureEditorSnapshot_());
}

void ParticleTestScene::Undo_(GameApp& app)
{
	if (undoStack_.empty()) {
		return;
	}
	redoStack_.push_back(CaptureEditorSnapshot_());
	EditorSnapshot snapshot = undoStack_.back();
	undoStack_.pop_back();
	RestoreEditorSnapshot_(app, snapshot);
}

void ParticleTestScene::Redo_(GameApp& app)
{
	if (redoStack_.empty()) {
		return;
	}
	undoStack_.push_back(CaptureEditorSnapshot_());
	EditorSnapshot snapshot = redoStack_.back();
	redoStack_.pop_back();
	RestoreEditorSnapshot_(app, snapshot);
}

void ParticleTestScene::KeepTimelineTimeInView_()
{
	const float margin = timelineViewDuration_ * 0.05f; // 端の5%をバッファとする
	if (timelineTime_ > timelineViewStart_ + timelineViewDuration_ - margin) {
		timelineViewStart_ = timelineTime_ - (timelineViewDuration_ - margin);
	} else if (timelineTime_ < timelineViewStart_ + margin) {
		timelineViewStart_ = timelineTime_ - margin;
	}
	timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
}

