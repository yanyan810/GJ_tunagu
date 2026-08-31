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
#include <Windows.h>

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

void ParticleTestScene::DrawPlayerSpecialPathPreview_()
{
#ifdef USE_IMGUI
	if (!drawPlayerSpecialPath_ || !playerAttackEditorEnabled_ || !gHasSceneImageRect) {
		return;
	}
	Camera* sceneCamera = GetSceneCamera_();
	const auto& keys = CurrentPlayerSpecialTimeline_().positionKeyframes;
	if (!sceneCamera || keys.empty()) {
		return;
	}
	const ImVec2 sceneMin = gSceneImageMin;
	const ImVec2 sceneMax = gSceneImageMax;
	const float sceneW = std::max(1.0f, sceneMax.x - sceneMin.x);
	const float sceneH = std::max(1.0f, sceneMax.y - sceneMin.y);
	auto project = [&](const Vector3& world, ImVec2& out) {
		const Matrix4x4& vp = sceneCamera->GetViewProjectionMatrix();
		const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
		const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
		const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
		if (w <= 0.001f) return false;
		out = { sceneMin.x + (x / w * 0.5f + 0.5f) * sceneW, sceneMin.y + (0.5f - y / w * 0.5f) * sceneH };
		return true;
	};
	std::vector<ImVec2> points(keys.size());
	std::vector<bool> visible(keys.size(), false);
	for (size_t i = 0; i < keys.size(); ++i) {
		visible[i] = project(playerSpecialPreviewOrigin_ + ResolvePlayerSpecialPositionOffset_(keys[i]), points[i]);
	}
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	draw->PushClipRect(sceneMin, sceneMax, true);
	for (size_t i = 1; i < points.size(); ++i) {
		if (visible[i - 1] && visible[i]) draw->AddLine(points[i - 1], points[i], IM_COL32(80, 220, 255, 230), 2.5f);
	}
	for (size_t i = 0; i < points.size(); ++i) {
		if (!visible[i]) continue;
		const bool nearTime = std::abs(keys[i].time - timelineTime_) <= 0.02f;
		draw->AddCircleFilled(points[i], nearTime ? 7.0f : 5.0f, nearTime ? IM_COL32(255, 210, 60, 255) : IM_COL32(80, 220, 255, 255), 16);
		const auto& opacityKeys = CurrentPlayerSpecialTimeline_().opacityKeyframes;
		const auto opacityIt = std::find_if(opacityKeys.begin(), opacityKeys.end(), [&](const auto& opacityKey) {
			return std::abs(opacityKey.time - keys[i].time) < 0.001f;
		});
		const bool hasOpacityAtPoint = opacityIt != opacityKeys.end();
		char label[64]{};
		const char* baseLabel = keys[i].space == ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget ? " [Boss]" : "";
		if (i == 0) sprintf_s(label, "Start %.2fs%s%s", keys[i].time, baseLabel, hasOpacityAtPoint ? " [Alpha]" : "");
		else if (i + 1 == keys.size()) sprintf_s(label, "End %.2fs%s%s", keys[i].time, baseLabel, hasOpacityAtPoint ? " [Alpha]" : "");
		else sprintf_s(label, "Point %zu %.2fs%s%s", i, keys[i].time, baseLabel, hasOpacityAtPoint ? " [Alpha]" : "");
		draw->AddText({ points[i].x + 7.0f, points[i].y - 14.0f }, IM_COL32(255, 255, 255, 240), label);
	}
	draw->PopClipRect();

	const ImVec2 mouse = ImGui::GetMousePos();
	const bool mouseInside = mouse.x >= sceneMin.x && mouse.x <= sceneMax.x && mouse.y >= sceneMin.y && mouse.y <= sceneMax.y;
	if (mouseInside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && draggedPlayerSpecialPositionKey_ < 0) {
		float nearestDistance = 14.0f;
		for (int i = 0; i < static_cast<int>(points.size()); ++i) {
			if (!visible[i]) continue;
			const float dx = mouse.x - points[i].x;
			const float dy = mouse.y - points[i].y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			if (distance < nearestDistance) {
				nearestDistance = distance;
				draggedPlayerSpecialPositionKey_ = i;
				selectedPlayerSpecialPositionKey_ = i;
				currentSpecialPosition_ = keys[i];
			}
		}
		if (draggedPlayerSpecialPositionKey_ >= 0) {
			currentSpecialOpacity_.time = keys[draggedPlayerSpecialPositionKey_].time;
			currentSpecialOpacity_.alpha = 1.0f;
			currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Linear;
			const auto& opacityKeys = CurrentPlayerSpecialTimeline_().opacityKeyframes;
			const auto opacityIt = std::find_if(opacityKeys.begin(), opacityKeys.end(), [&](const auto& opacityKey) {
				return std::abs(opacityKey.time - currentSpecialOpacity_.time) < 0.001f;
			});
			if (opacityIt != opacityKeys.end()) currentSpecialOpacity_ = *opacityIt;
		}
	}
	if (draggedPlayerSpecialPositionKey_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		auto& mutableKeys = CurrentPlayerSpecialTimeline_().positionKeyframes;
		if (draggedPlayerSpecialPositionKey_ < static_cast<int>(mutableKeys.size())) {
			const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
			const float distanceToCamera = std::max(1.0f, std::abs(mutableKeys[draggedPlayerSpecialPositionKey_].offset.z - sceneCamera->GetTranslate().z));
			const float worldPerPixel = distanceToCamera * 0.0019f;
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			mutableKeys[draggedPlayerSpecialPositionKey_].offset += CameraRight(cameraWorld) * (delta.x * worldPerPixel);
			mutableKeys[draggedPlayerSpecialPositionKey_].offset += CameraUp(cameraWorld) * (-delta.y * worldPerPixel);
			currentSpecialPosition_ = mutableKeys[draggedPlayerSpecialPositionKey_];
		}
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		draggedPlayerSpecialPositionKey_ = -1;
	}
#endif
}

void ParticleTestScene::FocusPlayerSpecialPathCamera_()
{
	if (!camera_) return;
	const auto& keys = CurrentPlayerSpecialTimeline_().positionKeyframes;
	Vector3 minPoint = playerSpecialPreviewOrigin_;
	Vector3 maxPoint = playerSpecialPreviewOrigin_;
	if (!keys.empty()) {
		minPoint = playerSpecialPreviewOrigin_ + ResolvePlayerSpecialPositionOffset_(keys.front());
		maxPoint = minPoint;
		for (const auto& key : keys) {
			const Vector3 p = playerSpecialPreviewOrigin_ + ResolvePlayerSpecialPositionOffset_(key);
			minPoint.x = std::min(minPoint.x, p.x); minPoint.y = std::min(minPoint.y, p.y); minPoint.z = std::min(minPoint.z, p.z);
			maxPoint.x = std::max(maxPoint.x, p.x); maxPoint.y = std::max(maxPoint.y, p.y); maxPoint.z = std::max(maxPoint.z, p.z);
		}
	}
	const Vector3 center{ (minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f, (minPoint.z + maxPoint.z) * 0.5f };
	const float span = std::max({ maxPoint.x - minPoint.x, maxPoint.y - minPoint.y, maxPoint.z - minPoint.z, 2.0f });
	editorCameraPosition_ = { center.x, center.y + span * 0.15f, center.z - (span * 2.2f + 6.0f) };
	editorCameraRotation_ = { 0.0f, 0.0f, 0.0f };
	camera_->SetTranslate(editorCameraPosition_);
	camera_->SetRotate(editorCameraRotation_);
	camera_->Update();
}
void ParticleTestScene::DrawGizmoControls_(EditorObject& item)
{
#ifdef USE_IMGUI
	ImGui::Separator();
	ImGui::TextUnformatted("Gizmo");
	int mode = static_cast<int>(gizmoMode_);
	if (ImGui::RadioButton("Translate", mode == 0)) {
		gizmoMode_ = GizmoMode::Translate;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mode == 1)) {
		gizmoMode_ = GizmoMode::Rotate;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mode == 2)) {
		gizmoMode_ = GizmoMode::Scale;
	}

	auto dragAxis = [&](const char* label, float* value, float speed, float minValue, float maxValue, ImVec4 color) -> bool {
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		bool changed = ImGui::DragFloat(label, value, speed, minValue, maxValue, "%.3f");
		ImGui::PopStyleColor();
		return changed;
		};

	bool changed = false;
	auto trackGizmoDrag = [&](bool itemChanged) {
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (itemChanged) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}
		};

	if (gizmoMode_ == GizmoMode::Translate) {
		bool x = dragAxis("X Position", &item.position.x, 0.05f, -100.0f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
		bool y = dragAxis("Y Position", &item.position.y, 0.05f, -100.0f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
		bool z = dragAxis("Z Position", &item.position.z, 0.05f, -100.0f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
	} else if (gizmoMode_ == GizmoMode::Rotate) {
		bool x = dragAxis("X Rotation", &item.rotation.x, 0.01f, -100.0f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
		bool y = dragAxis("Y Rotation", &item.rotation.y, 0.01f, -100.0f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
		bool z = dragAxis("Z Rotation", &item.rotation.z, 0.01f, -100.0f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
	} else {
		bool x = dragAxis("X Scale", &item.scale.x, 0.05f, 0.01f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
		bool y = dragAxis("Y Scale", &item.scale.y, 0.05f, 0.01f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
		bool z = dragAxis("Z Scale", &item.scale.z, 0.05f, 0.01f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
	}
	if (changed) {
		ApplyEditorObjectTransform_(item);
	}
#endif
}

void ParticleTestScene::DrawBoneControls_(EditorObject& item)
{
#ifdef USE_IMGUI
	if (!item.object || !item.object->HasSkinningModel()) {
		return;
	}

	SyncEditorObjectBones_(item);
	if (item.bonePoses.empty()) {
		return;
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Bone Controls");
	ImGui::Checkbox("Show Bones", &item.showBones);
	item.object->SetDebugDrawBones(false);
	ImGui::Text("Bones: %d", static_cast<int>(item.bonePoses.size()));

	item.selectedBone = std::clamp(item.selectedBone, 0, static_cast<int>(item.bonePoses.size()) - 1);
	const char* previewName = item.bonePoses[item.selectedBone].name.c_str();
	if (ImGui::BeginCombo("Bone", previewName)) {
		for (int i = 0; i < static_cast<int>(item.bonePoses.size()); ++i) {
			const bool selected = i == item.selectedBone;
			if (ImGui::Selectable(item.bonePoses[i].name.c_str(), selected)) {
				item.selectedBone = i;
				item.showBones = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	EditorBonePose& pose = item.bonePoses[item.selectedBone];
	bool changed = false;
	auto trackBoneDrag = [&](bool itemChanged) {
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (itemChanged) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}
		};

	bool t = ImGui::DragFloat3("Bone Translate", &pose.translate.x, 0.01f, -10.0f, 10.0f);
	changed |= t;
	trackBoneDrag(t);
	bool r = ImGui::DragFloat3("Bone Rotate", &pose.rotate.x, 0.01f, -100.0f, 100.0f);
	changed |= r;
	trackBoneDrag(r);
	bool s = ImGui::DragFloat3("Bone Scale", &pose.scale.x, 0.01f, 0.01f, 10.0f);
	changed |= s;
	trackBoneDrag(s);

	if (ImGui::Button("Reset Bone")) {
		transformDragBefore_ = CaptureEditorSnapshot_();
		pose.translate = { 0.0f, 0.0f, 0.0f };
		pose.rotate = { 0.0f, 0.0f, 0.0f };
		pose.scale = { 1.0f, 1.0f, 1.0f };
		PushUndoSnapshot_(transformDragBefore_);
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset All Bones")) {
		transformDragBefore_ = CaptureEditorSnapshot_();
		for (auto& bonePose : item.bonePoses) {
			bonePose.translate = { 0.0f, 0.0f, 0.0f };
			bonePose.rotate = { 0.0f, 0.0f, 0.0f };
			bonePose.scale = { 1.0f, 1.0f, 1.0f };
		}
		PushUndoSnapshot_(transformDragBefore_);
		changed = true;
	}

	if (changed) {
		ApplyEditorObjectBonePose_(item);
	}
#endif
}

void ParticleTestScene::DrawViewportBones_()
{
#ifdef USE_IMGUI
	Camera* sceneCamera = GetSceneCamera_();
	if (!gHasSceneImageRect || !sceneCamera) {
		return;
	}
	if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
		return;
	}
	if (editorMode_ == EditorMode::PlayerAttack && selectedEditorObject_ == playerAttackObjectIndex_ && !timelinePlaying_) {
		return;
	}
	EditorObject& item = editorObjects_[selectedEditorObject_];
	if (!item.showBones || !item.object || !item.object->HasSkinningModel()) {
		return;
	}

	SyncEditorObjectBones_(item);
	const Model::Skeleton* skeleton = item.object->GetSkeleton();
	if (!skeleton || skeleton->joints.empty()) {
		return;
	}

	const ImVec2 sceneMin = gSceneImageMin;
	const ImVec2 sceneMax = gSceneImageMax;
	const float sceneW = std::max(1.0f, sceneMax.x - sceneMin.x);
	const float sceneH = std::max(1.0f, sceneMax.y - sceneMin.y);

	auto project = [&](const Vector3& world, ImVec2& out) -> bool {
		const Matrix4x4& vp = sceneCamera->GetViewProjectionMatrix();
		const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
		const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
		const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
		if (w <= 0.001f) {
			return false;
		}
		const float ndcX = x / w;
		const float ndcY = y / w;
		out.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneW;
		out.y = sceneMin.y + (0.5f - ndcY * 0.5f) * sceneH;
		return out.x >= sceneMin.x - 80.0f && out.x <= sceneMax.x + 80.0f &&
			out.y >= sceneMin.y - 80.0f && out.y <= sceneMax.y + 80.0f;
		};

	std::vector<Vector3> worldPositions(skeleton->joints.size());
	std::vector<ImVec2> screenPositions(skeleton->joints.size());
	std::vector<bool> visible(skeleton->joints.size(), false);
	for (size_t i = 0; i < skeleton->joints.size(); ++i) {
		Matrix4x4 jointWorld{};
		if (!item.object->TryGetJointWorldMatrix(skeleton->joints[i].name, jointWorld)) {
			continue;
		}
		worldPositions[i] = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
		visible[i] = project(worldPositions[i], screenPositions[i]);
	}

	const ImVec2 mouse = ImGui::GetMousePos();
	const bool mouseInsideScene =
		mouse.x >= sceneMin.x && mouse.x <= sceneMax.x &&
		mouse.y >= sceneMin.y && mouse.y <= sceneMax.y;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0 && !viewportBoneDragActive_) {
		int nearestBone = -1;
		float nearestDistance = 9999.0f;
		for (int i = 0; i < static_cast<int>(screenPositions.size()); ++i) {
			if (!visible[i]) {
				continue;
			}
			const float dx = mouse.x - screenPositions[i].x;
			const float dy = mouse.y - screenPositions[i].y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearestBone = i;
			}
		}

		if (nearestBone >= 0 && nearestDistance <= 10.0f) {
			item.selectedBone = nearestBone;
			activeViewportBone_ = nearestBone;
			viewportBoneLastMouseX_ = mouse.x;
			viewportBoneLastMouseY_ = mouse.y;
			transformDragBefore_ = CaptureEditorSnapshot_();
			viewportBoneDragActive_ = true;
			viewportBoneDragChanged_ = false;
		}
	}

	if (viewportBoneDragActive_ && activeViewportBone_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		const ImVec2 delta{ mouse.x - viewportBoneLastMouseX_, mouse.y - viewportBoneLastMouseY_ };
		viewportBoneLastMouseX_ = mouse.x;
		viewportBoneLastMouseY_ = mouse.y;

		if (activeViewportBone_ < static_cast<int>(item.bonePoses.size())) {
			const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
			const float amountX = delta.x / 55.0f;
			const float amountY = -delta.y / 55.0f;
			item.bonePoses[activeViewportBone_].translate += CameraRight(cameraWorld) * amountX;
			item.bonePoses[activeViewportBone_].translate += CameraUp(cameraWorld) * amountY;
			ApplyEditorObjectBonePose_(item);
			viewportBoneDragChanged_ = true;
		}
	}

	if (viewportBoneDragActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		if (viewportBoneDragChanged_) {
			PushUndoSnapshot_(transformDragBefore_);
		}
		viewportBoneDragActive_ = false;
		viewportBoneDragChanged_ = false;
		activeViewportBone_ = -1;
	}

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	const ImU32 lineColor = IM_COL32(255, 220, 80, 230);
	const ImU32 jointColor = IM_COL32(255, 255, 255, 245);
	const ImU32 selectedColor = IM_COL32(80, 170, 255, 255);

	for (size_t i = 0; i < skeleton->joints.size(); ++i) {
		if (!visible[i] || !skeleton->joints[i].parent.has_value()) {
			continue;
		}
		const int32_t parentIndex = *skeleton->joints[i].parent;
		if (parentIndex < 0 || parentIndex >= static_cast<int32_t>(screenPositions.size()) || !visible[parentIndex]) {
			continue;
		}
		drawList->AddLine(screenPositions[parentIndex], screenPositions[i], lineColor, 2.0f);
	}

	for (size_t i = 0; i < screenPositions.size(); ++i) {
		if (!visible[i]) {
			continue;
		}
		const bool selected = static_cast<int>(i) == item.selectedBone;
		drawList->AddCircleFilled(screenPositions[i], selected ? 5.5f : 3.5f, selected ? selectedColor : jointColor, 16);
		drawList->AddCircle(screenPositions[i], selected ? 7.0f : 5.0f, IM_COL32(30, 30, 30, 220), 16, 1.0f);
	}
#endif
}

void ParticleTestScene::DrawViewportGizmo_(GameApp& app)
{
#ifdef USE_IMGUI
	Camera* sceneCamera = GetSceneCamera_();
	if (!gHasSceneImageRect || !sceneCamera) {
		return;
	}
	if (editorMode_ == EditorMode::PlayerAttack && selectedEditorObject_ == playerAttackObjectIndex_ && !timelinePlaying_) {
		return;
	}

	const ImVec2 sceneMin = gSceneImageMin;
	const ImVec2 sceneMax = gSceneImageMax;
	const float sceneW = std::max(1.0f, sceneMax.x - sceneMin.x);
	const float sceneH = std::max(1.0f, sceneMax.y - sceneMin.y);

	auto project = [&](const Vector3& world, ImVec2& out) -> bool {
		const Matrix4x4& vp = sceneCamera->GetViewProjectionMatrix();
		const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
		const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
		const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
		if (w <= 0.001f) {
			return false;
		}
		const float ndcX = x / w;
		const float ndcY = y / w;
		out.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneW;
		out.y = sceneMin.y + (0.5f - ndcY * 0.5f) * sceneH;
		return out.x >= sceneMin.x - 80.0f && out.x <= sceneMax.x + 80.0f &&
			out.y >= sceneMin.y - 80.0f && out.y <= sceneMax.y + 80.0f;
		};

	const ImVec2 mouse = ImGui::GetMousePos();
	const bool mouseInsideScene =
		mouse.x >= sceneMin.x && mouse.x <= sceneMax.x &&
		mouse.y >= sceneMin.y && mouse.y <= sceneMax.y;

	bool hasSelection = (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size()));
	bool clickedGizmo = false;

	if (hasSelection) {
		EditorObject& item = editorObjects_[selectedEditorObject_];
		Vector3 gizmoWorldPosition = item.position;
		bool editingBone = false;
		EditorBonePose* selectedBonePose = nullptr;
		bool editingVertex = false;

		if (item.editMode && item.object && item.object->GetModel()) {
			Model* model = item.object->GetModel();
			if (model) {
				auto editorVertexPosition = [&](uint32_t index) {
					auto offset = item.vertexOffsets.find(index);
					return offset != item.vertexOffsets.end()
						? offset->second
						: model->GetSourceVertexPosition(index);
					};
				auto getVertexWorldMatrix = [&](Model* m, uint32_t vertexIdx, const Matrix4x4& baseWorld) -> Matrix4x4 {
					if (!m) return baseWorld;
					int meshIndex = -1;
					const auto& meshes = m->GetModelData().meshes;
					for (size_t meshK = 0; meshK < meshes.size(); ++meshK) {
						const auto& mesh = meshes[meshK];
						if (vertexIdx >= mesh.startVertex && vertexIdx < mesh.startVertex + mesh.vertices.size()) {
							meshIndex = static_cast<int>(meshK);
							break;
						}
					}
					if (meshIndex < 0) return baseWorld;
					int32_t nodeIndex = m->GetMeshOwnerNodeIndex(static_cast<uint32_t>(meshIndex));
					if (nodeIndex < 0) return baseWorld;
					Matrix4x4 nodeWorld = m->GetNodeWorldMatrix(nodeIndex);
					return Matrix4x4::Multiply(nodeWorld, baseWorld);
					};
				if (!item.selectedVertexIndices.empty()) {
					Vector3 sumWorldPos{ 0.0f, 0.0f, 0.0f };
					int validCount = 0;
					Matrix4x4 worldMat = item.object->CalculateWorldMatrix();
					for (int idx : item.selectedVertexIndices) {
						if (idx >= 0 && idx < static_cast<int>(model->GetSourceVertexCount())) {
							Vector3 localPos = editorVertexPosition(static_cast<uint32_t>(idx));
							Matrix4x4 actualWorld = getVertexWorldMatrix(model, static_cast<uint32_t>(idx), worldMat);
							Vector3 worldPos = {
								localPos.x * actualWorld.m[0][0] + localPos.y * actualWorld.m[1][0] + localPos.z * actualWorld.m[2][0] + actualWorld.m[3][0],
								localPos.x * actualWorld.m[0][1] + localPos.y * actualWorld.m[1][1] + localPos.z * actualWorld.m[2][1] + actualWorld.m[3][1],
								localPos.x * actualWorld.m[0][2] + localPos.y * actualWorld.m[1][2] + localPos.z * actualWorld.m[2][2] + actualWorld.m[3][2],
							};
							sumWorldPos += worldPos;
							validCount++;
						}
					}
					if (validCount > 0) {
						gizmoWorldPosition = sumWorldPos * (1.0f / (float)validCount);
						editingVertex = true;
					}
				} else if (item.selectedVertexIndex >= 0 && item.selectedVertexIndex < static_cast<int>(model->GetSourceVertexCount())) {
					Vector3 localPos = editorVertexPosition(static_cast<uint32_t>(item.selectedVertexIndex));
					Matrix4x4 worldMat = item.object->CalculateWorldMatrix();
					Matrix4x4 actualWorld = getVertexWorldMatrix(model, static_cast<uint32_t>(item.selectedVertexIndex), worldMat);
					gizmoWorldPosition = {
						localPos.x * actualWorld.m[0][0] + localPos.y * actualWorld.m[1][0] + localPos.z * actualWorld.m[2][0] + actualWorld.m[3][0],
						localPos.x * actualWorld.m[0][1] + localPos.y * actualWorld.m[1][1] + localPos.z * actualWorld.m[2][1] + actualWorld.m[3][1],
						localPos.x * actualWorld.m[0][2] + localPos.y * actualWorld.m[1][2] + localPos.z * actualWorld.m[2][2] + actualWorld.m[3][2],
					};
					editingVertex = true;
				}
			}
		} else if (item.showBones && item.object && item.object->HasSkinningModel()) {
			SyncEditorObjectBones_(item);
			if (!item.bonePoses.empty()) {
				item.selectedBone = std::clamp(item.selectedBone, 0, static_cast<int>(item.bonePoses.size()) - 1);
				const Model::Skeleton* skeleton = item.object->GetSkeleton();
				if (skeleton && item.selectedBone < static_cast<int>(skeleton->joints.size())) {
					Matrix4x4 jointWorld{};
					if (item.object->TryGetJointWorldMatrix(skeleton->joints[item.selectedBone].name, jointWorld)) {
						gizmoWorldPosition = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
						selectedBonePose = &item.bonePoses[item.selectedBone];
						editingBone = selectedBonePose != nullptr;
					}
				}
			}
		}

		ImVec2 center{};
		if (project(gizmoWorldPosition, center)) {
			// --- 頂点ドットの描画とクリック選択判定（Edit Mode の場合） ---
			if (item.editMode && item.object && item.object->GetModel()) {
				Model* model = item.object->GetModel();
				if (model) {
					auto editorVertexPosition = [&](uint32_t index) {
						auto offset = item.vertexOffsets.find(index);
						return offset != item.vertexOffsets.end()
							? offset->second
							: model->GetSourceVertexPosition(index);
						};
					auto getVertexWorldMatrix = [&](Model* m, uint32_t vertexIdx, const Matrix4x4& baseWorld) -> Matrix4x4 {
						if (!m) return baseWorld;
						int meshIndex = -1;
						const auto& meshes = m->GetModelData().meshes;
						for (size_t meshK = 0; meshK < meshes.size(); ++meshK) {
							const auto& mesh = meshes[meshK];
							if (vertexIdx >= mesh.startVertex && vertexIdx < mesh.startVertex + mesh.vertices.size()) {
								meshIndex = static_cast<int>(meshK);
								break;
							}
						}
						if (meshIndex < 0) return baseWorld;
						int32_t nodeIndex = m->GetMeshOwnerNodeIndex(static_cast<uint32_t>(meshIndex));
						if (nodeIndex < 0) return baseWorld;
						Matrix4x4 nodeWorld = m->GetNodeWorldMatrix(nodeIndex);
						return Matrix4x4::Multiply(nodeWorld, baseWorld);
						};
					// vertexCount fields are also used as GPU draw metadata and can be
					// narrower than the retained CPU vertex array. Edit mode must enumerate
					// the complete source array (e.g. all sphere stacks, not only the top one).
					uint32_t vertexCount = model->GetSourceVertexCount();
					// Use the exact matrix sent to the renderer. It already includes the
					// model-root transform when required and camera-facing billboard rotation.
					Matrix4x4 worldMat = item.object->CalculateWorldMatrix();
					ImDrawList* drawList = ImGui::GetForegroundDrawList();
					ImVec2 mousePos = ImGui::GetMousePos();

					int hoveredIndex = -1;
					float minDistance = 12.0f; // ドット選択の判定距離

					std::unordered_set<int> selectedVertexSet(
						item.selectedVertexIndices.begin(),
						item.selectedVertexIndices.end());

					for (uint32_t i = 0; i < vertexCount; ++i) {
						Vector3 localPos = editorVertexPosition(i);
						Matrix4x4 actualWorld = getVertexWorldMatrix(model, i, worldMat);
						Vector3 worldPos = {
							localPos.x * actualWorld.m[0][0] + localPos.y * actualWorld.m[1][0] + localPos.z * actualWorld.m[2][0] + actualWorld.m[3][0],
							localPos.x * actualWorld.m[0][1] + localPos.y * actualWorld.m[1][1] + localPos.z * actualWorld.m[2][1] + actualWorld.m[3][1],
							localPos.x * actualWorld.m[0][2] + localPos.y * actualWorld.m[1][2] + localPos.z * actualWorld.m[2][2] + actualWorld.m[3][2],
						};

						ImVec2 screenPos{};
						if (project(worldPos, screenPos)) {
							bool selected = (static_cast<int>(i) == item.selectedVertexIndex) ||
								selectedVertexSet.contains(static_cast<int>(i));
							ImU32 dotColor = selected ? IM_COL32(255, 255, 0, 255) : IM_COL32(80, 80, 255, 220);
							drawList->AddCircleFilled(screenPos, selected ? 5.5f : 3.5f, dotColor, 12);
							drawList->AddCircle(screenPos, selected ? 7.0f : 5.0f, IM_COL32(30, 30, 30, 220), 12, 1.0f);

							float dx = mousePos.x - screenPos.x;
							float dy = mousePos.y - screenPos.y;
							float dist = std::sqrt(dx * dx + dy * dy);
							if (dist < minDistance) {
								minDistance = dist;
								hoveredIndex = i;
							}
						}
					}

					const bool mouseInsideScene =
						mousePos.x >= sceneMin.x && mousePos.x <= sceneMax.x &&
						mousePos.y >= sceneMin.y && mousePos.y <= sceneMax.y;

					// allowRightClickRotateの算出
					EditorObject* selectedObj = nullptr;
					if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
						selectedObj = &editorObjects_[selectedEditorObject_];
					}
					const bool isEditMode = selectedObj && selectedObj->editMode;
					const bool allowRightClickRotate = isEditMode ? (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) : true;

					// --- 右クリックによるボックス選択の処理 ---
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouseInsideScene && !allowRightClickRotate) {
						boxSelectActive_ = true;
						boxSelectStart_ = mousePos;
					}

					if (boxSelectActive_) {
						drawList->AddRectFilled(boxSelectStart_, mousePos, IM_COL32(255, 255, 0, 40));
						drawList->AddRect(boxSelectStart_, mousePos, IM_COL32(255, 255, 0, 180), 0.0f, 0, 1.5f);

						if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
							boxSelectActive_ = false;

							float minX = std::min(boxSelectStart_.x, mousePos.x);
							float maxX = std::max(boxSelectStart_.x, mousePos.x);
							float minY = std::min(boxSelectStart_.y, mousePos.y);
							float maxY = std::max(boxSelectStart_.y, mousePos.y);

							float rectW = maxX - minX;
							float rectH = maxY - minY;
							if (rectW > 4.0f && rectH > 4.0f) {
								PushUndoSnapshot_();

								if (!ImGui::IsKeyDown(ImGuiKey_LeftShift) && !ImGui::IsKeyDown(ImGuiKey_RightShift) &&
									!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
									item.selectedVertexIndices.clear();
									item.selectedVertexIndex = -1;
								}

								for (uint32_t i = 0; i < vertexCount; ++i) {
									Vector3 localPos = editorVertexPosition(i);
									Vector3 worldPos = {
										localPos.x * worldMat.m[0][0] + localPos.y * worldMat.m[1][0] + localPos.z * worldMat.m[2][0] + worldMat.m[3][0],
										localPos.x * worldMat.m[0][1] + localPos.y * worldMat.m[1][1] + localPos.z * worldMat.m[2][1] + worldMat.m[3][1],
										localPos.x * worldMat.m[0][2] + localPos.y * worldMat.m[1][2] + localPos.z * worldMat.m[2][2] + worldMat.m[3][2],
									};
									ImVec2 screenPos{};
									if (project(worldPos, screenPos)) {
										if (screenPos.x >= minX && screenPos.x <= maxX &&
											screenPos.y >= minY && screenPos.y <= maxY) {
											if (std::find(item.selectedVertexIndices.begin(), item.selectedVertexIndices.end(), static_cast<int>(i)) == item.selectedVertexIndices.end()) {
												item.selectedVertexIndices.push_back(i);
											}
										}
									}
								}

								if (!item.selectedVertexIndices.empty()) {
									item.selectedVertexIndex = item.selectedVertexIndices.front();
									item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
								}
								return;
							}
						}
					}

					// 単一左クリック選択判定
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0 && !viewportBoneDragActive_) {
						PushUndoSnapshot_();
						if (hoveredIndex >= 0) {
							if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift) ||
								ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
								auto it = std::find(item.selectedVertexIndices.begin(), item.selectedVertexIndices.end(), hoveredIndex);
								if (it == item.selectedVertexIndices.end()) {
									item.selectedVertexIndices.push_back(hoveredIndex);
								} else {
									item.selectedVertexIndices.erase(it);
								}
							} else {
								item.selectedVertexIndices.clear();
								item.selectedVertexIndices.push_back(hoveredIndex);
							}
							item.selectedVertexIndex = item.selectedVertexIndices.empty() ? -1 : item.selectedVertexIndices.back();
							item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
							return;
						} else {
							if (!ImGui::IsKeyDown(ImGuiKey_LeftShift) && !ImGui::IsKeyDown(ImGuiKey_RightShift) &&
								!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
								item.selectedVertexIndices.clear();
								item.selectedVertexIndex = -1;
								item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
							}
						}
					}
				}
			}

			const Vector3 axisWorld[3] = {
				{ 1.0f, 0.0f, 0.0f },
				{ 0.0f, 1.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
			};
			const ImU32 axisColor[3] = {
				IM_COL32(255, 70, 70, 255),
				IM_COL32(80, 255, 80, 255),
				IM_COL32(90, 130, 255, 255),
			};
			ImVec2 axisEnd[3]{};
			ImVec2 axisDir[3]{};
			float axisLen[3]{};
			const float worldHandleLength = 1.5f;

			for (int axis = 0; axis < 3; ++axis) {
				ImVec2 projectedEnd{};
				if (!project(gizmoWorldPosition + axisWorld[axis] * worldHandleLength, projectedEnd)) {
					projectedEnd = center;
				}
				ImVec2 rawDir{ projectedEnd.x - center.x, projectedEnd.y - center.y };
				float len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y);
				if (len < 0.001f) {
					rawDir = axis == 0 ? ImVec2(1.0f, 0.0f) : axis == 1 ? ImVec2(0.0f, -1.0f) : ImVec2(0.7f, 0.7f);
					len = 1.0f;
				}
				axisDir[axis] = ImVec2(rawDir.x / len, rawDir.y / len);
				axisLen[axis] = 72.0f;
				axisEnd[axis] = ImVec2(center.x + axisDir[axis].x * axisLen[axis], center.y + axisDir[axis].y * axisLen[axis]);
			}

			auto distanceToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
				const ImVec2 ab{ b.x - a.x, b.y - a.y };
				const ImVec2 ap{ p.x - a.x, p.y - a.y };
				const float abLen2 = ab.x * ab.x + ab.y * ab.y;
				float t = abLen2 > 0.001f ? (ap.x * ab.x + ap.y * ab.y) / abLen2 : 0.0f;
				t = std::clamp(t, 0.0f, 1.0f);
				const ImVec2 nearest{ a.x + ab.x * t, a.y + ab.y * t };
				const float dx = p.x - nearest.x;
				const float dy = p.y - nearest.y;
				return std::sqrt(dx * dx + dy * dy);
				};

			ImDrawList* drawList = ImGui::GetForegroundDrawList();

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0 && !viewportBoneDragActive_) {
				int nearestAxis = -1;
				float nearestDistance = 9999.0f;
				for (int axis = 0; axis < 3; ++axis) {
					const float distance = distanceToSegment(mouse, center, axisEnd[axis]);
					if (distance < nearestDistance) {
						nearestDistance = distance;
						nearestAxis = axis;
					}
				}
				const float centerDistance = std::sqrt((mouse.x - center.x) * (mouse.x - center.x) + (mouse.y - center.y) * (mouse.y - center.y));
				if (centerDistance <= 14.0f || nearestDistance <= 10.0f) {
					activeViewportGizmoAxis_ = centerDistance <= 14.0f ? 3 : nearestAxis;
					viewportGizmoLastMouseX_ = mouse.x;
					viewportGizmoLastMouseY_ = mouse.y;
					transformDragBefore_ = CaptureEditorSnapshot_();
					transformDragActive_ = true;
					transformDragChanged_ = false;
					clickedGizmo = true;
				}
			}

			if (activeViewportGizmoAxis_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				const ImVec2 delta{ mouse.x - viewportGizmoLastMouseX_, mouse.y - viewportGizmoLastMouseY_ };
				viewportGizmoLastMouseX_ = mouse.x;
				viewportGizmoLastMouseY_ = mouse.y;
				const int axis = activeViewportGizmoAxis_;

				if (item.editMode && (item.selectedVertexIndex >= 0 || !item.selectedVertexIndices.empty())) {
					// --- 頂点編集（移動）処理 ---
					float amount = 0.0f;
					float amountX = 0.0f;
					float amountY = 0.0f;

					if (axis >= 0 && axis < 3) {
						const float signedPixels = delta.x * axisDir[axis].x + delta.y * axisDir[axis].y;
						amount = signedPixels / 55.0f;
					} else if (axis == 3) {
						amountX = delta.x / 55.0f;
						amountY = -delta.y / 55.0f;
					}

					if (std::abs(amount) > 0.00001f || std::abs(amountX) > 0.00001f || std::abs(amountY) > 0.00001f) {
						// ワールド移動差分を算出
						Vector3 worldDelta{ 0.0f, 0.0f, 0.0f };
						if (axis == 0) worldDelta = { amount, 0.0f, 0.0f };
						if (axis == 1) worldDelta = { 0.0f, amount, 0.0f };
						if (axis == 2) worldDelta = { 0.0f, 0.0f, amount };
						if (axis == 3) {
							const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
							worldDelta = CameraRight(cameraWorld) * amountX + CameraUp(cameraWorld) * amountY;
						}

						Model* model = item.object ? item.object->GetModel() : nullptr;
						Matrix4x4 worldMat = item.object->CalculateWorldMatrix();
						Matrix4x4 actualWorld = worldMat;
						if (model) {
							auto getVertexWorldMatrix = [&](Model* m, uint32_t vertexIdx, const Matrix4x4& baseWorld) -> Matrix4x4 {
								if (!m) return baseWorld;
								int meshIndex = -1;
								const auto& meshes = m->GetModelData().meshes;
								for (size_t meshK = 0; meshK < meshes.size(); ++meshK) {
									const auto& mesh = meshes[meshK];
									if (vertexIdx >= mesh.startVertex && vertexIdx < mesh.startVertex + mesh.vertices.size()) {
										meshIndex = static_cast<int>(meshK);
										break;
									}
								}
								if (meshIndex < 0) return baseWorld;
								int32_t nodeIndex = m->GetMeshOwnerNodeIndex(static_cast<uint32_t>(meshIndex));
								if (nodeIndex < 0) return baseWorld;
								Matrix4x4 nodeWorld = m->GetNodeWorldMatrix(nodeIndex);
								return Matrix4x4::Multiply(nodeWorld, baseWorld);
								};
							if (!item.selectedVertexIndices.empty()) {
								actualWorld = getVertexWorldMatrix(model, static_cast<uint32_t>(item.selectedVertexIndices.front()), worldMat);
							} else if (item.selectedVertexIndex >= 0) {
								actualWorld = getVertexWorldMatrix(model, static_cast<uint32_t>(item.selectedVertexIndex), worldMat);
							}
						}

						// オブジェクトの回転・スケールの逆行列を作成してローカル移動差分に変換
						Matrix4x4 invRotScl = Matrix4x4::Inverse(actualWorld);

						Vector3 localDelta = {
							worldDelta.x * invRotScl.m[0][0] + worldDelta.y * invRotScl.m[1][0] + worldDelta.z * invRotScl.m[2][0],
							worldDelta.x * invRotScl.m[0][1] + worldDelta.y * invRotScl.m[1][1] + worldDelta.z * invRotScl.m[2][1],
							worldDelta.x * invRotScl.m[0][2] + worldDelta.y * invRotScl.m[1][2] + worldDelta.z * invRotScl.m[2][2],
						};

						// 複数選択の頂点すべてに移動を適用
						if (!item.selectedVertexIndices.empty()) {
							EnsureUniqueModelForObject_(item);
							model = item.object ? item.object->GetModel() : nullptr;
							if (model) {
								uint32_t vertexCount = model->GetVertexCount();
								Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
								if (!originalModel && item.geometryType >= 0) {
									originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
								}

								// 同一座標の共有頂点も含めて一括移動させるための重複排除セット
								MoveSelectedVertices_(item, localDelta);
								transformDragChanged_ = true;
							}
						} else {
							UpdateVertexPositionGroup_(item, item.selectedVertexIndex, localDelta);
							transformDragChanged_ = true;
						}
					}
				} else {
					// --- 通常のトランスフォーム・ボーン移動処理（複数選択対応） ---
					if (axis >= 0 && axis < 3) {
						const float signedPixels = delta.x * axisDir[axis].x + delta.y * axisDir[axis].y;
						const float amount = signedPixels / 55.0f;
						if (std::abs(amount) > 0.00001f) {
							if (editingBone) {
								if (gizmoMode_ == GizmoMode::Translate) {
									Vector3& translate = selectedBonePose->translate;
									if (axis == 0) translate.x += amount;
									if (axis == 1) translate.y += amount;
									if (axis == 2) translate.z += amount;
								} else if (gizmoMode_ == GizmoMode::Rotate) {
									Vector3& rotate = selectedBonePose->rotate;
									if (axis == 0) rotate.x += amount * 0.35f;
									if (axis == 1) rotate.y += amount * 0.35f;
									if (axis == 2) rotate.z += amount * 0.35f;
								} else {
									Vector3& scale = selectedBonePose->scale;
									if (axis == 0) scale.x = std::max(0.01f, scale.x + amount);
									if (axis == 1) scale.y = std::max(0.01f, scale.y + amount);
									if (axis == 2) scale.z = std::max(0.01f, scale.z + amount);
								}
								ApplyEditorObjectBonePose_(item);
							} else {
								for (auto& obj : editorObjects_) {
									if (obj.selected || &obj == &item) {
										if (gizmoMode_ == GizmoMode::Translate) {
											if (axis == 0) obj.position.x += amount;
											if (axis == 1) obj.position.y += amount;
											if (axis == 2) obj.position.z += amount;
										} else if (gizmoMode_ == GizmoMode::Rotate) {
											if (axis == 0) obj.rotation.x += amount * 0.35f;
											if (axis == 1) obj.rotation.y += amount * 0.35f;
											if (axis == 2) obj.rotation.z += amount * 0.35f;
										} else {
											if (axis == 0) obj.scale.x = std::max(0.01f, obj.scale.x + amount);
											if (axis == 1) obj.scale.y = std::max(0.01f, obj.scale.y + amount);
											if (axis == 2) obj.scale.z = std::max(0.01f, obj.scale.z + amount);
										}
										ApplyEditorObjectTransform_(obj);
									}
								}
							}
							transformDragChanged_ = true;
						}
					} else if (axis == 3) {
						const float amountX = delta.x / 55.0f;
						const float amountY = -delta.y / 55.0f;
						if (editingBone) {
							if (gizmoMode_ == GizmoMode::Translate) {
								const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
								selectedBonePose->translate += CameraRight(cameraWorld) * amountX;
								selectedBonePose->translate += CameraUp(cameraWorld) * amountY;
							} else if (gizmoMode_ == GizmoMode::Scale) {
								const float amount = (amountX + amountY) * 0.5f;
								selectedBonePose->scale.x = std::max(0.01f, selectedBonePose->scale.x + amount);
								selectedBonePose->scale.y = std::max(0.01f, selectedBonePose->scale.y + amount);
								selectedBonePose->scale.z = std::max(0.01f, selectedBonePose->scale.z + amount);
							} else {
								selectedBonePose->rotate.y += amountX * 0.35f;
								selectedBonePose->rotate.x += amountY * 0.35f;
							}
							ApplyEditorObjectBonePose_(item);
						} else {
							for (auto& obj : editorObjects_) {
								if (obj.selected || &obj == &item) {
									if (gizmoMode_ == GizmoMode::Translate) {
										const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
										obj.position += CameraRight(cameraWorld) * amountX;
										obj.position += CameraUp(cameraWorld) * amountY;
									} else if (gizmoMode_ == GizmoMode::Scale) {
										const float amount = (amountX + amountY) * 0.5f;
										obj.scale.x = std::max(0.01f, obj.scale.x + amount);
										obj.scale.y = std::max(0.01f, obj.scale.y + amount);
										obj.scale.z = std::max(0.01f, obj.scale.z + amount);
									} else {
										obj.rotation.y += amountX * 0.35f;
										obj.rotation.x += amountY * 0.35f;
									}
									ApplyEditorObjectTransform_(obj);
								}
							}
						}
						transformDragChanged_ = true;
					}
				}
			}

			if (activeViewportGizmoAxis_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				if (transformDragChanged_) {
					PushUndoSnapshot_(transformDragBefore_);
				}
				activeViewportGizmoAxis_ = -1;
				transformDragActive_ = false;
				transformDragChanged_ = false;
			}

			const char* modeText =
				gizmoMode_ == GizmoMode::Translate ? "Translate" :
				gizmoMode_ == GizmoMode::Rotate ? "Rotate" : "Scale";
			const std::string gizmoLabel = editingBone ? (std::string("Bone ") + modeText) : modeText;
			drawList->AddCircleFilled(center, activeViewportGizmoAxis_ == 3 ? 9.0f : 7.0f, IM_COL32(255, 255, 255, 230));
			drawList->AddCircle(center, gizmoMode_ == GizmoMode::Rotate ? 46.0f : 14.0f, IM_COL32(255, 255, 255, 180), 48, 2.0f);
			for (int axis = 0; axis < 3; ++axis) {
				const float thickness = activeViewportGizmoAxis_ == axis ? 5.0f : 3.0f;
				drawList->AddLine(center, axisEnd[axis], axisColor[axis], thickness);
				drawList->AddCircleFilled(axisEnd[axis], gizmoMode_ == GizmoMode::Scale ? 7.0f : 5.0f, axisColor[axis]);
			}
			drawList->AddText(ImVec2(center.x + 12.0f, center.y + 12.0f), IM_COL32(255, 255, 255, 230), gizmoLabel.c_str());
		}
	}

	// --- シーン上でのオブジェクトクリック判定 (ギズモをクリックしなかった、または未選択の場合) ---
	const bool isEditModeActive = hasSelection && editorObjects_[selectedEditorObject_].editMode;
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && !clickedGizmo && !viewportBoneDragActive_ && !isEditModeActive) {
		int clickedObjectIndex = -1;
		float minDistanceToCamera = 99999.0f;

		// 3Dレイ（ワールド空間）を作成する
		float ndcX = ((mouse.x - sceneMin.x) / sceneW) * 2.0f - 1.0f;
		float ndcY = 1.0f - ((mouse.y - sceneMin.y) / sceneH) * 2.0f;

		Matrix4x4 vp = sceneCamera->GetViewProjectionMatrix();
		Matrix4x4 invVP = Matrix4x4::Inverse(vp);

		Vector4 nearPointClip = { ndcX, ndcY, 0.0f, 1.0f };
		Vector4 farPointClip = { ndcX, ndcY, 1.0f, 1.0f };

		auto transformClipToWorld = [&](const Vector4& clip) -> Vector3 {
			float x = clip.x * invVP.m[0][0] + clip.y * invVP.m[1][0] + clip.z * invVP.m[2][0] + invVP.m[3][0];
			float y = clip.x * invVP.m[0][1] + clip.y * invVP.m[1][1] + clip.z * invVP.m[2][1] + invVP.m[3][1];
			float z = clip.x * invVP.m[0][2] + clip.y * invVP.m[1][2] + clip.z * invVP.m[2][2] + invVP.m[3][2];
			float w = clip.x * invVP.m[0][3] + clip.y * invVP.m[1][3] + clip.z * invVP.m[2][3] + invVP.m[3][3];
			if (std::abs(w) > 0.0001f) {
				return { x / w, y / w, z / w };
			}
			return { x, y, z };
			};

		Vector3 rayOrigin = transformClipToWorld(nearPointClip);
		Vector3 rayTarget = transformClipToWorld(farPointClip);
		Vector3 rayDir = { rayTarget.x - rayOrigin.x, rayTarget.y - rayOrigin.y, rayTarget.z - rayOrigin.z };

		float len = std::sqrt(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
		if (len > 0.001f) {
			rayDir = { rayDir.x / len, rayDir.y / len, rayDir.z / len };
		}

		for (int k = 0; k < static_cast<int>(editorObjects_.size()); ++k) {
			const auto& obj = editorObjects_[k];
			if (!obj.visible) {
				continue;
			}

			AABB localAABB{ {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f} };
			if (obj.object) {
				Model* model = obj.object->GetModel();
				if (model) {
					AABB tempAABB{};
					if (model->GetLocalAABB(tempAABB)) {
						localAABB = tempAABB;
					}
				}
			}

			Matrix4x4 worldMat = Matrix4x4::MakeAffineMatrix(obj.scale, obj.rotation, obj.position);
			Matrix4x4 invWorld = Matrix4x4::Inverse(worldMat);

			// レイをローカル空間に変換
			Vector3 localRayOrigin = {
				rayOrigin.x * invWorld.m[0][0] + rayOrigin.y * invWorld.m[1][0] + rayOrigin.z * invWorld.m[2][0] + invWorld.m[3][0],
				rayOrigin.x * invWorld.m[0][1] + rayOrigin.y * invWorld.m[1][1] + rayOrigin.z * invWorld.m[2][1] + invWorld.m[3][1],
				rayOrigin.x * invWorld.m[0][2] + rayOrigin.y * invWorld.m[1][2] + rayOrigin.z * invWorld.m[2][2] + invWorld.m[3][2],
			};
			Vector3 localRayDir = {
				rayDir.x * invWorld.m[0][0] + rayDir.y * invWorld.m[1][0] + rayDir.z * invWorld.m[2][0],
				rayDir.x * invWorld.m[0][1] + rayDir.y * invWorld.m[1][1] + rayDir.z * invWorld.m[2][1],
				rayDir.x * invWorld.m[0][2] + rayDir.y * invWorld.m[1][2] + rayDir.z * invWorld.m[2][2],
			};

			// スラブ法によるRay-AABB判定
			float tmin = 0.0f;
			float tmax = 99999.0f;
			bool intersect = true;

			// X軸
			if (std::abs(localRayDir.x) < 0.00001f) {
				if (localRayOrigin.x < localAABB.min.x || localRayOrigin.x > localAABB.max.x) {
					intersect = false;
				}
			} else {
				float t1 = (localAABB.min.x - localRayOrigin.x) / localRayDir.x;
				float t2 = (localAABB.max.x - localRayOrigin.x) / localRayDir.x;
				if (t1 > t2) std::swap(t1, t2);
				tmin = std::max(tmin, t1);
				tmax = std::min(tmax, t2);
			}

			// Y軸
			if (intersect) {
				if (std::abs(localRayDir.y) < 0.00001f) {
					if (localRayOrigin.y < localAABB.min.y || localRayOrigin.y > localAABB.max.y) {
						intersect = false;
					}
				} else {
					float t1 = (localAABB.min.y - localRayOrigin.y) / localRayDir.y;
					float t2 = (localAABB.max.y - localRayOrigin.y) / localRayDir.y;
					if (t1 > t2) std::swap(t1, t2);
					tmin = std::max(tmin, t1);
					tmax = std::min(tmax, t2);
				}
			}

			// Z軸
			if (intersect) {
				if (std::abs(localRayDir.z) < 0.00001f) {
					if (localRayOrigin.z < localAABB.min.z || localRayOrigin.z > localAABB.max.z) {
						intersect = false;
					}
				} else {
					float t1 = (localAABB.min.z - localRayOrigin.z) / localRayDir.z;
					float t2 = (localAABB.max.z - localRayOrigin.z) / localRayDir.z;
					if (t1 > t2) std::swap(t1, t2);
					tmin = std::max(tmin, t1);
					tmax = std::min(tmax, t2);
				}
			}

			if (intersect && tmin <= tmax) {
				Vector3 hitPointWorld = {
					rayOrigin.x + rayDir.x * tmin,
					rayOrigin.y + rayDir.y * tmin,
					rayOrigin.z + rayDir.z * tmin
				};

				const Matrix4x4& viewMat = sceneCamera->GetViewMatrix();
				float hitViewSpaceZ = hitPointWorld.x * viewMat.m[0][2] +
					hitPointWorld.y * viewMat.m[1][2] +
					hitPointWorld.z * viewMat.m[2][2] +
					viewMat.m[3][2];

				if (hitViewSpaceZ < minDistanceToCamera) {
					minDistanceToCamera = hitViewSpaceZ;
					clickedObjectIndex = k;
				}
			}
		}

		if (clickedObjectIndex >= 0) {
			PushUndoSnapshot_();
			ImGuiIO& io = ImGui::GetIO();
			const bool isCtrlPressed = io.KeyCtrl ||
				(app.GetInput() && (app.GetInput()->IsKeyPressed(DIK_LCONTROL) || app.GetInput()->IsKeyPressed(DIK_RCONTROL)));
			if (isCtrlPressed) {
				editorObjects_[clickedObjectIndex].selected = !editorObjects_[clickedObjectIndex].selected;
				if (editorObjects_[clickedObjectIndex].selected) {
					selectedEditorObject_ = clickedObjectIndex;
				} else if (selectedEditorObject_ == clickedObjectIndex) {
					selectedEditorObject_ = -1;
					for (int k = 0; k < static_cast<int>(editorObjects_.size()); ++k) {
						if (editorObjects_[k].selected) {
							selectedEditorObject_ = k;
							break;
						}
					}
				}
			} else {
				for (auto& other : editorObjects_) {
					other.selected = false;
				}
				editorObjects_[clickedObjectIndex].selected = true;
				selectedEditorObject_ = clickedObjectIndex;
			}
			selectedParticleNode_ = -1;
		}
	}
#endif
}

void ParticleTestScene::DrawEditorCameraControls_()
{
#ifdef USE_IMGUI
	if (!camera_ || !gHasSceneImageRect) {
		return;
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Editor Camera");
	bool uiChanged = false;
	uiChanged |= ImGui::DragFloat3("Camera Position", &editorCameraPosition_.x, 0.1f);
	uiChanged |= ImGui::DragFloat3("Camera Rotation", &editorCameraRotation_.x, 0.01f);
	ImGui::DragFloat("Move Speed", &editorCameraMoveSpeed_, 0.01f, 0.01f, 5.0f);
	ImGui::DragFloat("Look Speed", &editorCameraLookSpeed_, 0.0005f, 0.001f, 0.05f, "%.4f");
	const bool applyCamera = ImGui::Button("Apply Camera");
	if (uiChanged || applyCamera) {
		camera_->SetTranslate(editorCameraPosition_);
		camera_->SetRotate(editorCameraRotation_);
		camera_->Update();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Camera")) {
		editorCameraPosition_ = { 0.0f, 3.0f, -20.0f };
		editorCameraRotation_ = { 0.0f, 0.0f, 0.0f };
		camera_->SetTranslate(editorCameraPosition_);
		camera_->SetRotate(editorCameraRotation_);
		camera_->Update();
	}
#endif
}

void ParticleTestScene::DrawAnimationCameraControls_()
{
#ifdef USE_IMGUI
	ImGui::Separator();
	ImGui::TextUnformatted("Animation Camera");
	bool cameraChanged = false;
	cameraChanged |= ImGui::Checkbox("Preview Animation Camera", &useAnimationCameraPreview_);
	if (!useAnimationCameraPreview_) {
		animationCameraPreviewSwapped_ = false;
	}
	cameraChanged |= ImGui::DragFloat3("Anim Cam Position", &animationCameraPosition_.x, 0.1f);
	cameraChanged |= ImGui::DragFloat3("Anim Cam Rotation", &animationCameraRotation_.x, 0.01f);
	cameraChanged |= ImGui::SliderFloat("Anim Cam FovY", &animationCameraFovY_, 0.1f, 1.8f, "%.3f");
	if (cameraChanged) {
		ApplyAnimationCamera_();
		ApplyCameraToEditorObjects_();
	}

	if (ImGui::Button("Copy From Editor Camera") && camera_) {
		PushUndoSnapshot_();
		animationCameraPosition_ = camera_->GetTranslate();
		animationCameraRotation_ = camera_->GetRotate();
		animationCameraFovY_ = camera_->GetFovY();
		ApplyAnimationCamera_();
		ApplyCameraToEditorObjects_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Anim Camera")) {
		PushUndoSnapshot_();
		animationCameraPosition_ = { 0.0f, 3.0f, -12.0f };
		animationCameraRotation_ = { 0.0f, 0.0f, 0.0f };
		animationCameraFovY_ = 0.45f;
		ApplyAnimationCamera_();
		ApplyCameraToEditorObjects_();
	}

	if (ImGui::Button("Add / Replace Camera Key")) {
		PushUndoSnapshot_();
		AddCameraKeyframe_();
	}
	ImGui::SameLine();
	const char* deleteCameraBtnLabel = "Delete Near Camera Key";
	if (selectedKeyframeType_ == DragTarget::CameraKeyframe && selectedKeyframeIndex_ >= 0) {
		deleteCameraBtnLabel = "Delete Selected Camera Key";
	}
	if (ImGui::Button(deleteCameraBtnLabel)) {
		PushUndoSnapshot_();
		DeleteNearestCameraKeyframe_();
	}

	if (selectedKeyframeType_ == DragTarget::CameraKeyframe &&
		selectedKeyframeIndex_ >= 0 &&
		selectedKeyframeIndex_ < static_cast<int>(cameraKeyframes_.size())) {

		auto& key = cameraKeyframes_[selectedKeyframeIndex_];
		ImGui::SeparatorText("Selected Camera Keyframe");
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::InputFloat("Camera Key Time", &key.time, 0.01f, 0.1f, "%.3f")) {
			key.time = std::clamp(key.time, 0.0f, timelineDuration_);
			SortCameraKeyframes_();
			EvaluateTimeline_(false);
		}
	}
#endif
}

void ParticleTestScene::HandleEffectEditorShortcuts_(GameApp& app)
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 mouse = ImGui::GetMousePos();
	const bool rightCameraDrag =
		gHasSceneImageRect &&
		ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
		mouse.x >= gSceneImageMin.x && mouse.x <= gSceneImageMax.x &&
		mouse.y >= gSceneImageMin.y && mouse.y <= gSceneImageMax.y;

	Input* input = app.GetInput();
	if (!input) return;

	// Ctrlキー押下判定（ImGuiとDirectInputの論理和、GetAsyncKeyStateはラグを招くので使わない）
	const bool isCtrlPressed = io.KeyCtrl || input->IsKeyPressed(DIK_LCONTROL) || input->IsKeyPressed(DIK_RCONTROL);

	// 各キーのトリガー判定（ImGuiとDirectInputの論理和で確実に1回だけ検知）
	auto isTriggered = [&](ImGuiKey imguiKey, BYTE dikKey) {
		return ImGui::IsKeyPressed(imguiKey, false) || input->IsKeyTrigger(dikKey);
		};

	// Ctrl+A で全選択
	if (!io.WantTextInput && isCtrlPressed && isTriggered(ImGuiKey_A, DIK_A)) {
		for (auto& obj : editorObjects_) {
			obj.selected = true;
		}
		if (!editorObjects_.empty()) {
			selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
			selectedParticleNode_ = -1;
		}
	}

	// Iキーで選択中オブジェクトすべてにキーフレームを挿入
	if (!io.WantTextInput && isTriggered(ImGuiKey_I, DIK_I)) {
		PushUndoSnapshot_();
		bool anyKeyframed = false;
		for (auto& obj : editorObjects_) {
			const bool isCurrentlySelected = obj.selected || (selectedEditorObject_ >= 0 && &obj == &editorObjects_[selectedEditorObject_]);
			if (isCurrentlySelected) {
				AddKeyframeToObject_(obj);
				anyKeyframed = true;
			}
		}
		if (anyKeyframed) {
			EvaluateTimeline_(false);
		}
	}

	if (!io.WantTextInput && isCtrlPressed && isTriggered(ImGuiKey_Z, DIK_Z)) {
		Undo_(app);
	}
	if (!io.WantTextInput && isCtrlPressed && isTriggered(ImGuiKey_Y, DIK_Y)) {
		Redo_(app);
	}
	if (!io.WantTextInput && isCtrlPressed && isTriggered(ImGuiKey_C, DIK_C)) {
		if (selectedKeyframeType_ == DragTarget::ModelKeyframe && selectedKeyframeIndex_ >= 0) {
			CopySelectedModelKeyframe_();
		} else {
			CopySelectedObject_();
		}
	}
	if (!io.WantTextInput && isCtrlPressed && isTriggered(ImGuiKey_V, DIK_V)) {
		if (hasCopiedModelKeyframe_ && selectedEditorObject_ >= 0) {
			PushUndoSnapshot_();
			PasteCopiedModelKeyframe_();
		} else if (hasCopiedObject_) {
			PushUndoSnapshot_();
			PasteEditorObject_(app);
		}
	}
	if (!io.WantTextInput && isTriggered(ImGuiKey_Delete, DIK_DELETE)) {
		if (selectedKeyframeIndex_ >= 0 && selectedKeyframeType_ != DragTarget::None) {
			PushUndoSnapshot_();
			switch (selectedKeyframeType_) {
			case DragTarget::ModelKeyframe:
				if (selectedKeyframeObjectIndex_ >= 0 && selectedKeyframeObjectIndex_ < static_cast<int>(editorObjects_.size())) {
					auto& keys = editorObjects_[selectedKeyframeObjectIndex_].keyframes;
					if (selectedKeyframeIndex_ >= 0 && selectedKeyframeIndex_ < static_cast<int>(keys.size())) {
						keys.erase(keys.begin() + selectedKeyframeIndex_);
						EvaluateTimeline_(false);
					}
				}
				break;
			case DragTarget::CameraKeyframe:
				if (selectedKeyframeIndex_ >= 0 && selectedKeyframeIndex_ < static_cast<int>(cameraKeyframes_.size())) {
					cameraKeyframes_.erase(cameraKeyframes_.begin() + selectedKeyframeIndex_);
					EvaluateTimeline_(false);
				}
				break;
			case DragTarget::PlayerAttackHitboxKeyframe:
				if (selectedKeyframeIndex_ >= 0 && selectedKeyframeIndex_ < static_cast<int>(playerAttackHitboxKeyframes_.size())) {
					playerAttackHitboxKeyframes_.erase(playerAttackHitboxKeyframes_.begin() + selectedKeyframeIndex_);
					EvaluateTimeline_(false);
				}
				break;
			case DragTarget::PlayerSpecialHitboxKeyframe: {
				auto& keys = CurrentPlayerSpecialTimeline_().hitboxes;
				if (selectedKeyframeIndex_ >= 0 && selectedKeyframeIndex_ < static_cast<int>(keys.size())) {
					keys.erase(keys.begin() + selectedKeyframeIndex_);
					EvaluatePlayerSpecialTimeline_();
				}
				break;
			}
			default:
				break;
			}
			selectedKeyframeIndex_ = -1;
			selectedKeyframeType_ = DragTarget::None;
			selectedKeyframeObjectIndex_ = -1;
		} else if (selectedEditorObject_ >= 0) {
			RequestDeleteSelectedObject_();
		} else if (selectedParticleNode_ >= 0) {
			PushUndoSnapshot_();
			particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
			selectedParticleNode_ = -1;
		}
	}
	if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && isTriggered(ImGuiKey_W, DIK_W)) {
		gizmoMode_ = GizmoMode::Translate;
	}
	if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && isTriggered(ImGuiKey_E, DIK_E)) {
		gizmoMode_ = GizmoMode::Rotate;
	}
	if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && isTriggered(ImGuiKey_R, DIK_R)) {
		gizmoMode_ = GizmoMode::Scale;
	}
#endif
}

void ParticleTestScene::DrawEffectInspectorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
	if (gParticleTestEditorModeSwitcherVisible && gParticleTestEditorMode == 2) {
		DrawPlayerAttackInspectorImGui_(app);
		return;
	}

	ImGui::Begin("Inspector");

	ImGui::TextUnformatted("Model Source");
	ImGui::InputText("Model Path", editorModelPath_, sizeof(editorModelPath_));
	if (ImGui::Button("Open Model File...")) {
		OpenModelFileDialog_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Open + Add")) {
		if (OpenModelFileDialog_()) {
			PushUndoSnapshot_();
			AddEditorObject_(app, editorModelPath_);
		}
	}
	if (ImGui::Button("Add Model")) {
		PushUndoSnapshot_();
		AddEditorObject_(app, editorModelPath_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Weapon")) {
		PushUndoSnapshot_();
		AddEditorObject_(app, "Player/sword.obj");
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Geometry Source");
	ImGui::Combo("Geometry Type", &selectedGeometryType_, kGeometryNames, kGeometryCount);
	if (ImGui::Button("Add Geometry")) {
		PushUndoSnapshot_();
		AddGeometryObject_(app, selectedGeometryType_);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Particle Node Source");
	if (ImGui::Button("Add Particle Node")) {
		std::vector<std::string> groupNames;
		std::string fileName;
		if (OpenParticleFileDialog_(groupNames, fileName)) {
			PushUndoSnapshot_();
			ParticleManager::GetInstance()->LoadAdditional(fileName, "");

			std::filesystem::path fp(fileName);
			std::string stemName = fp.stem().string();

			auto* pm = ParticleManager::GetInstance();
			float maxLifeTime = 0.0f;
			for (const auto& groupName : groupNames) {
				maxLifeTime = std::max(maxLifeTime, pm->GetGroupLifeTimeMax(groupName));
			}
			if (maxLifeTime <= 0.0f) {
				maxLifeTime = 1.0f;
			}

			ParticleNode node;
			node.name = stemName + "_" + std::to_string(particleNodes_.size() + 1);
			node.particleFileName = fileName;
			node.startTime = 0.0f;
			node.presetDuration = maxLifeTime;
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			node.position = { 0.0f, 1.0f, 0.0f };
			particleNodes_.push_back(std::move(node));

			selectedParticleNode_ = static_cast<int>(particleNodes_.size()) - 1;
			selectedEditorObject_ = -1;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Effect Node##Hierarchy")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(false, jsonPath)) {
			PushUndoSnapshot_();
			AddEffectReferenceNode_(jsonPath);
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Object Actions");
	if (ImGui::Button("Duplicate") && selectedEditorObject_ >= 0) {
		PushUndoSnapshot_();
		DuplicateSelectedObject_(app);
	}
	ImGui::SameLine();
	if (ImGui::Button("Copy") && selectedEditorObject_ >= 0) {
		CopySelectedObject_();
	}
	if (ImGui::Button("Paste") && hasCopiedObject_) {
		PushUndoSnapshot_();
		PasteEditorObject_(app);
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete")) {
		if (selectedEditorObject_ >= 0) {
			RequestDeleteSelectedObject_();
		} else if (selectedParticleNode_ >= 0) {
			PushUndoSnapshot_();
			particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
			selectedParticleNode_ = -1;
		}
	}
	if (ImGui::Button("Undo")) {
		Undo_(app);
	}
	ImGui::SameLine();
	if (ImGui::Button("Redo")) {
		Redo_(app);
	}

	ImGui::InputText("Effect JSON", effectJsonPath_, sizeof(effectJsonPath_));
	if (ImGui::Button("Save Effect JSON")) {
		const std::string savePath = MakeEffectsJsonPath_(effectJsonPath_);
		strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), savePath.c_str(), _TRUNCATE);
		SaveEffectJson_(effectJsonPath_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save As...##EffectJson")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(true, jsonPath)) {
			strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), jsonPath.c_str(), _TRUNCATE);
			SaveEffectJson_(effectJsonPath_);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Open...##EffectJson")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(false, jsonPath)) {
			strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), jsonPath.c_str(), _TRUNCATE);
			PushUndoSnapshot_();
			LoadEffectJson_(app, effectJsonPath_);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Effect JSON")) {
		PushUndoSnapshot_();
		LoadEffectJson_(app, effectJsonPath_);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Scene Objects");
	for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
		auto& obj = editorObjects_[i];
		const bool selected = obj.selected || (i == selectedEditorObject_);

		ImGui::PushID(i);

		if (ImGui::Checkbox("##visible", &obj.visible)) {
			if (obj.object) {
				obj.object->SetIsVisible(obj.visible);
			}
		}

		ImGui::SameLine();

		if (ImGui::Selectable(obj.name.c_str(), selected)) {
			selectedParticleNode_ = -1;
			const bool isCtrlPressed = ImGui::GetIO().KeyCtrl ||
				(app.GetInput() && (app.GetInput()->IsKeyPressed(DIK_LCONTROL) || app.GetInput()->IsKeyPressed(DIK_RCONTROL)));
			if (isCtrlPressed) {
				// Ctrlキー押下時：選択状態をトグル
				obj.selected = !obj.selected;
				if (obj.selected) {
					selectedEditorObject_ = i;
				} else if (selectedEditorObject_ == i) {
					selectedEditorObject_ = -1;
					for (int k = 0; k < static_cast<int>(editorObjects_.size()); ++k) {
						if (editorObjects_[k].selected) {
							selectedEditorObject_ = k;
							break;
						}
					}
				}
			} else {
				// Ctrlキーなし：これ単体のみを選択（他は解除）
				for (auto& other : editorObjects_) {
					other.selected = false;
				}
				obj.selected = true;
				selectedEditorObject_ = i;
			}
		}

		ImGui::PopID();
	}
	for (int i = 0; i < static_cast<int>(particleNodes_.size()); ++i) {
		const bool selected = i == selectedParticleNode_;
		char label[128];
		sprintf_s(label, "%s (%.2f-%.2f) [%s]", particleNodes_[i].name.c_str(), particleNodes_[i].startTime, particleNodes_[i].endTime,
			particleNodes_[i].isEffectNode ? "Effect" : "Particle");
		if (ImGui::Selectable(label, selected)) {
			selectedParticleNode_ = i;
			selectedEditorObject_ = -1;
		}
	}

	if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
		EditorObject& item = editorObjects_[selectedEditorObject_];
		ImGui::Separator();
		ImGui::Text("%s (%s)", item.name.c_str(), item.modelPath.c_str());

		const bool hasSelectedModelKey =
			selectedKeyframeType_ == DragTarget::ModelKeyframe &&
			selectedKeyframeObjectIndex_ == selectedEditorObject_ &&
			selectedKeyframeIndex_ >= 0 &&
			selectedKeyframeIndex_ < static_cast<int>(item.keyframes.size());
		if (hasSelectedModelKey &&
			ImGui::CollapsingHeader("Selected Keyframe", ImGuiTreeNodeFlags_DefaultOpen)) {
			float selectedKeyTime = item.keyframes[selectedKeyframeIndex_].time;
			if (ImGui::DragFloat("Key Time##Inspector", &selectedKeyTime, 0.01f, 0.0f, timelineDuration_, "%.3f sec")) {
				item.keyframes[selectedKeyframeIndex_].time = std::clamp(selectedKeyTime, 0.0f, timelineDuration_);
				SortKeyframes_(item);
				selectedKeyframeIndex_ = 0;
				float nearestDistance = std::abs(item.keyframes.front().time - selectedKeyTime);
				for (int i = 1; i < static_cast<int>(item.keyframes.size()); ++i) {
					const float distance = std::abs(item.keyframes[i].time - selectedKeyTime);
					if (distance < nearestDistance) {
						nearestDistance = distance;
						selectedKeyframeIndex_ = i;
					}
				}
				EvaluateTimeline_(false);
			}

			EffectKeyframe& selectedKey = item.keyframes[selectedKeyframeIndex_];
			const char* interpolationNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out" };
			if (ImGui::Combo("Interpolation To Next##Inspector", &selectedKey.interpolationType,
				interpolationNames, IM_ARRAYSIZE(interpolationNames))) {
				EvaluateTimeline_(false);
			}
			const bool hasNextKey = selectedKeyframeIndex_ + 1 < static_cast<int>(item.keyframes.size());
			if (hasNextKey) {
				ImGui::Text("Applies: %.3fs -> %.3fs", selectedKey.time,
					item.keyframes[selectedKeyframeIndex_ + 1].time);
			} else {
				ImGui::TextDisabled("Last key: no following interpolation segment.");
			}
			if (!hasNextKey) ImGui::BeginDisabled();
			if (ImGui::Button("Set Linear To Next Key")) {
				selectedKey.interpolationType = 0;
				EvaluateTimeline_(false);
			}
			if (!hasNextKey) ImGui::EndDisabled();
			if (ImGui::Button("Copy Keyframe")) {
				CopySelectedModelKeyframe_();
			}
			ImGui::SameLine();
			if (!hasCopiedModelKeyframe_) ImGui::BeginDisabled();
			if (ImGui::Button("Paste Keyframe At Current Time")) {
				PushUndoSnapshot_();
				PasteCopiedModelKeyframe_();
			}
			if (!hasCopiedModelKeyframe_) ImGui::EndDisabled();
			ImGui::TextDisabled("Ctrl+C / Ctrl+V also works while a model key is selected.");

			const char* channelNames[] = { "All", "Position", "Rotation", "Scale", "Color" };
			ImGui::Text("Channel: %s", channelNames[std::clamp(selectedModelKeyframeChannel_, 0, 4)]);
			bool selectedKeyChanged = false;
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 1) &&
				ImGui::CollapsingHeader("Position##InspectorSelectedKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				selectedKeyChanged |= ImGui::DragFloat3("Position##InspectorKeyValue", &selectedKey.position.x, 0.05f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 2) &&
				ImGui::CollapsingHeader("Rotation##InspectorSelectedKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				selectedKeyChanged |= ImGui::DragFloat3("Rotation##InspectorKeyValue", &selectedKey.rotation.x, 0.01f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 3) &&
				ImGui::CollapsingHeader("Scale##InspectorSelectedKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				selectedKeyChanged |= ImGui::DragFloat3("Scale##InspectorKeyValue", &selectedKey.scale.x, 0.05f, 0.001f, 1000.0f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 4) &&
				ImGui::CollapsingHeader("Color##InspectorSelectedKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				selectedKeyChanged |= ImGui::ColorEdit4("Color##InspectorKeyValue", &selectedKey.color.x);
			}
			if (selectedKeyChanged) EvaluateTimeline_(false);
			ImGui::Separator();
		}
		if (hasCopiedModelKeyframe_ && !hasSelectedModelKey) {
			if (ImGui::Button("Paste Copied Keyframe At Current Time")) {
				PushUndoSnapshot_();
				PasteCopiedModelKeyframe_();
			}
			ImGui::TextDisabled("Copied keyframe is ready. Shortcut: Ctrl+V");
			ImGui::Separator();
		}
		bool changed = false;

		auto trackDragEdit = [&](bool itemChanged) {
			if (ImGui::IsItemActivated() && !transformDragActive_) {
				transformDragBefore_ = CaptureEditorSnapshot_();
				transformDragActive_ = true;
				transformDragChanged_ = false;
			}
			if (itemChanged) {
				transformDragChanged_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
				if (transformDragChanged_) {
					PushUndoSnapshot_(transformDragBefore_);
				}
				transformDragActive_ = false;
				transformDragChanged_ = false;
			}
			};

		bool modelChanged = false;
		if (item.geometryType < 0) {
			char modelBuf[256];
			strncpy_s(modelBuf, sizeof(modelBuf), item.modelPath.c_str(), _TRUNCATE);
			if (ImGui::InputText("Model Path", modelBuf, sizeof(modelBuf))) {
				item.modelPath = modelBuf;
				modelChanged = true;
			}
			if (ImGui::IsItemActivated() && !transformDragActive_) {
				transformDragBefore_ = CaptureEditorSnapshot_();
				transformDragActive_ = true;
				transformDragChanged_ = false;
			}
			if (ImGui::IsItemEdited()) {
				transformDragChanged_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
				if (transformDragChanged_) {
					PushUndoSnapshot_(transformDragBefore_);
				}
				transformDragActive_ = false;
				transformDragChanged_ = false;
			}
			if (ImGui::Button("Open Model File...##SelectedObject")) {
				std::string modelPath;
				if (OpenModelFileDialog_(modelPath)) {
					PushUndoSnapshot_();
					item.modelPath = modelPath;
					modelChanged = true;
				}
			}
		}
		if (modelChanged) {
			item.object->SetModel(item.modelPath);
			changed = true;
		}

		Vector3 oldPosition = item.position;
		Vector3 oldRotation = item.rotation;
		Vector3 oldScale = item.scale;

		bool positionChanged = ImGui::DragFloat3("Position", &item.position.x, 0.05f);
		if (positionChanged) {
			Vector3 delta = { item.position.x - oldPosition.x, item.position.y - oldPosition.y, item.position.z - oldPosition.z };
			for (auto& obj : editorObjects_) {
				if (obj.selected || &obj == &item) {
					if (&obj != &item) {
						obj.position = { obj.position.x + delta.x, obj.position.y + delta.y, obj.position.z + delta.z };
						ApplyEditorObjectTransform_(obj);
					}
				}
			}
			changed = true;
		}
		trackDragEdit(positionChanged);

		bool rotationChanged = ImGui::DragFloat3("Rotation", &item.rotation.x, 0.01f);
		if (rotationChanged) {
			Vector3 delta = { item.rotation.x - oldRotation.x, item.rotation.y - oldRotation.y, item.rotation.z - oldRotation.z };
			for (auto& obj : editorObjects_) {
				if (obj.selected || &obj == &item) {
					if (&obj != &item) {
						obj.rotation = { obj.rotation.x + delta.x, obj.rotation.y + delta.y, obj.rotation.z + delta.z };
						ApplyEditorObjectTransform_(obj);
					}
				}
			}
			changed = true;
		}
		trackDragEdit(rotationChanged);

		bool scaleChanged = ImGui::DragFloat3("Scale", &item.scale.x, 0.05f, 0.01f, 100.0f);
		if (scaleChanged) {
			Vector3 delta = { item.scale.x - oldScale.x, item.scale.y - oldScale.y, item.scale.z - oldScale.z };
			for (auto& obj : editorObjects_) {
				if (obj.selected || &obj == &item) {
					if (&obj != &item) {
						obj.scale = { obj.scale.x + delta.x, obj.scale.y + delta.y, obj.scale.z + delta.z };
						obj.scale.x = std::max(0.001f, obj.scale.x);
						obj.scale.y = std::max(0.001f, obj.scale.y);
						obj.scale.z = std::max(0.001f, obj.scale.z);
						ApplyEditorObjectTransform_(obj);
					}
				}
			}
			changed = true;
		}
		trackDragEdit(scaleChanged);

		bool colorChanged = ImGui::ColorEdit4("Color / Alpha", &item.color.x);
		if (colorChanged) {
			for (auto& obj : editorObjects_) {
				if (obj.selected || &obj == &item) {
					if (&obj != &item) {
						obj.color = item.color;
					}
				}
			}
			changed = true;
		}
		trackDragEdit(colorChanged);

		int currentBlend = static_cast<int>(item.blendMode);
		if (ImGui::Combo("Blend Mode", &currentBlend, kObjectBlendModeNames, IM_ARRAYSIZE(kObjectBlendModeNames))) {
			PushUndoSnapshot_();
			currentBlend = std::clamp(currentBlend, 0, static_cast<int>(Object3dCommon::BlendMode::kCountOfBlendMode) - 1);
			item.blendMode = static_cast<Object3dCommon::BlendMode>(currentBlend);
			for (auto& obj : editorObjects_) {
				if (obj.selected || &obj == &item) {
					obj.blendMode = item.blendMode;
				}
			}
			changed = true;
		}

		char textureBuf[256];
		strncpy_s(textureBuf, sizeof(textureBuf), item.texturePath.c_str(), _TRUNCATE);
		if (ImGui::InputText("Texture Path", textureBuf, sizeof(textureBuf))) {
			item.texturePath = textureBuf;
			changed = true;
		}
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (ImGui::IsItemEdited()) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}
		if (ImGui::Button("Open Texture File...")) {
			std::string texturePath;
			if (OpenTextureFileDialog_(texturePath)) {
				PushUndoSnapshot_();
				item.texturePath = texturePath;
				changed = true;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Texture")) {
			PushUndoSnapshot_();
			item.texturePath.clear();
			changed = true;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Object Post Effect");
		const bool bloomBefore = item.bloomPostEffect;
		if (ImGui::Checkbox("Bloom", &item.bloomPostEffect)) {
			item.bloomPostEffect = bloomBefore;
			PushUndoSnapshot_();
			item.bloomPostEffect = !bloomBefore;
		}
		const bool outlineBloomBefore = item.outlineBloomPostEffect;
		if (ImGui::Checkbox("Outline Bloom", &item.outlineBloomPostEffect)) {
			item.outlineBloomPostEffect = outlineBloomBefore;
			PushUndoSnapshot_();
			item.outlineBloomPostEffect = !outlineBloomBefore;
		}

		bool bloomColorChanged = ImGui::ColorEdit4("Bloom Color", &item.bloomColor.x);
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (bloomColorChanged) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}

		bool outlineBloomColorChanged = ImGui::ColorEdit4("Outline Bloom Color", &item.outlineBloomColor.x);
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (outlineBloomColorChanged) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}

		const bool billboardBefore = item.billboard;
		bool billboardChanged = ImGui::Checkbox("Billboard", &item.billboard);
		if (billboardChanged) {
			item.billboard = billboardBefore;
			PushUndoSnapshot_();
			item.billboard = !billboardBefore;
			changed = true;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Attach To Bone");
		const bool attachBefore = item.attachToBone;
		if (ImGui::Checkbox("Enable Bone Attach", &item.attachToBone)) {
			item.attachToBone = attachBefore;
			PushUndoSnapshot_();
			item.attachToBone = !attachBefore;
		}

		const char* parentPreview = "None";
		for (const auto& parent : editorObjects_) {
			if (parent.id == item.attachParentId) {
				parentPreview = parent.name.c_str();
				break;
			}
		}
		if (ImGui::BeginCombo("Attach Parent", parentPreview)) {
			if (ImGui::Selectable("None", item.attachParentId < 0)) {
				PushUndoSnapshot_();
				item.attachParentId = -1;
			}
			for (const auto& parent : editorObjects_) {
				if (parent.id == item.id) {
					continue;
				}
				const bool selected = parent.id == item.attachParentId;
				if (ImGui::Selectable(parent.name.c_str(), selected)) {
					PushUndoSnapshot_();
					item.attachParentId = parent.id;
					if (item.attachJointName.empty() && !parent.bonePoses.empty()) {
						item.attachJointName = parent.bonePoses.front().name;
					}
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		EditorObject* attachParent = nullptr;
		for (auto& parent : editorObjects_) {
			if (parent.id == item.attachParentId && parent.id != item.id) {
				attachParent = &parent;
				break;
			}
		}
		if (attachParent) {
			SyncEditorObjectBones_(*attachParent);
			const char* bonePreview = item.attachJointName.empty() ? "Select Bone" : item.attachJointName.c_str();
			if (ImGui::BeginCombo("Attach Bone", bonePreview)) {
				for (const auto& bone : attachParent->bonePoses) {
					const bool selected = bone.name == item.attachJointName;
					if (ImGui::Selectable(bone.name.c_str(), selected)) {
						PushUndoSnapshot_();
						item.attachJointName = bone.name;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		} else if (item.attachToBone) {
			ImGui::TextDisabled("Select an attach parent object.");
		}

		char attachBoneBuf[128];
		strncpy_s(attachBoneBuf, sizeof(attachBoneBuf), item.attachJointName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Attach Bone Name", attachBoneBuf, sizeof(attachBoneBuf))) {
			item.attachJointName = attachBoneBuf;
		}
		if (ImGui::IsItemActivated() && !transformDragActive_) {
			transformDragBefore_ = CaptureEditorSnapshot_();
			transformDragActive_ = true;
			transformDragChanged_ = false;
		}
		if (ImGui::IsItemEdited()) {
			transformDragChanged_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
			if (transformDragChanged_) {
				PushUndoSnapshot_(transformDragBefore_);
			}
			transformDragActive_ = false;
			transformDragChanged_ = false;
		}

		bool attachOffsetChanged = ImGui::DragFloat3("Attach Offset", &item.attachOffset.x, 0.01f, -20.0f, 20.0f);
		trackDragEdit(attachOffsetChanged);
		bool attachRotationChanged = ImGui::DragFloat3("Attach Rotation", &item.attachRotation.x, 0.01f, -100.0f, 100.0f);
		trackDragEdit(attachRotationChanged);
		bool attachScaleChanged = ImGui::DragFloat3("Attach Scale", &item.attachScale.x, 0.01f, 0.001f, 20.0f);
		trackDragEdit(attachScaleChanged);

		if (changed) {
			ApplyEditorObjectTransform_(item);
		}

		DrawGizmoControls_(item);
		DrawBoneControls_(item);

		// 頂点編集（Edit Mode）コントロール
		ImGui::Separator();
		ImGui::TextUnformatted("Vertex Edit Mode");
		if (ImGui::Checkbox("Edit Mode", &item.editMode)) {
			// Edit Modeをトグルしたとき、まだユニークモデルになっていないならユニークモデルを作成する
			if (item.editMode) {
				EnsureUniqueModelForObject_(item);
			} else {
				item.selectedVertexIndex = -1;
				item.selectedVertexIndices.clear();
				item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
			}
		}

		if (item.editMode) {
			Model* model = item.object ? item.object->GetModel() : nullptr;
			if (model) {
				uint32_t vertexCount = model->GetVertexCount();
				ImGui::Text("Total Vertices: %u", vertexCount);
				const int lastVertexIndex = std::max(0, static_cast<int>(vertexCount) - 1);
				item.vertexRangeStart = std::clamp(item.vertexRangeStart, 0, lastVertexIndex);
				item.vertexRangeEnd = std::clamp(item.vertexRangeEnd, 0, lastVertexIndex);

				ImGui::InputInt("Range Start", &item.vertexRangeStart);
				ImGui::InputInt("Range End", &item.vertexRangeEnd);

				auto appendVertexRange = [&]() {
					if (vertexCount == 0) {
						return;
					}

					const int start = std::clamp(std::min(item.vertexRangeStart, item.vertexRangeEnd), 0, lastVertexIndex);
					const int end = std::clamp(std::max(item.vertexRangeStart, item.vertexRangeEnd), 0, lastVertexIndex);
					for (int i = start; i <= end; ++i) {
						if (std::find(item.selectedVertexIndices.begin(), item.selectedVertexIndices.end(), i) == item.selectedVertexIndices.end()) {
							item.selectedVertexIndices.push_back(i);
						}
					}
					item.selectedVertexIndex = item.selectedVertexIndices.empty() ? -1 : item.selectedVertexIndices.back();
					item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
					};

				if (ImGui::Button("Select Range")) {
					PushUndoSnapshot_();
					item.selectedVertexIndices.clear();
					appendVertexRange();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add Range")) {
					PushUndoSnapshot_();
					appendVertexRange();
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear Selection")) {
					PushUndoSnapshot_();
					item.selectedVertexIndices.clear();
					item.selectedVertexIndex = -1;
					item.vertexSelectionOffset = { 0.0f, 0.0f, 0.0f };
				}

				// --- 選択中頂点のインデックス表示 ---
				if (!item.selectedVertexIndices.empty()) {
					if (item.selectedVertexIndices.size() == 1) {
						ImGui::Text("Selected Vertex: #%d", item.selectedVertexIndex);

						// 現在の頂点ローカル座標を取得して編集できるようにする
						Vector3 currentPos = model->GetVertexPosition(item.selectedVertexIndex);

						bool vtxChanged = ImGui::DragFloat3("Vertex Position", &currentPos.x, 0.01f);
						if (vtxChanged) {
							// Undoスナップショットをドラッグ開始時に保存
							if (ImGui::IsItemActivated() && !transformDragActive_) {
								transformDragBefore_ = CaptureEditorSnapshot_();
								transformDragActive_ = true;
								transformDragChanged_ = false;
							}

							// 移動差分を計算して、グループ内の頂点に適用
							Vector3 oldPos = model->GetVertexPosition(item.selectedVertexIndex);
							Vector3 localDelta = { currentPos.x - oldPos.x, currentPos.y - oldPos.y, currentPos.z - oldPos.z };

							MoveSelectedVertices_(item, localDelta);
							transformDragChanged_ = true;
						}

						if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
							if (transformDragChanged_) {
								PushUndoSnapshot_(transformDragBefore_);
							}
							transformDragActive_ = false;
							transformDragChanged_ = false;
						}
					} else {
						ImGui::Text("Selected Vertices: %zu", item.selectedVertexIndices.size());
						std::string indicesStr = "#";
						for (size_t k = 0; k < std::min<size_t>(item.selectedVertexIndices.size(), 5); ++k) {
							if (k > 0) indicesStr += ", #";
							indicesStr += std::to_string(item.selectedVertexIndices[k]);
						}
						if (item.selectedVertexIndices.size() > 5) {
							indicesStr += "... and " + std::to_string(item.selectedVertexIndices.size() - 5) + " more";
						}
						ImGui::TextUnformatted(indicesStr.c_str());

						Vector3 currentOffset = item.vertexSelectionOffset;
						bool offsetChanged = ImGui::DragFloat3("Selection Offset", &currentOffset.x, 0.01f);
						if (offsetChanged) {
							if (ImGui::IsItemActivated() && !transformDragActive_) {
								transformDragBefore_ = CaptureEditorSnapshot_();
								transformDragActive_ = true;
								transformDragChanged_ = false;
							}

							Vector3 localDelta = {
								currentOffset.x - item.vertexSelectionOffset.x,
								currentOffset.y - item.vertexSelectionOffset.y,
								currentOffset.z - item.vertexSelectionOffset.z
							};
							MoveSelectedVertices_(item, localDelta);
							item.vertexSelectionOffset = currentOffset;
							transformDragChanged_ = true;
						}

						if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
							if (transformDragChanged_) {
								PushUndoSnapshot_(transformDragBefore_);
							}
							transformDragActive_ = false;
							transformDragChanged_ = false;
						}
					}

					if (ImGui::Button("Reset Vertex")) {
						PushUndoSnapshot_();

						Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
						if (!originalModel && item.geometryType >= 0) {
							originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
						}

						if (originalModel) {
							std::unordered_set<uint32_t> affectedVertices;
							uint32_t vertexCount = model->GetVertexCount();

							for (int selIdx : item.selectedVertexIndices) {
								if (selIdx >= 0 && selIdx < static_cast<int>(vertexCount)) {
									Vector3 origSelectedPos = originalModel->GetVertexPosition(selIdx);
									for (uint32_t i = 0; i < vertexCount; ++i) {
										Vector3 origPos = originalModel->GetVertexPosition(i);
										float dx = origPos.x - origSelectedPos.x;
										float dy = origPos.y - origSelectedPos.y;
										float dz = origPos.z - origSelectedPos.z;
										float distSq = dx * dx + dy * dy + dz * dz;
										if (distSq < 0.0001f) {
											affectedVertices.insert(i);
										}
									}
								}
							}

							for (uint32_t i : affectedVertices) {
								item.vertexOffsets.erase(i);
								model->UpdateVertexPosition(i, originalModel->GetVertexPosition(i));
							}
						}
					}
				} else {
					ImGui::TextDisabled("No vertex selected. Click or Box Select (Right Click Drag) vertices.");
				}

				if (ImGui::Button("Reset All Vertices")) {
					PushUndoSnapshot_();
					item.vertexOffsets.clear();

					// 初期モデルからすべての頂点座標をコピーして上書きリセットする
					Model* originalModel = ModelManager::GetInstance()->FindModel(item.modelPath);
					if (!originalModel && item.geometryType >= 0) {
						originalModel = GetOrCreateEditorGeometryModel(item.geometryType);
					}

					if (originalModel) {
						for (uint32_t i = 0; i < vertexCount; ++i) {
							model->UpdateVertexPosition(i, originalModel->GetVertexPosition(i));
						}
					}
				}
			}
		}
	}

	if (selectedParticleNode_ >= 0 && selectedParticleNode_ < static_cast<int>(particleNodes_.size())) {
		ParticleNode& node = particleNodes_[selectedParticleNode_];
		ImGui::Separator();
		ImGui::Text("%s (%s)", node.name.c_str(), node.isEffectNode ? "EffectNode" : "ParticleNode");

		char nameBuf[128];
		strncpy_s(nameBuf, sizeof(nameBuf), node.name.c_str(), _TRUNCATE);
		if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
			node.name = nameBuf;
		}

		char fileBuf[128];
		strncpy_s(fileBuf, sizeof(fileBuf), node.particleFileName.c_str(), _TRUNCATE);
		if (ImGui::InputText(node.isEffectNode ? "Effect File" : "Particle File", fileBuf, sizeof(fileBuf))) {
			node.particleFileName = fileBuf;
		}

		if (ImGui::DragFloat("Start Time", &node.startTime, 0.01f, 0.0f, timelineDuration_)) {
			node.startTime = std::clamp(node.startTime, 0.0f, timelineDuration_);
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat("End Time", &node.endTime, 0.01f, 0.0f, timelineDuration_)) {
			node.endTime = std::clamp(node.endTime, node.startTime + 0.01f, timelineDuration_);
			node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat("Preset Duration", &node.presetDuration, 0.01f, 0.01f, 10.0f)) {
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat3("Position", &node.position.x, 0.05f)) {
			RequestTimelineRebuild_(timelineTime_);
		}
		if (!node.isEffectNode) {
			ImGui::DragFloat3("Rotation", &node.rotation.x, 0.01f);
			ImGui::DragFloat3("Scale", &node.scale.x, 0.05f);
			ImGui::DragInt("Emit Count", &node.emitCount, 1, 1, 1000);
		}
	}

	DrawEditorCameraControls_();
	DrawAnimationCameraControls_();
	ImGui::End();
#endif
}

void ParticleTestScene::AddEffectReferenceNode_(const std::string& jsonPath)
{
	float duration = 1.0f;
	try {
		std::ifstream file(jsonPath);
		if (file.is_open()) {
			json root;
			file >> root;
			duration = std::max(0.01f, root.value("timeline", json::object()).value("duration", 1.0f));
		}
	} catch (...) {
		duration = 1.0f;
	}

	const std::filesystem::path path(jsonPath);
	ParticleNode node;
	node.isEffectNode = true;
	node.name = path.stem().string() + "_Effect_" + std::to_string(particleNodes_.size() + 1);
	node.particleFileName = path.generic_string();
	node.startTime = timelineTime_;
	node.presetDuration = duration;
	node.endTime = node.startTime + duration;
	node.position = { 0.0f, 0.0f, 0.0f };
	node.hasEmitted = false;
	particleNodes_.push_back(std::move(node));
	selectedParticleNode_ = static_cast<int>(particleNodes_.size()) - 1;
	selectedEditorObject_ = -1;
	timelineDuration_ = std::max(timelineDuration_, particleNodes_.back().endTime);
	EffectManager::GetInstance()->LoadEffect(particleNodes_.back().name, particleNodes_.back().particleFileName);
}

void ParticleTestScene::DrawEffectEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
	ImGui::Begin("Effect Editor");

	ImGui::TextUnformatted("Timeline (Dope Sheet)");
	DrawDopeSheet_(app);

	ImGui::Separator();

	// タイムラインコントローラー
	if (ImGui::Button(timelinePlaying_ ? "Stop" : "Play")) {
		const bool startPlayback = !timelinePlaying_;
		timelinePlaying_ = startPlayback;
		if (startPlayback && editorMode_ == EditorMode::PlayerAttack && playerAttackEditorEnabled_) {
			const PlayerSpecialTimeline& specialTimeline = CurrentPlayerSpecialTimeline_();
			timelineDuration_ = std::max(0.05f, specialTimeline.totalSec);
			timelineTime_ = 0.0f;
			lastTimelineTime_ = -0.001f;
			livePreviewSpecialEdit_ = false;
			pendingTimelineRebuild_ = true;
			pendingTimelineRebuildTime_ = 0.0f;
		}
		if (startPlayback && timelineTime_ == 0.0f) {
			for (auto& node : particleNodes_) {
				node.hasEmitted = false;
			}
			lastTimelineTime_ = -1.0f;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart")) {
		timelineTime_ = 0.0f;
		lastTimelineTime_ = -0.001f;
		timelinePlaying_ = true;
		if (editorMode_ == EditorMode::PlayerAttack && playerAttackEditorEnabled_) {
			timelineDuration_ = std::max(0.05f, CurrentPlayerSpecialTimeline_().totalSec);
			livePreviewSpecialEdit_ = false;
		}
		pendingTimelineRebuild_ = true;
		pendingTimelineRebuildTime_ = 0.0f;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &timelineLoop_);

	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::DragFloat("Duration", &timelineDuration_, 0.05f, 0.05f, 30.0f, "%.2f s")) {
		if (timelineDuration_ < 0.05f) timelineDuration_ = 0.05f;
		timelineTime_ = std::clamp(timelineTime_, 0.0f, timelineDuration_);
		timelineViewDuration_ = std::clamp(timelineViewDuration_, std::min(0.05f, timelineDuration_), timelineDuration_);
		timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
		RequestTimelineRebuild_(timelineTime_);
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::SliderFloat("Current Time", &timelineTime_, 0.0f, timelineDuration_, "%.3f s")) {
		RequestTimelineRebuild_(timelineTime_);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(80.0f);
	if (ImGui::InputFloat("##timelineTimeInput", &timelineTime_, 0.01f, 0.1f, "%.3f")) {
		timelineTime_ = std::clamp(timelineTime_, 0.0f, timelineDuration_);
		RequestTimelineRebuild_(timelineTime_);
	}

	ImGui::SameLine();
	if (ImGui::Button("Add Particle Node")) {
		std::vector<std::string> groupNames;
		std::string fileName;
		if (OpenParticleFileDialog_(groupNames, fileName)) {
			PushUndoSnapshot_();
			ParticleManager::GetInstance()->LoadAdditional(fileName, "");

			std::filesystem::path fp(fileName);
			std::string stemName = fp.stem().string();

			auto* pm = ParticleManager::GetInstance();
			float maxLifeTime = 0.0f;
			for (const auto& groupName : groupNames) {
				maxLifeTime = std::max(maxLifeTime, pm->GetGroupLifeTimeMax(groupName));
			}
			if (maxLifeTime <= 0.0f) {
				maxLifeTime = 1.0f;
			}

			ParticleNode node;
			node.name = stemName + "_" + std::to_string(particleNodes_.size() + 1);
			node.particleFileName = fileName;
			node.startTime = 0.0f;
			node.presetDuration = maxLifeTime;
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			node.position = { 0.0f, 1.0f, 0.0f };
			particleNodes_.push_back(std::move(node));

			selectedParticleNode_ = static_cast<int>(particleNodes_.size()) - 1;
			selectedEditorObject_ = -1;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Effect Node")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(false, jsonPath)) {
			PushUndoSnapshot_();
			AddEffectReferenceNode_(jsonPath);
		}
	}

	ImGui::Separator();

	if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
		EditorObject& item = editorObjects_[selectedEditorObject_];
		ImGui::Text("Keyframes: %s (%s)", item.name.c_str(), item.modelPath.c_str());

		ImGui::SeparatorText("Apply Interpolation Range");
		if (item.keyframes.size() < 2) {
			ImGui::TextDisabled("Add at least two keyframes to create an interpolation segment.");
		} else {
			static int interpolationRangeStart = 0;
			static int interpolationRangeEnd = 1;
			static int interpolationRangeType = 0;
			const int lastKeyIndex = static_cast<int>(item.keyframes.size()) - 1;
			interpolationRangeStart = std::clamp(interpolationRangeStart, 0, lastKeyIndex - 1);
			interpolationRangeEnd = std::clamp(interpolationRangeEnd, interpolationRangeStart + 1, lastKeyIndex);
			if (ImGui::DragInt("From Key", &interpolationRangeStart, 0.1f, 0, lastKeyIndex - 1)) {
				interpolationRangeEnd = std::max(interpolationRangeEnd, interpolationRangeStart + 1);
			}
			if (ImGui::DragInt("To Key", &interpolationRangeEnd, 0.1f, interpolationRangeStart + 1, lastKeyIndex)) {
				interpolationRangeEnd = std::max(interpolationRangeEnd, interpolationRangeStart + 1);
			}
			const char* rangeInterpolationNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out" };
			ImGui::Combo("Range Interpolation", &interpolationRangeType,
				rangeInterpolationNames, IM_ARRAYSIZE(rangeInterpolationNames));
			ImGui::Text("Apply: Key %d (%.3fs) -> Key %d (%.3fs)",
				interpolationRangeStart, item.keyframes[interpolationRangeStart].time,
				interpolationRangeEnd, item.keyframes[interpolationRangeEnd].time);
			if (ImGui::Button("Apply To Key Range")) {
				PushUndoSnapshot_();
				// A key owns the interpolation segment leading to the next key,
				// so the end key itself is intentionally excluded.
				for (int keyIndex = interpolationRangeStart; keyIndex < interpolationRangeEnd; ++keyIndex) {
					item.keyframes[keyIndex].interpolationType = interpolationRangeType;
				}
				EvaluateTimeline_(false);
			}
			ImGui::SameLine();
			if (ImGui::Button("Apply Linear To Range")) {
				PushUndoSnapshot_();
				for (int keyIndex = interpolationRangeStart; keyIndex < interpolationRangeEnd; ++keyIndex) {
					item.keyframes[keyIndex].interpolationType = 0;
				}
				interpolationRangeType = 0;
				EvaluateTimeline_(false);
			}
			ImGui::TextDisabled("Each setting applies from a key to the following key.");
		}

		if (selectedKeyframeType_ == DragTarget::ModelKeyframe &&
			selectedKeyframeObjectIndex_ == selectedEditorObject_ &&
			selectedKeyframeIndex_ >= 0 &&
			selectedKeyframeIndex_ < static_cast<int>(item.keyframes.size())) {

			auto& key = item.keyframes[selectedKeyframeIndex_];
			ImGui::SeparatorText("Selected Keyframe Info");
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputFloat("Keyframe Time", &key.time, 0.01f, 0.1f, "%.3f")) {
				key.time = std::clamp(key.time, 0.0f, timelineDuration_);
				SortKeyframes_(item);
				EvaluateTimeline_(false);
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			const char* kInterpolationNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out" };
			if (ImGui::Combo("Interpolation To Next Key", &key.interpolationType, kInterpolationNames, IM_ARRAYSIZE(kInterpolationNames))) {
				EvaluateTimeline_(false);
			}
			if (selectedKeyframeIndex_ + 1 < static_cast<int>(item.keyframes.size())) {
				ImGui::Text("Segment: %.3fs -> %.3fs", key.time,
					item.keyframes[selectedKeyframeIndex_ + 1].time);
			} else {
				ImGui::TextDisabled("The last key has no following interpolation segment.");
			}

			const char* selectedChannelNames[] = { "All", "Position", "Rotation", "Scale", "Color" };
			ImGui::Text("Selected Channel: %s",
				selectedChannelNames[std::clamp(selectedModelKeyframeChannel_, 0, 4)]);
			bool keyValueChanged = false;
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 1) &&
				ImGui::CollapsingHeader("Position##SelectedModelKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				keyValueChanged |= ImGui::DragFloat3("Key Position", &key.position.x, 0.05f, -1000.0f, 1000.0f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 2) &&
				ImGui::CollapsingHeader("Rotation##SelectedModelKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				keyValueChanged |= ImGui::DragFloat3("Key Rotation", &key.rotation.x, 0.01f, -100.0f, 100.0f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 3) &&
				ImGui::CollapsingHeader("Scale##SelectedModelKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				keyValueChanged |= ImGui::DragFloat3("Key Scale", &key.scale.x, 0.05f, 0.001f, 1000.0f);
			}
			if ((selectedModelKeyframeChannel_ == 0 || selectedModelKeyframeChannel_ == 4) &&
				ImGui::CollapsingHeader("Color##SelectedModelKey", ImGuiTreeNodeFlags_DefaultOpen)) {
				keyValueChanged |= ImGui::ColorEdit4("Key Color", &key.color.x);
			}
			if (keyValueChanged) {
				EvaluateTimeline_(false);
			}
			ImGui::Separator();
		}

		if (ImGui::Button("Add / Replace Keyframe")) {
			PushUndoSnapshot_();
			AddKeyframeToSelected_();
		}
		ImGui::SameLine();
		const char* deleteBtnLabel = "Delete Near Keyframe";
		if (selectedKeyframeType_ == DragTarget::ModelKeyframe &&
			selectedKeyframeObjectIndex_ == selectedEditorObject_ &&
			selectedKeyframeIndex_ >= 0) {
			deleteBtnLabel = "Delete Selected Keyframe";
		}
		if (ImGui::Button(deleteBtnLabel)) {
			PushUndoSnapshot_();
			DeleteNearestKeyframeFromSelected_();
		}

		if (ImGui::BeginTable("KeyframesTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Position");
			ImGui::TableSetupColumn("Rotation");
			ImGui::TableSetupColumn("Scale");
			ImGui::TableSetupColumn("Color");
			ImGui::TableSetupColumn("To Next");
			ImGui::TableHeadersRow();
			for (size_t k = 0; k < item.keyframes.size(); ++k) {
				auto& key = item.keyframes[k];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				char timeId[64];
				sprintf_s(timeId, "##keytime_%zu", k);
				ImGui::SetNextItemWidth(70.0f);
				if (ImGui::DragFloat(timeId, &key.time, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
					SortKeyframes_(item);
					EvaluateTimeline_(false);
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.2f %.2f %.2f", key.position.x, key.position.y, key.position.z);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.2f %.2f %.2f", key.rotation.x, key.rotation.y, key.rotation.z);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%.2f %.2f %.2f", key.scale.x, key.scale.y, key.scale.z);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f %.2f %.2f %.2f", key.color.x, key.color.y, key.color.z, key.color.w);
				ImGui::TableSetColumnIndex(5);
				const char* shortInterpolationNames[] = { "Linear", "Ease In", "Ease Out", "Ease InOut" };
				ImGui::TextUnformatted(k + 1 < item.keyframes.size()
					? shortInterpolationNames[std::clamp(key.interpolationType, 0, 3)] : "-");
			}
			ImGui::EndTable();
		}
	} else if (selectedParticleNode_ >= 0 && selectedParticleNode_ < static_cast<int>(particleNodes_.size())) {
		ParticleNode& node = particleNodes_[selectedParticleNode_];
		ImGui::Text("Selected %s Node: %s", node.isEffectNode ? "Effect" : "Particle", node.name.c_str());

		char nameBuf[128];
		strncpy_s(nameBuf, sizeof(nameBuf), node.name.c_str(), _TRUNCATE);
		if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
			node.name = nameBuf;
		}

		char fileBuf[128];
		strncpy_s(fileBuf, sizeof(fileBuf), node.particleFileName.c_str(), _TRUNCATE);
		if (ImGui::InputText(node.isEffectNode ? "Effect File" : "Particle File", fileBuf, sizeof(fileBuf))) {
			node.particleFileName = fileBuf;
		}

		if (ImGui::DragFloat("Start Time", &node.startTime, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
			node.startTime = std::clamp(node.startTime, 0.0f, timelineDuration_);
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat("End Time", &node.endTime, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
			node.endTime = std::clamp(node.endTime, node.startTime + 0.01f, timelineDuration_);
			node.presetDuration = GetParticleNodeDuration_(node);
			node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat("Preset Duration", &node.presetDuration, 0.01f, 0.01f, 10.0f)) {
			node.endTime = node.startTime + GetParticleNodeDuration_(node);
			timelineDuration_ = std::max(timelineDuration_, node.endTime);
			RequestTimelineRebuild_(timelineTime_);
		}
		if (ImGui::DragFloat3("Position", &node.position.x, 0.05f)) {
			RequestTimelineRebuild_(timelineTime_);
		}
		if (!node.isEffectNode) {
			ImGui::DragFloat3("Rotation", &node.rotation.x, 0.01f);
			ImGui::DragFloat3("Scale", &node.scale.x, 0.05f);
			ImGui::DragInt("Emit Count", &node.emitCount, 1, 1, 1000);
		}

		if (ImGui::Button(node.isEffectNode ? "Delete Effect Node" : "Delete Particle Node")) {
			PushUndoSnapshot_();
			particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
			selectedParticleNode_ = -1;
		}
	} else {
		ImGui::TextDisabled("Select an object or particle node in Hierarchy, Dope Sheet, or Inspector.");
	}

	ImGui::End();
#endif
}

void ParticleTestScene::DrawPlayerAttackEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
	ImGui::Begin("PlayerAttack Editor");

	if (ImGui::Button("Create / Focus PlayerAttack Editor")) {
		EnsurePlayerAttackEditor_(app);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Draw HitBox", &drawPlayerAttackHitbox_);

	if (!playerAttackEditorEnabled_) {
		ImGui::TextDisabled("Create the editor to place a Player model and HitBox track.");
		ImGui::End();
		return;
	}

	ImGui::Text("Timeline: %.3f / %.3f", timelineTime_, timelineDuration_);
	if (timelinePlaying_) {
		ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "PLAY MODE: Player is visible");
	} else {
		ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f), "PATH EDIT MODE: Player is hidden");
	}
	if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
		ImGui::Text("Player Object: %s", editorObjects_[playerAttackObjectIndex_].name.c_str());
		if (ImGui::Button("Select Player Object")) {
			selectedEditorObject_ = playerAttackObjectIndex_;
			selectedParticleNode_ = -1;
		}
	}

	if (ImGui::CollapsingHeader("Legacy HitBox Editor")) {
		ImGui::TextDisabled("Legacy generic hitbox track. I attacks use the sections below.");
		ImGui::SeparatorText("Current HitBox");
		bool changed = false;
		changed |= ImGui::Checkbox("Active", &currentPlayerAttackHitbox_.active);
		changed |= ImGui::DragFloat3("Offset", &currentPlayerAttackHitbox_.offset.x, 0.05f, -20.0f, 20.0f);
		changed |= ImGui::DragFloat3("Half Size", &currentPlayerAttackHitbox_.halfSize.x, 0.05f, 0.01f, 20.0f);
		if (changed && playerAttackHitboxCube_) {
			previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
			Vector3 playerBase{};
			if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
				playerBase = editorObjects_[playerAttackObjectIndex_].position;
			}
			playerAttackHitboxCube_->SetTranslate(playerBase + previewPlayerAttackHitbox_.offset);
			playerAttackHitboxCube_->SetScale(previewPlayerAttackHitbox_.halfSize);
		}

		if (ImGui::Button("Add / Replace HitBox Keyframe")) {
			AddPlayerAttackHitboxKeyframe_();
			EvaluateTimeline_(false);
		}
		ImGui::SameLine();
		const char* deleteHitboxBtnLabel = "Delete Near HitBox Keyframe";
		if (selectedKeyframeType_ == DragTarget::PlayerAttackHitboxKeyframe && selectedKeyframeIndex_ >= 0) {
			deleteHitboxBtnLabel = "Delete Selected HitBox Keyframe";
		}
		if (ImGui::Button(deleteHitboxBtnLabel)) {
			DeleteNearestPlayerAttackHitboxKeyframe_();
			EvaluateTimeline_(false);
		}

		if (selectedKeyframeType_ == DragTarget::PlayerAttackHitboxKeyframe &&
			selectedKeyframeIndex_ >= 0 &&
			selectedKeyframeIndex_ < static_cast<int>(playerAttackHitboxKeyframes_.size())) {

			auto& key = playerAttackHitboxKeyframes_[selectedKeyframeIndex_];
			ImGui::SeparatorText("Selected HitBox Keyframe");
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputFloat("HitBox Key Time", &key.time, 0.01f, 0.1f, "%.3f")) {
				key.time = std::clamp(key.time, 0.0f, timelineDuration_);
				SortPlayerAttackHitboxKeyframes_();
				EvaluateTimeline_(false);
			}
		}

		if (ImGui::BeginTable("PlayerAttackHitboxKeys", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn("Offset");
			ImGui::TableSetupColumn("Half Size");
			ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableHeadersRow();

			for (int i = 0; i < static_cast<int>(playerAttackHitboxKeyframes_.size()); ++i) {
				auto& key = playerAttackHitboxKeyframes_[i];
				ImGui::PushID(i);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::SetNextItemWidth(64.0f);
				if (ImGui::DragFloat("##time", &key.time, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
					SortPlayerAttackHitboxKeyframes_();
					EvaluateTimeline_(false);
				}
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Checkbox("##active", &key.active)) {
					EvaluateTimeline_(false);
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.2f %.2f %.2f", key.offset.x, key.offset.y, key.offset.z);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%.2f %.2f %.2f", key.halfSize.x, key.halfSize.y, key.halfSize.z);
				ImGui::TableSetColumnIndex(4);
				if (ImGui::Button("Set")) {
					currentPlayerAttackHitbox_ = key;
					previewPlayerAttackHitbox_ = currentPlayerAttackHitbox_;
					timelineTime_ = key.time;
					RequestTimelineRebuild_(timelineTime_);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	ImGui::SeparatorText("Bone Pose");
	ImGui::TextDisabled("Use Inspector > Bone Controls and the viewport bone handles for the selected Player object.");

	ImGui::SeparatorText("I Attack Timeline");
	EnsurePlayerSpecialTimelineDefaults_();
	const char* levelLabels[] = { "Lv0", "Lv1", "Lv2", "Lv3" };
	if (ImGui::Combo("Side I Lv", &selectedPlayerSpecialLevel_, levelLabels, IM_ARRAYSIZE(levelLabels))) {
		selectedPlayerSpecialPositionKey_ = -1;
		SyncPlayerSpecialPreviewNodes_();
		EvaluatePlayerSpecialTimeline_();
	}

	PlayerSpecialTimeline& specialTimeline = CurrentPlayerSpecialTimeline_();
	ImGui::Text("Timeline Name: %s", specialTimeline.name.c_str());
	ImGui::Text("Movement Points: %d", static_cast<int>(specialTimeline.positionKeyframes.size()));
	ImGui::Checkbox("Draw Movement Path##Main", &drawPlayerSpecialPath_);
	ImGui::SameLine();
	if (ImGui::Button("Focus Movement Path##Main")) {
		FocusPlayerSpecialPathCamera_();
	}
	ImGui::TextDisabled("Cyan points are movement keys. Drag a point in the Scene view.");
	if (ImGui::TreeNodeEx("Boss Target Dummy", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Match Test Scene Layout##Main", &matchTestSceneLayout_);
		if (matchTestSceneLayout_) {
			ImGui::TextDisabled("Player (-12, 0, 5) / Boss (0, 0, 5)");
			ImGui::TextDisabled("Uses the same spawn layout as TestScene.");
		}
		ImGui::Checkbox("Show Boss Dummy##Main", &showBossDummy_);
		ImGui::SameLine();
		ImGui::Checkbox("Show Body HitBox##Main", &showBossDummyHitbox_);
		if (matchTestSceneLayout_) ImGui::BeginDisabled();
		ImGui::DragFloat("Boss X (Horizontal)##Main", &bossDummyPosition_.x, 0.05f, -100.0f, 100.0f, "%.2f");
		ImGui::DragFloat("Boss Y (Feet Height)##Main", &bossDummyPosition_.y, 0.05f, -100.0f, 100.0f, "%.2f");
		ImGui::DragFloat("Boss Z (Depth)##Main", &bossDummyPosition_.z, 0.05f, -100.0f, 100.0f, "%.2f");
		if (ImGui::Button("Reset Boss Position##Main")) bossDummyPosition_ = { 6.0f, 0.0f, 0.0f };
		if (matchTestSceneLayout_) ImGui::EndDisabled();
		ImGui::TextDisabled("The target center is %.2f units above the feet.", bossDummyHalfSize_.y);
		ImGui::TreePop();
	}
	if (ImGui::Checkbox("Live Edit Preview", &livePreviewSpecialEdit_)) {
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("ON: preview unsaved HitBox/Position values. OFF: preview registered timeline keys.");
	}
	if (ImGui::DragFloat("Special Total Sec", &specialTimeline.totalSec, 0.01f, 0.0f, 0.0f, "%.2f")) {
		specialTimeline.totalSec = std::max(0.05f, specialTimeline.totalSec);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::Button("Use Special Duration")) {
		timelineDuration_ = specialTimeline.totalSec;
		RequestTimelineRebuild_(std::min(timelineTime_, timelineDuration_));
	}
	ImGui::Checkbox("Freeze Boss During Attack##Main", &specialTimeline.freezeBossDuringAttack);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Stops the target boss for this entire special attack and releases it when the attack ends.");
	}

	ImGui::SeparatorText("Special HitBox Key");
	bool specialHitboxChanged = false;
	if (ImGui::DragFloat("Hit Time", &currentSpecialHitbox_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialHitbox_.time = std::max(0.0f, currentSpecialHitbox_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialHitbox_.time + currentSpecialHitbox_.duration);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
		specialHitboxChanged = true;
	}
	specialHitboxChanged |= ImGui::DragFloat("Hit Duration", &currentSpecialHitbox_.duration, 0.01f, 0.01f, 2.0f, "%.2f");
	specialHitboxChanged |= ImGui::DragFloat("Hit Stop Sec", &currentSpecialHitbox_.hitStopSec, 0.005f, 0.0f, 1.0f, "%.3f");
	specialHitboxChanged |= ImGui::Checkbox("Hit Active", &currentSpecialHitbox_.active);
	specialHitboxChanged |= ImGui::Checkbox("Multi Hit", &currentSpecialHitbox_.multiHit);
	specialHitboxChanged |= ImGui::Checkbox("Follow Player Movement", &currentSpecialHitbox_.followPlayerMovement);
	specialHitboxChanged |= ImGui::DragInt("Damage", &currentSpecialHitbox_.damage, 1, 0, 999);
	specialHitboxChanged |= ImGui::DragFloat3("Special Offset", &currentSpecialHitbox_.offset.x, 0.05f, -20.0f, 20.0f);
	specialHitboxChanged |= ImGui::DragFloat3("Special Half Size", &currentSpecialHitbox_.halfSize.x, 0.05f, 0.01f, 20.0f);
	if (specialHitboxChanged) {
		livePreviewSpecialEdit_ = true;
		previewPlayerAttackHitbox_.time = currentSpecialHitbox_.time;
		previewPlayerAttackHitbox_.offset = currentSpecialHitbox_.offset;
		previewPlayerAttackHitbox_.halfSize = currentSpecialHitbox_.halfSize;
		previewPlayerAttackHitbox_.active = currentSpecialHitbox_.active;
		previewPlayerAttackHitbox_.followPlayerMovement = currentSpecialHitbox_.followPlayerMovement;
	}
	if (ImGui::Button("Add / Replace Special HitBox")) {
		bool replaced = false;
		for (auto& key : specialTimeline.hitboxes) {
			if (std::abs(key.time - currentSpecialHitbox_.time) < 0.001f) {
				key = currentSpecialHitbox_;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			specialTimeline.hitboxes.push_back(currentSpecialHitbox_);
		}
		SortCurrentPlayerSpecialTimeline_();
		EvaluatePlayerSpecialTimeline_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete Near Special HitBox")) {
		auto& keys = specialTimeline.hitboxes;
		auto it = std::min_element(keys.begin(), keys.end(), [this](const PlayerSpecialHitboxKeyframe& a, const PlayerSpecialHitboxKeyframe& b) {
			return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
			});
		if (it != keys.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
			keys.erase(it);
		}
		EvaluatePlayerSpecialTimeline_();
	}

	if (ImGui::BeginTable("SideSpecialHitboxKeys", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Dur", ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableSetupColumn("Act", ImGuiTableColumnFlags_WidthFixed, 38.0f);
		ImGui::TableSetupColumn("Multi", ImGuiTableColumnFlags_WidthFixed, 45.0f);
		ImGui::TableSetupColumn("Dmg", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("Stop", ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableSetupColumn("Offset / Half");
		ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(specialTimeline.hitboxes.size()); ++i) {
			auto& key = specialTimeline.hitboxes[i];
			ImGui::PushID(("special_hit_" + std::to_string(i)).c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::SetNextItemWidth(54.0f);
			if (ImGui::DragFloat("##time", &key.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
				key.time = std::max(0.0f, key.time);
				specialTimeline.totalSec = std::max(specialTimeline.totalSec, key.time + key.duration);
				timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
				SortCurrentPlayerSpecialTimeline_();
				EvaluatePlayerSpecialTimeline_();
			}
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(50.0f);
			ImGui::DragFloat("##dur", &key.duration, 0.01f, 0.01f, 2.0f, "%.2f");
			ImGui::TableSetColumnIndex(2);
			ImGui::Checkbox("##active", &key.active);
			ImGui::TableSetColumnIndex(3);
			ImGui::Checkbox("##multi", &key.multiHit);
			ImGui::TableSetColumnIndex(4);
			ImGui::SetNextItemWidth(44.0f);
			ImGui::DragInt("##damage", &key.damage, 1, 0, 999);
			ImGui::TableSetColumnIndex(5);
			ImGui::SetNextItemWidth(50.0f);
			ImGui::DragFloat("##hitStop", &key.hitStopSec, 0.005f, 0.0f, 1.0f, "%.3f");
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("O %.2f %.2f %.2f / H %.2f %.2f %.2f", key.offset.x, key.offset.y, key.offset.z, key.halfSize.x, key.halfSize.y, key.halfSize.z);
			ImGui::TableSetColumnIndex(7);
			if (ImGui::Button("Set")) {
				currentSpecialHitbox_ = key;
				timelineTime_ = key.time;
				RequestTimelineRebuild_(timelineTime_);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Motion Keys");
	if (ImGui::DragFloat("Motion Time", &currentSpecialMotion_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialMotion_.time = std::max(0.0f, currentSpecialMotion_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialMotion_.time + currentSpecialMotion_.duration);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	ImGui::DragFloat("Motion Duration", &currentSpecialMotion_.duration, 0.01f, 0.01f, 2.0f, "%.2f");
	ImGui::DragFloat3("Velocity", &currentSpecialMotion_.velocity.x, 0.05f, -100.0f, 100.0f);
	ImGui::Checkbox("Lock Velocity", &currentSpecialMotion_.lockVelocity);
	if (ImGui::Button("Add Motion Key")) {
		specialTimeline.motions.push_back(currentSpecialMotion_);
		SortCurrentPlayerSpecialTimeline_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete Near Motion")) {
		auto& keys = specialTimeline.motions;
		auto it = std::min_element(keys.begin(), keys.end(), [this](const PlayerSpecialMotionKeyframe& a, const PlayerSpecialMotionKeyframe& b) {
			return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
			});
		if (it != keys.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
			keys.erase(it);
		}
	}

	if (ImGui::BeginTable("SideSpecialMotionKeys", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Dur", ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableSetupColumn("Velocity");
		ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 42.0f);
		ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(specialTimeline.motions.size()); ++i) {
			auto& key = specialTimeline.motions[i];
			ImGui::PushID(("special_motion_" + std::to_string(i)).c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f", key.duration);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.2f %.2f %.2f", key.velocity.x, key.velocity.y, key.velocity.z);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(key.lockVelocity ? "Yes" : "No");
			ImGui::TableSetColumnIndex(4);
			if (ImGui::Button("Set")) {
				currentSpecialMotion_ = key;
				timelineTime_ = key.time;
				RequestTimelineRebuild_(timelineTime_);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
#endif
}

void ParticleTestScene::DrawPlayerAttackInspectorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
	ImGui::Begin("Inspector");

	ImGui::TextUnformatted("PlayerAttack Inspector");
	if (ImGui::Button("Create / Focus PlayerAttack Editor")) {
		EnsurePlayerAttackEditor_(app);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Draw HitBox", &drawPlayerAttackHitbox_);

	ImGui::InputText("Editor JSON", effectJsonPath_, sizeof(effectJsonPath_));
	if (ImGui::Button("Save PlayerAttack JSON")) {
		const std::string savePath = MakeEffectsJsonPath_(effectJsonPath_);
		strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), savePath.c_str(), _TRUNCATE);
		SaveEffectJson_(effectJsonPath_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save As...##PlayerAttackJson")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(true, jsonPath)) {
			strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), jsonPath.c_str(), _TRUNCATE);
			SaveEffectJson_(effectJsonPath_);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Open...##PlayerAttackJson")) {
		std::string jsonPath;
		if (OpenEffectJsonFileDialog_(false, jsonPath)) {
			strncpy_s(effectJsonPath_, sizeof(effectJsonPath_), jsonPath.c_str(), _TRUNCATE);
			PushUndoSnapshot_();
			LoadEffectJson_(app, effectJsonPath_);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Load PlayerAttack JSON")) {
		PushUndoSnapshot_();
		LoadEffectJson_(app, effectJsonPath_);
	}

	if (!playerAttackEditorEnabled_) {
		ImGui::Separator();
		ImGui::TextDisabled("Create the PlayerAttack editor first.");
		ImGui::End();
		return;
	}

	static int playerAttackInspectorPage = 1;
	if (ImGui::BeginTabBar("PlayerAttackInspectorTabs")) {
		if (ImGui::BeginTabItem("HitBox")) { playerAttackInspectorPage = 0; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Trajectory")) { playerAttackInspectorPage = 1; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Appearance")) { playerAttackInspectorPage = 2; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Effects")) { playerAttackInspectorPage = 3; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Player")) { playerAttackInspectorPage = 4; ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}

	if (playerAttackInspectorPage == 4) {
	ImGui::SeparatorText("Player Object");
	if (ImGui::Checkbox("Game Camera Preview", &useGameCameraPreview_)) {
		ApplyCameraToEditorObjects_();
	}
	ImGui::TextDisabled(useGameCameraPreview_
		? "Using the same framing calculation as TestScene."
		: "Using the free editor camera.");
	ImGui::TextDisabled(timelinePlaying_ ? "Player is shown during playback." : "Player is hidden until Play. Edit the cyan movement points in Scene.");
	if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
		EditorObject& player = editorObjects_[playerAttackObjectIndex_];
		if (ImGui::Selectable(player.name.c_str(), selectedEditorObject_ == playerAttackObjectIndex_)) {
			selectedEditorObject_ = playerAttackObjectIndex_;
			selectedParticleNode_ = -1;
		}
		ImGui::Text("Model: %s", player.modelPath.c_str());

		bool changed = false;
		if (matchTestSceneLayout_) ImGui::BeginDisabled();
		bool basePositionChanged = ImGui::DragFloat3("Player Base Position", &playerSpecialPreviewOrigin_.x, 0.05f);
		if (matchTestSceneLayout_) ImGui::EndDisabled();
		bool baseRotationChanged = ImGui::DragFloat3("Base Rotation", &player.rotation.x, 0.01f);
		changed |= baseRotationChanged;
		changed |= ImGui::DragFloat3("Scale", &player.scale.x, 0.05f, 0.01f, 100.0f);
		if (basePositionChanged) {
			playerSpecialPreviewOriginInitialized_ = true;
			ApplyPlayerSpecialPreviewPosition_();
		}
		if (baseRotationChanged) {
			playerSpecialPreviewBaseRotation_ = player.rotation;
		}
		if (changed) {
			ApplyEditorObjectTransform_(player);
		}

		const Vector3 positionBeforeGizmo = player.position;
		DrawGizmoControls_(player);
		const Vector3 gizmoDelta = player.position - positionBeforeGizmo;
		if (!matchTestSceneLayout_ &&
			(std::abs(gizmoDelta.x) > 0.0001f || std::abs(gizmoDelta.y) > 0.0001f || std::abs(gizmoDelta.z) > 0.0001f)) {
			playerSpecialPreviewOrigin_ += gizmoDelta;
			playerSpecialPreviewOriginInitialized_ = true;
			ApplyPlayerSpecialPreviewPosition_();
		} else if (matchTestSceneLayout_) {
			ApplyPlayerSpecialPreviewPosition_();
		}
		DrawBoneControls_(player);
	} else {
		ImGui::TextDisabled("PlayerAttack player object is not available.");
	}
	}

	// 攻撃タイプのラベル
	const char* attackTypeLabels[] = {
		"Neutral I",
		"Side I",
		"Up I",
		"Down I",
	};

	int selectedAttackIndex =
		static_cast<int>(selectedPlayerSpecialAttackType_);

	ImGui::SeparatorText("I Attack Type");
	EnsurePlayerSpecialTimelineDefaults_();

	if (ImGui::Combo(
		"I Attack Type",
		&selectedAttackIndex,
		attackTypeLabels,
		IM_ARRAYSIZE(attackTypeLabels))) {

		// 整数を列挙型に戻す
		selectedPlayerSpecialAttackType_ =
			static_cast<PlayerSpecialAttackType>(selectedAttackIndex);

		selectedPlayerSpecialPositionKey_ = -1;
		SyncPlayerSpecialPreviewNodes_();
		EvaluatePlayerSpecialTimeline_();
	}

	//json保存ボタン
	if (ImGui::Button("Save All I Attacks")) {

		const bool succeeded =
			SavePlayerSpecialTimelinesJson_(
				"resources/Data/PlayerIAttacks.json"
			);

		if (succeeded) {

			playerSpecialJsonStatus_ =
				"Saved: resources/Data/PlayerIAttacks.json";
		} else {
			playerSpecialJsonStatus_ =
				"Failed to save I Attack JSON";
		}
	}

	const char* saveAttackNames[] = {
		"NeutralSpecial", "SideSpecial", "UpSpecial", "DownSpecial"
	};
	const std::string selectedAttackName = saveAttackNames[std::clamp(selectedAttackIndex, 0, 3)];
	if (ImGui::Button("Save Current Attack (All Lv)")) {
		const std::string savePath = "resources/Data/PlayerIAttacks/" + selectedAttackName + ".json";
		const bool succeeded = SavePlayerSpecialTimelinesJson_(savePath, selectedAttackIndex, -1);
		playerSpecialJsonStatus_ = succeeded ? "Saved: " + savePath : "Failed to save: " + savePath;
	}
	if (ImGui::Button("Save Current Attack + Level")) {
		const int selectedLevel = std::clamp(selectedPlayerSpecialLevel_, 0, 3);
		const std::string savePath = "resources/Data/PlayerIAttacks/" + selectedAttackName + "_Lv" +
			std::to_string(selectedLevel) + ".json";
		const bool succeeded = SavePlayerSpecialTimelinesJson_(savePath, selectedAttackIndex, selectedLevel);
		playerSpecialJsonStatus_ = succeeded ? "Saved: " + savePath : "Failed to save: " + savePath;
	}
	if (ImGui::Button("Load I Attack JSON")) {
		const bool succeeded = LoadPlayerSpecialTimelinesJson_("resources/Data/PlayerIAttacks.json");
		playerSpecialJsonStatus_ = succeeded
			? "Loaded: resources/Data/PlayerIAttacks.json"
			: "Failed to load I Attack JSON";
	}
	if (ImGui::Button("Open I Attack JSON...")) {
		std::string selectedPath;
		if (OpenPlayerSpecialJsonFileDialog_(selectedPath)) {
			const bool succeeded = LoadPlayerSpecialTimelinesJson_(selectedPath);
			playerSpecialJsonStatus_ = succeeded ? "Loaded: " + selectedPath : "Failed to load: " + selectedPath;
		}
	}

	//結果表示
	if (!playerSpecialJsonStatus_.empty()) {
		ImGui::TextUnformatted(
			playerSpecialJsonStatus_.c_str()
		);

	}


	PlayerSpecialTimeline& specialTimeline = CurrentPlayerSpecialTimeline_();
	ImGui::Text("Timeline: %s", specialTimeline.name.c_str());
	if (ImGui::Checkbox("Live Edit Preview", &livePreviewSpecialEdit_)) {
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("ON: preview unsaved HitBox/Position values. OFF: preview registered timeline keys.");
	}
	if (ImGui::DragFloat("Total Sec", &specialTimeline.totalSec, 0.01f, 0.0f, 0.0f, "%.2f")) {
		specialTimeline.totalSec = std::max(0.05f, specialTimeline.totalSec);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::Button("Use Special Duration")) {
		timelineDuration_ = specialTimeline.totalSec;
		RequestTimelineRebuild_(std::min(timelineTime_, timelineDuration_));
	}
	ImGui::Checkbox("Freeze Boss During Attack##Inspector", &specialTimeline.freezeBossDuringAttack);

	if (playerAttackInspectorPage == 0) {
	ImGui::SeparatorText("Current Special HitBox");
	bool hitboxChanged = false;
	if (ImGui::DragFloat("Hit Time", &currentSpecialHitbox_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialHitbox_.time = std::max(0.0f, currentSpecialHitbox_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialHitbox_.time + currentSpecialHitbox_.duration);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
		hitboxChanged = true;
	}
	hitboxChanged |= ImGui::DragFloat("Hit Duration", &currentSpecialHitbox_.duration, 0.01f, 0.01f, 2.0f, "%.2f");
	hitboxChanged |= ImGui::DragFloat("Hit Stop Sec", &currentSpecialHitbox_.hitStopSec, 0.005f, 0.0f, 1.0f, "%.3f");
	hitboxChanged |= ImGui::Checkbox("Hit Active", &currentSpecialHitbox_.active);
	hitboxChanged |= ImGui::DragInt("Damage", &currentSpecialHitbox_.damage, 1, 0, 999);
	hitboxChanged |= ImGui::DragFloat3("Offset", &currentSpecialHitbox_.offset.x, 0.05f, -20.0f, 20.0f);
	hitboxChanged |= ImGui::DragFloat3("Half Size", &currentSpecialHitbox_.halfSize.x, 0.05f, 0.01f, 20.0f);
	hitboxChanged |= ImGui::Checkbox("Multi Hit", &currentSpecialHitbox_.multiHit);
	hitboxChanged |= ImGui::Checkbox("Follow Player Movement", &currentSpecialHitbox_.followPlayerMovement);
	if (hitboxChanged) {
		livePreviewSpecialEdit_ = true;
		previewPlayerAttackHitbox_.time = currentSpecialHitbox_.time;
		previewPlayerAttackHitbox_.offset = currentSpecialHitbox_.offset;
		previewPlayerAttackHitbox_.halfSize = currentSpecialHitbox_.halfSize;
		previewPlayerAttackHitbox_.active = currentSpecialHitbox_.active;
		previewPlayerAttackHitbox_.followPlayerMovement = currentSpecialHitbox_.followPlayerMovement;
		EvaluatePlayerSpecialTimeline_();
	}

	if (ImGui::Button("Add / Replace HitBox Key")) {
		bool replaced = false;

		for (PlayerSpecialHitboxKeyframe& key
			: specialTimeline.hitboxes) {

			if (std::abs(
				key.time - currentSpecialHitbox_.time
			) < 0.001f) {

				key = currentSpecialHitbox_;
				replaced = true;
				break;
			}
		}

		if (!replaced) {
			specialTimeline.hitboxes.push_back(
				currentSpecialHitbox_
			);
		}

		SortCurrentPlayerSpecialTimeline_();
		EvaluatePlayerSpecialTimeline_();
	}

	ImGui::Text(
		"Registered HitBoxes: %d",
		static_cast<int>(specialTimeline.hitboxes.size())
	);
	}

	if (playerAttackInspectorPage == 1) {
	ImGui::SeparatorText("Boss Target Dummy");
	ImGui::Checkbox("Match Test Scene Layout", &matchTestSceneLayout_);
	if (matchTestSceneLayout_) {
		ImGui::TextDisabled("Player Start: -12, 0, 5 / Boss Feet: 0, 0, 5");
	}
	ImGui::Checkbox("Show Boss Dummy", &showBossDummy_);
	ImGui::SameLine();
	ImGui::Checkbox("Show Body HitBox", &showBossDummyHitbox_);
	if (matchTestSceneLayout_) ImGui::BeginDisabled();
	ImGui::DragFloat("Boss X (Horizontal)", &bossDummyPosition_.x, 0.05f, -100.0f, 100.0f, "%.2f");
	ImGui::DragFloat("Boss Y (Feet Height)", &bossDummyPosition_.y, 0.05f, -100.0f, 100.0f, "%.2f");
	ImGui::DragFloat("Boss Z (Depth)", &bossDummyPosition_.z, 0.05f, -100.0f, 100.0f, "%.2f");
	if (ImGui::Button("Reset Boss Position")) bossDummyPosition_ = { 6.0f, 0.0f, 0.0f };
	if (matchTestSceneLayout_) ImGui::EndDisabled();
	ImGui::DragFloat3("Boss Body Half Size", &bossDummyHalfSize_.x, 0.05f, 0.05f, 20.0f);
	const Vector3 bossTargetCenter{
		bossDummyPosition_.x,
		bossDummyPosition_.y + bossDummyHalfSize_.y,
		bossDummyPosition_.z
	};
	ImGui::TextDisabled("Boss Target Center: %.2f, %.2f, %.2f",
		bossTargetCenter.x, bossTargetCenter.y, bossTargetCenter.z);
	ImGui::TextDisabled("Boss-relative keys use this body center as (0, 0, 0).");

	ImGui::SeparatorText("Current Motion");
	if (ImGui::DragFloat("Motion Time", &currentSpecialMotion_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialMotion_.time = std::max(0.0f, currentSpecialMotion_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialMotion_.time + currentSpecialMotion_.duration);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	ImGui::DragFloat("Motion Duration", &currentSpecialMotion_.duration, 0.01f, 0.01f, 2.0f, "%.2f");
	ImGui::DragFloat3("Velocity", &currentSpecialMotion_.velocity.x, 0.05f, -100.0f, 100.0f);
	ImGui::Checkbox("Lock Velocity", &currentSpecialMotion_.lockVelocity);

	ImGui::SeparatorText("Current Position Keyframe");

	bool positionChanged = false;
	positionChanged |= ImGui::DragFloat(
		"Position Time",
		&currentSpecialPosition_.time,
		0.01f,
		0.0f,
		0.0f,
		"%.2f"
	);

	positionChanged |= ImGui::DragFloat3(
		"Position Offset",
		&currentSpecialPosition_.offset.x,
		0.05f,
		-20.0f,
		20.0f
	);
	int positionInterpolation = static_cast<int>(currentSpecialPosition_.interpolation);
	const char* positionInterpolationNames[] = {
		"Linear", "Ease In", "Ease Out", "Ease In Out", "Step (instant)"
	};
	if (ImGui::Combo("Easing To Next Point", &positionInterpolation,
		positionInterpolationNames, IM_ARRAYSIZE(positionInterpolationNames))) {
		currentSpecialPosition_.interpolation =
			static_cast<ParticleTestEditor::PlayerSpecialPositionInterpolation>(positionInterpolation);
		positionChanged = true;
	}
	int positionSpace = static_cast<int>(currentSpecialPosition_.space);
	const char* positionSpaceNames[] = { "Player Start", "Boss Target" };
	if (ImGui::Combo("Position Base", &positionSpace,
		positionSpaceNames, IM_ARRAYSIZE(positionSpaceNames))) {
		currentSpecialPosition_.space =
			static_cast<ParticleTestEditor::PlayerSpecialPositionSpace>(positionSpace);
		positionChanged = true;
	}
	if (currentSpecialPosition_.space == ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget) {
		ImGui::TextDisabled("Offset is measured from the boss body center.");
	}
	if (ImGui::Checkbox("Advance To Next Point On Hit", &currentSpecialPosition_.advanceOnHit)) {
		positionChanged = true;
	}
	ImGui::TextDisabled("When enabled, the player waits here until the current attack confirms a hit.");

	// Speed and time express the same segment in two different ways. Editing the
	// speed recalculates this key's arrival time from the preceding registered key.
	if (selectedPlayerSpecialPositionKey_ > 0 &&
		selectedPlayerSpecialPositionKey_ < static_cast<int>(specialTimeline.positionKeyframes.size())) {
		const auto& previous = specialTimeline.positionKeyframes[selectedPlayerSpecialPositionKey_ - 1];
		const Vector3 delta = ResolvePlayerSpecialPositionOffset_(currentSpecialPosition_) -
			ResolvePlayerSpecialPositionOffset_(previous);
		const float distance = LengthVector3(delta);
		const float duration = std::max(0.001f, currentSpecialPosition_.time - previous.time);
		float segmentSpeed = distance / duration;
		if (ImGui::DragFloat("Speed From Previous Point", &segmentSpeed, 0.1f, 0.01f, 200.0f, "%.2f units/s")) {
			currentSpecialPosition_.time = previous.time + distance / std::max(0.01f, segmentSpeed);
			currentSpecialPosition_.time = std::max(0.0f, currentSpecialPosition_.time);
			specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialPosition_.time);
			timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
			positionChanged = true;
		}
		ImGui::TextDisabled("Distance %.2f / Duration %.2fs", distance, duration);
	} else {
		ImGui::TextDisabled("Select the second or later point to edit segment speed.");
	}
	if (positionChanged) {
		currentSpecialPosition_.time = std::max(0.0f, currentSpecialPosition_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialPosition_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
		livePreviewSpecialEdit_ = true;
		EvaluatePlayerSpecialTimeline_();
	}

	if (ImGui::Button("Add New Position Key")) {
		// A timeline cannot contain two position keys at exactly the same time.
		// Only an exact-time duplicate is replaced; a different time always adds a point.
		bool sameTimeKeyFound = false;
		for (auto& key : specialTimeline.positionKeyframes) {
			if (std::abs(key.time - currentSpecialPosition_.time) < 0.001f) {
				key = currentSpecialPosition_;
				sameTimeKeyFound = true;
				break;
			}
		}
		if (!sameTimeKeyFound) {
			specialTimeline.positionKeyframes.push_back(currentSpecialPosition_);
		}
		SortCurrentPlayerSpecialTimeline_();
		// Return to new-key mode immediately so the next click adds another point.
		selectedPlayerSpecialPositionKey_ = -1;
		currentSpecialPosition_.time += 0.10f;
		EvaluatePlayerSpecialTimeline_();
	}
	ImGui::SameLine();
	const bool canUpdatePositionKey =
		selectedPlayerSpecialPositionKey_ >= 0 &&
		selectedPlayerSpecialPositionKey_ < static_cast<int>(specialTimeline.positionKeyframes.size());
	if (!canUpdatePositionKey) ImGui::BeginDisabled();
	if (ImGui::Button("Update Selected Key") && canUpdatePositionKey) {
		specialTimeline.positionKeyframes[selectedPlayerSpecialPositionKey_] = currentSpecialPosition_;
		SortCurrentPlayerSpecialTimeline_();
		selectedPlayerSpecialPositionKey_ = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.positionKeyframes.size()); ++i) {
			if (std::abs(specialTimeline.positionKeyframes[i].time - currentSpecialPosition_.time) < 0.001f) {
				selectedPlayerSpecialPositionKey_ = i;
				break;
			}
		}
		EvaluatePlayerSpecialTimeline_();
	}
	if (!canUpdatePositionKey) ImGui::EndDisabled();
	ImGui::TextDisabled("Add New always creates another point when Position Time is different.");

	//登録した位置を一覧表示する
	if (ImGui::BeginTable(
		"PositionKeyTable",
		9,
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg)) {

		// 列を設定する
		ImGui::TableSetupColumn("Time");
		ImGui::TableSetupColumn("X");
		ImGui::TableSetupColumn("Y");
		ImGui::TableSetupColumn("Z");
		ImGui::TableSetupColumn("Base");
		ImGui::TableSetupColumn("Ease");
		ImGui::TableSetupColumn("Speed");
		ImGui::TableSetupColumn("Hit Next");
		ImGui::TableSetupColumn("Delete");
		ImGui::TableHeadersRow();

		int deletePositionIndex = -1;

		for (size_t i = 0; i < specialTimeline.positionKeyframes.size(); ++i) {


			// 位置キーフレームを順番に表示する
			const PlayerSpecialPositionKeyframe& key =
				specialTimeline.positionKeyframes[i];

			ImGui::TableNextRow();

			// Time列
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable("##SelectPositionKey", selectedPlayerSpecialPositionKey_ == static_cast<int>(i),
				ImGuiSelectableFlags_SpanAllColumns)) {
				selectedPlayerSpecialPositionKey_ = static_cast<int>(i);
				currentSpecialPosition_ = key;
				currentSpecialOpacity_.time = key.time;
				currentSpecialOpacity_.alpha = 1.0f;
				currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Linear;
				const auto opacityIt = std::find_if(specialTimeline.opacityKeyframes.begin(), specialTimeline.opacityKeyframes.end(),
					[&](const auto& opacityKey) { return std::abs(opacityKey.time - key.time) < 0.001f; });
				if (opacityIt != specialTimeline.opacityKeyframes.end()) currentSpecialOpacity_ = *opacityIt;
			}
			ImGui::SameLine();
			ImGui::Text("%.2f", key.time);

			// X列
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f", key.offset.x);
			// Y列
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.2f", key.offset.y);

			// Z列
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.2f", key.offset.z);

			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(key.space == ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget ? "Boss" : "Player");

			ImGui::TableSetColumnIndex(5);
			const char* easeShortNames[] = { "Linear", "In", "Out", "InOut", "Step" };
			ImGui::TextUnformatted(easeShortNames[static_cast<int>(key.interpolation)]);

			ImGui::TableSetColumnIndex(6);
			if (i > 0) {
				const auto& previous = specialTimeline.positionKeyframes[i - 1];
				const float distance = LengthVector3(
					ResolvePlayerSpecialPositionOffset_(key) - ResolvePlayerSpecialPositionOffset_(previous));
				const float duration = std::max(0.001f, key.time - previous.time);
				ImGui::Text("%.2f", distance / duration);
			} else {
				ImGui::TextUnformatted("-");
			}

			ImGui::TableSetColumnIndex(7);
			ImGui::TextUnformatted(key.advanceOnHit && i + 1 < specialTimeline.positionKeyframes.size() ? "Wait" : "-");

			//Delete列
			ImGui::TableSetColumnIndex(8);

			if (ImGui::Button("Delete")) {

				deletePositionIndex = static_cast<int>(i);
			}
			ImGui::PopID();

		}


		ImGui::EndTable();


		if (deletePositionIndex >= 0) {
			specialTimeline.positionKeyframes.erase(
				specialTimeline.positionKeyframes.begin()
				+ deletePositionIndex
			);

			SortCurrentPlayerSpecialTimeline_();
			selectedPlayerSpecialPositionKey_ = -1;
			EvaluatePlayerSpecialTimeline_();
		}

	}

	ImGui::Checkbox("Draw Movement Path", &drawPlayerSpecialPath_);
	ImGui::SameLine();
	if (ImGui::Button("Focus Movement Path")) {
		FocusPlayerSpecialPathCamera_();
	}
	if (specialTimeline.positionKeyframes.empty()) {
		ImGui::TextDisabled("No movement points yet. Add at least two Position Keys below.");
	} else {
		ImGui::TextDisabled("Drag the cyan Start / Point / End markers in the Scene view.");
	}

	if (selectedPlayerSpecialPositionKey_ >= 0 &&
		selectedPlayerSpecialPositionKey_ < static_cast<int>(specialTimeline.positionKeyframes.size())) {
		const float selectedPointTime = specialTimeline.positionKeyframes[selectedPlayerSpecialPositionKey_].time;
		currentSpecialOpacity_.time = selectedPointTime;
		ImGui::SeparatorText("Selected Point Appearance");
		ImGui::Text("Point Time: %.2f sec", selectedPointTime);
		ImGui::SliderFloat("Alpha At Point", &currentSpecialOpacity_.alpha, 0.0f, 1.0f, "%.2f");
		int pointOpacityMode = static_cast<int>(currentSpecialOpacity_.interpolation);
		const char* pointOpacityModes[] = { "Instant at point", "Gradual fade to point" };
		if (ImGui::Combo("Alpha Change At Point", &pointOpacityMode, pointOpacityModes, IM_ARRAYSIZE(pointOpacityModes))) {
			currentSpecialOpacity_.interpolation =
				static_cast<ParticleTestEditor::PlayerSpecialOpacityInterpolation>(pointOpacityMode);
		}
		if (ImGui::Button("Set Alpha On Selected Point")) {
			currentSpecialOpacity_.time = selectedPointTime;
			bool replaced = false;
			for (auto& opacityKey : specialTimeline.opacityKeyframes) {
				if (std::abs(opacityKey.time - selectedPointTime) < 0.001f) {
					opacityKey = currentSpecialOpacity_;
					replaced = true;
					break;
				}
			}
			if (!replaced) specialTimeline.opacityKeyframes.push_back(currentSpecialOpacity_);
			SortCurrentPlayerSpecialTimeline_();
			EvaluatePlayerSpecialTimeline_();
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Alpha From Point")) {
			specialTimeline.opacityKeyframes.erase(
				std::remove_if(specialTimeline.opacityKeyframes.begin(), specialTimeline.opacityKeyframes.end(),
					[&](const auto& opacityKey) { return std::abs(opacityKey.time - selectedPointTime) < 0.001f; }),
				specialTimeline.opacityKeyframes.end());
			EvaluatePlayerSpecialTimeline_();
		}
		ImGui::TextDisabled("[Alpha] appears beside points that have an opacity key.");
	}
	}

	if (playerAttackInspectorPage == 2) {
	ImGui::SeparatorText("Visual Z Keys (Model Only)");
	ImGui::TextDisabled("Moves only the rendered player and following effects. Physics and hitboxes stay on the gameplay lane.");
	if (ImGui::DragFloat("Visual Z Time", &currentSpecialVisualZ_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialVisualZ_.time = std::max(0.0f, currentSpecialVisualZ_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialVisualZ_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	if (ImGui::DragFloat("Visual Z Offset", &currentSpecialVisualZ_.offsetZ, 0.05f, -50.0f, 50.0f, "%.2f")) {
		EvaluatePlayerSpecialTimeline_();
	}
	int visualZEasing = static_cast<int>(currentSpecialVisualZ_.interpolation);
	const char* visualZEasingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Instant / Step" };
	if (ImGui::Combo("Visual Z Easing", &visualZEasing, visualZEasingNames, IM_ARRAYSIZE(visualZEasingNames))) {
		currentSpecialVisualZ_.interpolation =
			static_cast<ParticleTestEditor::PlayerSpecialPositionInterpolation>(visualZEasing);
	}
	if (ImGui::Button("Add / Replace Visual Z Key")) {
		bool replaced = false;
		for (auto& key : specialTimeline.visualZKeyframes) {
			if (std::abs(key.time - currentSpecialVisualZ_.time) < 0.001f) {
				key = currentSpecialVisualZ_;
				replaced = true;
				break;
			}
		}
		if (!replaced) specialTimeline.visualZKeyframes.push_back(currentSpecialVisualZ_);
		SortCurrentPlayerSpecialTimeline_();
		EvaluatePlayerSpecialTimeline_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Visual Z")) {
		currentSpecialVisualZ_.offsetZ = 0.0f;
	}
	if (ImGui::BeginTable("VisualZKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Z"); ImGui::TableSetupColumn("Easing");
		ImGui::TableSetupColumn("Use"); ImGui::TableSetupColumn("Delete"); ImGui::TableHeadersRow();
		int deleteVisualZIndex = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.visualZKeyframes.size()); ++i) {
			const auto& key = specialTimeline.visualZKeyframes[i];
			ImGui::PushID(18000 + i); ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", key.offsetZ);
			ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(visualZEasingNames[static_cast<int>(key.interpolation)]);
			ImGui::TableSetColumnIndex(3);
			if (ImGui::Button("Set")) {
				currentSpecialVisualZ_ = key;
				timelineTime_ = key.time;
				EvaluatePlayerSpecialTimeline_();
			}
			ImGui::TableSetColumnIndex(4); if (ImGui::Button("Delete")) deleteVisualZIndex = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (deleteVisualZIndex >= 0) {
			specialTimeline.visualZKeyframes.erase(specialTimeline.visualZKeyframes.begin() + deleteVisualZIndex);
			EvaluatePlayerSpecialTimeline_();
		}
	}

	ImGui::SeparatorText("Player Opacity Keys");
	if (ImGui::DragFloat("Opacity Time", &currentSpecialOpacity_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialOpacity_.time = std::max(0.0f, currentSpecialOpacity_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialOpacity_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	ImGui::SliderFloat("Player Alpha", &currentSpecialOpacity_.alpha, 0.0f, 1.0f, "%.2f");
	int opacityMode = static_cast<int>(currentSpecialOpacity_.interpolation);
	const char* opacityModes[] = { "Instant at this time", "Gradual fade to this time" };
	if (ImGui::Combo("Alpha Change", &opacityMode, opacityModes, IM_ARRAYSIZE(opacityModes))) {
		currentSpecialOpacity_.interpolation = static_cast<ParticleTestEditor::PlayerSpecialOpacityInterpolation>(opacityMode);
	}
	if (ImGui::Button("Instant Hide")) {
		currentSpecialOpacity_.alpha = 0.0f;
		currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step;
	}
	ImGui::SameLine();
	if (ImGui::Button("Instant Show")) {
		currentSpecialOpacity_.alpha = 1.0f;
		currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step;
	}
	if (ImGui::Button("Fade Out To Time")) {
		currentSpecialOpacity_.alpha = 0.0f;
		currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Linear;
	}
	ImGui::SameLine();
	if (ImGui::Button("Fade In To Time")) {
		currentSpecialOpacity_.alpha = 1.0f;
		currentSpecialOpacity_.interpolation = ParticleTestEditor::PlayerSpecialOpacityInterpolation::Linear;
	}
	ImGui::TextDisabled("Set Opacity Time, choose a preset, then add the key.");
	if (ImGui::Button("Add / Replace Opacity Key")) {
		bool replaced = false;
		for (auto& key : specialTimeline.opacityKeyframes) {
			if (std::abs(key.time - currentSpecialOpacity_.time) < 0.001f) { key = currentSpecialOpacity_; replaced = true; break; }
		}
		if (!replaced) specialTimeline.opacityKeyframes.push_back(currentSpecialOpacity_);
		SortCurrentPlayerSpecialTimeline_();
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::BeginTable("OpacityKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Alpha"); ImGui::TableSetupColumn("Mode");
		ImGui::TableSetupColumn("Use"); ImGui::TableSetupColumn("Delete"); ImGui::TableHeadersRow();
		int deleteIndex = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.opacityKeyframes.size()); ++i) {
			const auto& key = specialTimeline.opacityKeyframes[i];
			ImGui::PushID(10000 + i); ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", key.alpha);
			ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(key.interpolation == ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step ? "Step" : "Linear");
			ImGui::TableSetColumnIndex(3); if (ImGui::Button("Set")) { currentSpecialOpacity_ = key; timelineTime_ = key.time; EvaluatePlayerSpecialTimeline_(); }
			ImGui::TableSetColumnIndex(4); if (ImGui::Button("Delete")) deleteIndex = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (deleteIndex >= 0) { specialTimeline.opacityKeyframes.erase(specialTimeline.opacityKeyframes.begin() + deleteIndex); EvaluatePlayerSpecialTimeline_(); }
	}

	ImGui::SeparatorText("Player Direction Keys");
	if (ImGui::DragFloat("Direction Time", &currentSpecialRotation_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialRotation_.time = std::max(0.0f, currentSpecialRotation_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialRotation_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	Vector3 rotationDegrees{
		currentSpecialRotation_.rotation.x * 180.0f / kPi,
		currentSpecialRotation_.rotation.y * 180.0f / kPi,
		currentSpecialRotation_.rotation.z * 180.0f / kPi
	};
	bool directionDegreesChanged = false;
	directionDegreesChanged |= ImGui::DragFloat("Pitch X (deg)", &rotationDegrees.x, 1.0f, -1080.0f, 1080.0f, "%.1f");
	directionDegreesChanged |= ImGui::DragFloat("Yaw Y (deg)", &rotationDegrees.y, 1.0f, -1080.0f, 1080.0f, "%.1f");
	directionDegreesChanged |= ImGui::DragFloat("Roll Z (deg)", &rotationDegrees.z, 1.0f, -1080.0f, 1080.0f, "%.1f");
	if (directionDegreesChanged) {
		currentSpecialRotation_.rotation = {
			rotationDegrees.x * kPi / 180.0f,
			rotationDegrees.y * kPi / 180.0f,
			rotationDegrees.z * kPi / 180.0f
		};
		if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
			EditorObject& player = editorObjects_[playerAttackObjectIndex_];
			player.rotation = currentSpecialRotation_.rotation;
			ApplyEditorObjectTransform_(player);
		}
	}
	int directionMode = static_cast<int>(currentSpecialRotation_.interpolation);
	const char* directionModes[] = { "Instant at this time", "Gradual rotate to this time" };
	if (ImGui::Combo("Direction Change", &directionMode, directionModes, IM_ARRAYSIZE(directionModes))) {
		currentSpecialRotation_.interpolation =
			static_cast<ParticleTestEditor::PlayerSpecialRotationInterpolation>(directionMode);
	}
	auto setPreviewYaw = [&](float degrees) {
		currentSpecialRotation_.rotation.y = degrees * kPi / 180.0f;
		if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
			EditorObject& player = editorObjects_[playerAttackObjectIndex_];
			player.rotation = currentSpecialRotation_.rotation;
			ApplyEditorObjectTransform_(player);
		}
	};
	if (ImGui::Button("Yaw 0")) setPreviewYaw(0.0f);
	ImGui::SameLine();
	if (ImGui::Button("Yaw 90")) setPreviewYaw(90.0f);
	ImGui::SameLine();
	if (ImGui::Button("Yaw -90")) setPreviewYaw(-90.0f);
	ImGui::SameLine();
	if (ImGui::Button("Yaw 180")) setPreviewYaw(180.0f);
	if (ImGui::Button("Add / Replace Direction Key")) {
		bool replaced = false;
		for (auto& key : specialTimeline.rotationKeyframes) {
			if (std::abs(key.time - currentSpecialRotation_.time) < 0.001f) {
				key = currentSpecialRotation_;
				replaced = true;
				break;
			}
		}
		if (!replaced) specialTimeline.rotationKeyframes.push_back(currentSpecialRotation_);
		SortCurrentPlayerSpecialTimeline_();
		EvaluatePlayerSpecialTimeline_();
	}
	if (ImGui::BeginTable("DirectionKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("X deg"); ImGui::TableSetupColumn("Y deg");
		ImGui::TableSetupColumn("Mode"); ImGui::TableSetupColumn("Delete"); ImGui::TableHeadersRow();
		int deleteDirectionIndex = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.rotationKeyframes.size()); ++i) {
			const auto& key = specialTimeline.rotationKeyframes[i];
			ImGui::PushID(12000 + i); ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (ImGui::Selectable("##UseDirectionKey", false, ImGuiSelectableFlags_SpanAllColumns)) {
				currentSpecialRotation_ = key;
				timelineTime_ = key.time;
				EvaluatePlayerSpecialTimeline_();
			}
			ImGui::SameLine(); ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", key.rotation.x * 180.0f / kPi);
			ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", key.rotation.y * 180.0f / kPi);
			ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(
				key.interpolation == ParticleTestEditor::PlayerSpecialRotationInterpolation::Step ? "Instant" : "Gradual");
			ImGui::TableSetColumnIndex(4); if (ImGui::Button("Delete")) deleteDirectionIndex = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (deleteDirectionIndex >= 0) {
			specialTimeline.rotationKeyframes.erase(specialTimeline.rotationKeyframes.begin() + deleteDirectionIndex);
			EvaluatePlayerSpecialTimeline_();
		}
	}
	ImGui::TextDisabled("Each key chooses instant or gradual rotation on arrival.");

	ImGui::SeparatorText("Player Animation Keys");
	std::vector<std::string> playerAnimationNames;
	Object3d* playerAnimationObject = nullptr;
	if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
		playerAnimationObject = editorObjects_[playerAttackObjectIndex_].object.get();
		if (playerAnimationObject) playerAnimationNames = playerAnimationObject->GetAnimationNames();
	}
	if (playerAnimationNames.empty()) {
		ImGui::TextDisabled("This player model has no animation clips.");
	} else {
		if (currentSpecialAnimation_.animationName.empty() ||
			std::find(playerAnimationNames.begin(), playerAnimationNames.end(), currentSpecialAnimation_.animationName) == playerAnimationNames.end()) {
			currentSpecialAnimation_.animationName = playerAnimationNames.front();
		}
		if (ImGui::BeginCombo("Animation Clip", currentSpecialAnimation_.animationName.c_str())) {
			for (const auto& animationName : playerAnimationNames) {
				const bool selected = animationName == currentSpecialAnimation_.animationName;
				if (ImGui::Selectable(animationName.c_str(), selected)) {
					currentSpecialAnimation_.animationName = animationName;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}
	if (ImGui::DragFloat("Animation Time", &currentSpecialAnimation_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialAnimation_.time = std::max(0.0f, currentSpecialAnimation_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialAnimation_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	ImGui::DragFloat("Animation Blend", &currentSpecialAnimation_.blendSec, 0.01f, 0.0f, 2.0f, "%.2f sec");
	ImGui::Checkbox("Animation Loop", &currentSpecialAnimation_.loop);
	if (ImGui::Button("Preview Animation") && playerAnimationObject && !currentSpecialAnimation_.animationName.empty()) {
		playerAnimationObject->CrossFadeTo(
			currentSpecialAnimation_.animationName,
			currentSpecialAnimation_.blendSec,
			currentSpecialAnimation_.loop
		);
	}
	ImGui::SameLine();
	if (ImGui::Button("Add / Replace Animation Key") && !currentSpecialAnimation_.animationName.empty()) {
		bool replaced = false;
		for (auto& key : specialTimeline.animations) {
			if (std::abs(key.time - currentSpecialAnimation_.time) < 0.001f) {
				key = currentSpecialAnimation_;
				replaced = true;
				break;
			}
		}
		if (!replaced) specialTimeline.animations.push_back(currentSpecialAnimation_);
		SortCurrentPlayerSpecialTimeline_();
	}
	if (playerAnimationObject && !playerAnimationObject->GetPlayingAnimName().empty()) {
		ImGui::Text("Playing: %s", playerAnimationObject->GetPlayingAnimName().c_str());
	}
	if (ImGui::BeginTable("AnimationKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Clip"); ImGui::TableSetupColumn("Blend");
		ImGui::TableSetupColumn("Loop"); ImGui::TableSetupColumn("Delete"); ImGui::TableHeadersRow();
		int deleteAnimationIndex = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.animations.size()); ++i) {
			const auto& key = specialTimeline.animations[i];
			ImGui::PushID(15000 + i); ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (ImGui::Selectable("##UseAnimationKey", false, ImGuiSelectableFlags_SpanAllColumns)) {
				currentSpecialAnimation_ = key;
				timelineTime_ = key.time;
				if (playerAnimationObject) playerAnimationObject->PlayAnimation(key.animationName, key.loop);
			}
			ImGui::SameLine(); ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(key.animationName.c_str());
			ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", key.blendSec);
			ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(key.loop ? "Yes" : "No");
			ImGui::TableSetColumnIndex(4); if (ImGui::Button("Delete")) deleteAnimationIndex = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (deleteAnimationIndex >= 0) {
			specialTimeline.animations.erase(specialTimeline.animations.begin() + deleteAnimationIndex);
		}
	}
	ImGui::TextDisabled("Animation clips come from the player model. Keys choose when each clip starts.");
	}

	if (playerAttackInspectorPage == 3) {
	ImGui::SeparatorText("Combined Effect Keys");
	if (ImGui::DragFloat("Effect Time", &currentSpecialEffect_.time, 0.01f, 0.0f, 0.0f, "%.2f")) {
		currentSpecialEffect_.time = std::max(0.0f, currentSpecialEffect_.time);
		specialTimeline.totalSec = std::max(specialTimeline.totalSec, currentSpecialEffect_.time);
		timelineDuration_ = std::max(timelineDuration_, specialTimeline.totalSec);
	}
	std::vector<std::string> availableEffectJsonFiles;
	std::error_code effectDirectoryError;
	const std::filesystem::path effectDirectory("resources/effects");
	if (std::filesystem::exists(effectDirectory, effectDirectoryError)) {
		for (std::filesystem::recursive_directory_iterator it(effectDirectory, effectDirectoryError), end;
			it != end && !effectDirectoryError; it.increment(effectDirectoryError)) {
			if (!it->is_regular_file(effectDirectoryError) || it->path().extension() != ".json") continue;
			availableEffectJsonFiles.push_back(it->path().generic_string());
		}
	}
	std::sort(availableEffectJsonFiles.begin(), availableEffectJsonFiles.end());
	const std::string selectedEffectFileName = playerSpecialEffectPath_[0] != '\0'
		? std::filesystem::path(playerSpecialEffectPath_).filename().string()
		: std::string("Select effect...");
	if (ImGui::BeginCombo("Effect From resources/effects", selectedEffectFileName.c_str())) {
		for (const auto& effectJsonPath : availableEffectJsonFiles) {
			const bool selected = effectJsonPath == std::filesystem::path(playerSpecialEffectPath_).generic_string();
			const std::string relativeLabel = std::filesystem::relative(
				std::filesystem::path(effectJsonPath), effectDirectory, effectDirectoryError).generic_string();
			if (ImGui::Selectable(relativeLabel.c_str(), selected)) {
				strncpy_s(playerSpecialEffectPath_, sizeof(playerSpecialEffectPath_), effectJsonPath.c_str(), _TRUNCATE);
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (availableEffectJsonFiles.empty()) {
		ImGui::TextDisabled("No JSON files found in resources/effects.");
	}
	ImGui::InputText("Effect JSON", playerSpecialEffectPath_, sizeof(playerSpecialEffectPath_));
	if (ImGui::Button("Browse Effect JSON")) {
		std::string selectedPath;
		if (OpenEffectJsonFileDialog_(false, selectedPath)) strncpy_s(playerSpecialEffectPath_, sizeof(playerSpecialEffectPath_), selectedPath.c_str(), _TRUNCATE);
	}
	ImGui::DragFloat3("Effect Offset", &currentSpecialEffect_.offset.x, 0.05f, -100.0f, 100.0f);
	int effectPositionMode = static_cast<int>(currentSpecialEffect_.positionMode);
	const char* effectPositionModeNames[] = { "Fixed At Spawn", "Follow Player", "Movement Point" };
	if (ImGui::Combo("Effect Position Base", &effectPositionMode,
		effectPositionModeNames, IM_ARRAYSIZE(effectPositionModeNames))) {
		currentSpecialEffect_.positionMode =
			static_cast<ParticleTestEditor::PlayerSpecialEffectPositionMode>(effectPositionMode);
		currentSpecialEffect_.followPlayerMovement =
			currentSpecialEffect_.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer;
		for (auto& key : specialTimeline.effectKeyframes) {
			if (std::abs(key.time - currentSpecialEffect_.time) < 0.001f) {
				key.positionMode = currentSpecialEffect_.positionMode;
				key.followPlayerMovement = currentSpecialEffect_.followPlayerMovement;
				key.movementPointIndex = currentSpecialEffect_.movementPointIndex;
			}
		}
		SyncPlayerSpecialPreviewNodes_();
		RequestTimelineRebuild_(timelineTime_);
	}
	if (currentSpecialEffect_.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint) {
		const char* movementPointPreview = "Select a movement point";
		std::string movementPointLabel;
		if (currentSpecialEffect_.movementPointIndex >= 0 &&
			currentSpecialEffect_.movementPointIndex < static_cast<int>(specialTimeline.positionKeyframes.size())) {
			const auto& point = specialTimeline.positionKeyframes[currentSpecialEffect_.movementPointIndex];
			movementPointLabel = "Point " + std::to_string(currentSpecialEffect_.movementPointIndex) +
				"  " + std::to_string(point.time) + "s";
			movementPointPreview = movementPointLabel.c_str();
		}
		if (ImGui::BeginCombo("Movement Point For Effect", movementPointPreview)) {
			for (int pointIndex = 0; pointIndex < static_cast<int>(specialTimeline.positionKeyframes.size()); ++pointIndex) {
				const auto& point = specialTimeline.positionKeyframes[pointIndex];
				char label[96]{};
				sprintf_s(label, "Point %d  %.2fs%s", pointIndex, point.time,
					point.space == ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget ? " [Boss]" : "");
				if (ImGui::Selectable(label, currentSpecialEffect_.movementPointIndex == pointIndex)) {
					currentSpecialEffect_.movementPointIndex = pointIndex;
				}
			}
			ImGui::EndCombo();
		}
		const bool canUseSelectedPoint = selectedPlayerSpecialPositionKey_ >= 0 &&
			selectedPlayerSpecialPositionKey_ < static_cast<int>(specialTimeline.positionKeyframes.size());
		if (!canUseSelectedPoint) ImGui::BeginDisabled();
		if (ImGui::Button("Use Selected Movement Point") && canUseSelectedPoint) {
			currentSpecialEffect_.movementPointIndex = selectedPlayerSpecialPositionKey_;
		}
		if (!canUseSelectedPoint) ImGui::EndDisabled();
		ImGui::TextDisabled("This changes only the spawn position. Effect Time stays unchanged.");
	}
	auto resolveEffectPreviewPosition = [&](const PlayerSpecialEffectKeyframe& effectKey) {
		if (effectKey.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint &&
			effectKey.movementPointIndex >= 0 &&
			effectKey.movementPointIndex < static_cast<int>(specialTimeline.positionKeyframes.size())) {
			return playerSpecialPreviewOrigin_ +
				ResolvePlayerSpecialPositionOffset_(specialTimeline.positionKeyframes[effectKey.movementPointIndex]) +
				effectKey.offset;
		}
		return playerSpecialPreviewOrigin_ + previewSpecialPositionOffset_ + effectKey.offset;
	};
	if (ImGui::Button("Add / Replace Effect Key")) {
		currentSpecialEffect_.jsonPath = playerSpecialEffectPath_;
		currentSpecialEffect_.name = "I_Attack_" + std::to_string(static_cast<int>(selectedPlayerSpecialAttackType_)) + "_" + std::to_string(selectedPlayerSpecialLevel_) + "_" + std::to_string(static_cast<int>(currentSpecialEffect_.time * 1000.0f));
		bool replaced = false;
		for (auto& key : specialTimeline.effectKeyframes) {
			if (std::abs(key.time - currentSpecialEffect_.time) < 0.001f) { key = currentSpecialEffect_; replaced = true; break; }
		}
		if (!replaced) specialTimeline.effectKeyframes.push_back(currentSpecialEffect_);
		SortCurrentPlayerSpecialTimeline_();
		SyncPlayerSpecialPreviewNodes_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Effect") && playerSpecialEffectPath_[0] != '\0') {
		currentSpecialEffect_.jsonPath = playerSpecialEffectPath_;
		if (currentSpecialEffect_.name.empty()) currentSpecialEffect_.name = "I_Attack_Preview";
		EffectManager::GetInstance()->LoadEffect(currentSpecialEffect_.name, currentSpecialEffect_.jsonPath);
		EffectManager::GetInstance()->Play(currentSpecialEffect_.name, resolveEffectPreviewPosition(currentSpecialEffect_));
	}
	if (ImGui::BeginTable("EffectKeyTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("JSON"); ImGui::TableSetupColumn("Base"); ImGui::TableSetupColumn("Use"); ImGui::TableSetupColumn("Play"); ImGui::TableSetupColumn("Delete"); ImGui::TableHeadersRow();
		int deleteIndex = -1;
		for (int i = 0; i < static_cast<int>(specialTimeline.effectKeyframes.size()); ++i) {
			auto& key = specialTimeline.effectKeyframes[i]; ImGui::PushID(20000 + i); ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%.2f", key.time);
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(std::filesystem::path(key.jsonPath).filename().string().c_str());
			ImGui::TableSetColumnIndex(2);
			const char* shortBaseNames[] = { "Fixed", "Player", "Point" };
			int rowPositionMode = static_cast<int>(key.positionMode);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::Combo("##EffectKeyBase", &rowPositionMode, shortBaseNames, IM_ARRAYSIZE(shortBaseNames))) {
				key.positionMode = static_cast<ParticleTestEditor::PlayerSpecialEffectPositionMode>(rowPositionMode);
				key.followPlayerMovement = key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer;
				if (key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint && key.movementPointIndex < 0) {
					key.movementPointIndex = selectedPlayerSpecialPositionKey_ >= 0 ? selectedPlayerSpecialPositionKey_ : 0;
				}
				currentSpecialEffect_ = key;
				SyncPlayerSpecialPreviewNodes_();
				RequestTimelineRebuild_(timelineTime_);
			}
			ImGui::TableSetColumnIndex(3); if (ImGui::Button("Set")) { currentSpecialEffect_ = key; strncpy_s(playerSpecialEffectPath_, sizeof(playerSpecialEffectPath_), key.jsonPath.c_str(), _TRUNCATE); }
			ImGui::TableSetColumnIndex(4); if (ImGui::Button("Play")) { EffectManager::GetInstance()->LoadEffect(key.name, key.jsonPath); EffectManager::GetInstance()->Play(key.name, resolveEffectPreviewPosition(key)); }
			ImGui::TableSetColumnIndex(5); if (ImGui::Button("Delete")) deleteIndex = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (deleteIndex >= 0) specialTimeline.effectKeyframes.erase(specialTimeline.effectKeyframes.begin() + deleteIndex);
	}
	}

	if (playerAttackInspectorPage == 1) {
	ImGui::Text(
		"Preview Offset: %.2f, %.2f, %.2f",
		previewSpecialPositionOffset_.x,
		previewSpecialPositionOffset_.y,
		previewSpecialPositionOffset_.z
	);
	ImGui::Text("Preview World: %.2f, %.2f, %.2f",
		playerSpecialPreviewOrigin_.x + previewSpecialPositionOffset_.x,
		playerSpecialPreviewOrigin_.y + previewSpecialPositionOffset_.y,
		playerSpecialPreviewOrigin_.z + previewSpecialPositionOffset_.z);

	DrawEditorCameraControls_();
	}
	ImGui::End();
#endif
}


void ParticleTestScene::DrawParticleModeImGui_()
{
#ifdef USE_IMGUI
	ImGui::Begin("Particle Mode");
	ImGui::Text("JSON: %s", kParticleJson);
	if (ImGui::Button("Reload JSON")) {
		ReloadParticleJson_();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save HitEffect JSON")) {
		ParticleManager::GetInstance()->Save("hit_effect.json");
	}
	ImGui::SameLine();
	if (ImGui::Button("Load HitEffect JSON")) {
		ParticleManager::GetInstance()->ClearGroups();
		ParticleManager::GetInstance()->Load("hit_effect.json");
		EnsureHitEffectGroup_();
	}

	ImGui::Separator();
	ImGui::InputText("HitEffect Group", hitEffectGroupName_, sizeof(hitEffectGroupName_));
	ImGui::DragInt("Spawn Count", &hitEffectSpawnCount_, 1, 1, 1024);
	ImGui::DragFloat3("Spawn Position", &hitEffectSpawnPosition_.x, 0.1f);
	if (ImGui::Button("Create / Reset HitEffect Preset")) {
		EnsureHitEffectGroup_();
		ParticleManager::GetInstance()->ConfigureHitEffectPreset(hitEffectGroupName_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Spawn HitEffect")) {
		SpawnHitEffectPreview_();
	}
	ImGui::Separator();
	ParticleManager::GetInstance()->DrawImGuiContents();
	ImGui::End();

	if (editorParticle_) {
		editorParticle_->DebugImGui();
	}
#endif
}

void ParticleTestScene::DrawImGui(GameApp& app)
{
#ifdef USE_IMGUI
	gParticleTestEditorModeSwitcherVisible = true;
	gParticleTestEditorMode = std::clamp(gParticleTestEditorMode, 0, 2);
	if (gParticleTestEditorMode == 2) {
		editorMode_ = EditorMode::PlayerAttack;
	} else {
		editorMode_ = gParticleTestEditorMode == 0 ? EditorMode::Blender : EditorMode::Particle;
	}

	const bool objectEditorMode = editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack;
	if (objectEditorMode) {
		if (editorMode_ == EditorMode::PlayerAttack) {
			EnsurePlayerAttackEditor_(app);
			if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
				editorObjects_[playerAttackObjectIndex_].visible = timelinePlaying_;
			}
		}
		HandleEffectEditorShortcuts_(app);
		if (gParticleTestBlenderHierarchySelectionChanged) {
			gParticleTestBlenderHierarchySelectionChanged = false;
			if (gParticleTestBlenderHierarchySelected >= 0) {
				if (gParticleTestBlenderHierarchySelected < static_cast<int>(editorObjects_.size())) {
					selectedEditorObject_ = gParticleTestBlenderHierarchySelected;
					selectedParticleNode_ = -1;
				} else if (gParticleTestBlenderHierarchySelected < static_cast<int>(editorObjects_.size() + particleNodes_.size())) {
					selectedParticleNode_ = gParticleTestBlenderHierarchySelected - static_cast<int>(editorObjects_.size());
					selectedEditorObject_ = -1;
				}
			} else {
				selectedEditorObject_ = -1;
				selectedParticleNode_ = -1;
			}
		}

		gParticleTestBlenderHierarchyNames.clear();
		gParticleTestBlenderHierarchyNames.reserve(editorObjects_.size() + particleNodes_.size());
		for (const auto& item : editorObjects_) {
			gParticleTestBlenderHierarchyNames.push_back(item.name + " (" + item.modelPath + ")");
		}
		for (const auto& node : particleNodes_) {
			gParticleTestBlenderHierarchyNames.push_back(node.name + " (" + node.particleFileName + ") [" + (node.isEffectNode ? "Effect" : "Particle") + "]");
		}

		if (selectedEditorObject_ >= 0) {
			gParticleTestBlenderHierarchySelected = selectedEditorObject_;
		} else if (selectedParticleNode_ >= 0) {
			gParticleTestBlenderHierarchySelected = static_cast<int>(editorObjects_.size()) + selectedParticleNode_;
		} else {
			gParticleTestBlenderHierarchySelected = -1;
		}
		animationCameraPreviewSwapped_ = gParticleTestAnimationCameraPreviewSwapped;
		gParticleTestAnimationCameraPreviewVisible = useAnimationCameraPreview_;
		gParticleTestAnimationCameraPreviewSwapped = animationCameraPreviewSwapped_;
	} else {
		gParticleTestAnimationCameraPreviewVisible = false;
		gParticleTestAnimationCameraPreviewSwapped = false;
	}

	if (objectEditorMode) {
		DrawEffectInspectorImGui_(app);
		DrawEffectEditorImGui_(app);
		if (editorMode_ == EditorMode::PlayerAttack) {
			DrawPlayerAttackEditorImGui_(app);
		}
		DrawViewportBones_();
		DrawViewportGizmo_(app);
		if (editorMode_ == EditorMode::PlayerAttack) {
			DrawPlayerSpecialPathPreview_();
		}
	} else {
		DrawParticleModeImGui_();
	}
#endif
}
