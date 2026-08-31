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
#include <unordered_map>
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;
using namespace ParticleTestSceneSupport;
void ParticleTestScene::DrawDopeSheet_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	static std::unordered_map<int, bool> expandedObjectTracks;
    
    int trackCount = 1; // Camera
    if (playerAttackEditorEnabled_) {
        trackCount += 2; // Special hitboxes + movement points
    }
    trackCount += static_cast<int>(editorObjects_.size());
	for (const auto& item : editorObjects_) {
		if (expandedObjectTracks[item.id]) trackCount += 4;
	}
    trackCount += static_cast<int>(particleNodes_.size());
    
    float trackHeight = 22.0f;
    float headerHeight = 24.0f;
    float totalHeight = headerHeight + trackCount * trackHeight;

	static int visibleTrackRows = 8;
	if (ImGui::SmallButton("Rows -")) visibleTrackRows = std::max(3, visibleTrackRows - 1);
	ImGui::SameLine();
	if (ImGui::SmallButton("Rows +")) visibleTrackRows = std::min(20, visibleTrackRows + 1);
	ImGui::SameLine();
	ImGui::TextDisabled("Visible rows: %d / Tracks: %d", visibleTrackRows, trackCount);
	canvasSize = ImGui::GetContentRegionAvail();
	const float requestedHeight = headerHeight + visibleTrackRows * trackHeight + 4.0f;
	canvasSize.y = std::min(requestedHeight, totalHeight + 4.0f);
    if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;
    
    ImGui::BeginChild("DopeSheetContainer", canvasSize, true, ImGuiWindowFlags_None);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    const float labelWidth = 160.0f;
    float timelineStartX = canvasPos.x + labelWidth;
    float timelineWidth = canvasSize.x - labelWidth - 16.0f;
    if (timelineWidth < 50.0f) timelineWidth = 50.0f;
    float timelineEndX = timelineStartX + timelineWidth;

    timelineViewDuration_ = std::clamp(
        timelineViewDuration_ <= 0.0f ? timelineDuration_ : timelineViewDuration_,
        std::min(0.05f, timelineDuration_),
        timelineDuration_);
    timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
    
    auto timeToX = [&](float t) -> float {
        if (timelineViewDuration_ <= 0.0f) return timelineStartX;
        return timelineStartX + ((t - timelineViewStart_) / timelineViewDuration_) * timelineWidth;
    };
    
    auto xToTime = [&](float x) -> float {
        if (timelineWidth <= 0.0f) return 0.0f;
        float t = timelineViewStart_ + ((x - timelineStartX) / timelineWidth) * timelineViewDuration_;
        return std::clamp(t, 0.0f, timelineDuration_);
    };

    const bool mouseOverTimeline =
        io.MousePos.x >= timelineStartX && io.MousePos.x <= timelineEndX &&
        io.MousePos.y >= canvasPos.y && io.MousePos.y <= canvasPos.y + canvasSize.y;
    if (mouseOverTimeline && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && std::abs(io.MouseWheel) > 0.001f) {
        const float mouseRatio = std::clamp((io.MousePos.x - timelineStartX) / timelineWidth, 0.0f, 1.0f);
        // マウス位置での現在時間（ズーム前）
        const float mouseTime = timelineViewStart_ + mouseRatio * timelineViewDuration_;
        
        const float zoomFactor = io.MouseWheel > 0.0f ? 0.8f : 1.25f;
        float nextViewDuration = std::clamp(timelineViewDuration_ * zoomFactor, std::min(0.05f, timelineDuration_), timelineDuration_);
        
        // マウス位置を不変点とする新しいタイムライン開始位置
        timelineViewStart_ = mouseTime - mouseRatio * nextViewDuration;
        timelineViewDuration_ = nextViewDuration;
        
        timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
    }
    
    // ヘッダー背景
    drawList->AddRectFilled(canvasPos, ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_HeaderActive));
    drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + headerHeight), ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_Border));
    
    // タイムライン描画領域をクリップ（ラベル領域へのはみ出しを防ぐ）
    // Keep every custom row, including its label, inside the child window.
    // The old timeline-only clip was temporarily replaced by label clips and
    // allowed labels from scrolled rows to leak into the Scene viewport.
    drawList->PushClipRect(canvasPos, ImVec2(timelineEndX, canvasPos.y + canvasSize.y), true);

    // グリッド線の描画 (表示幅に応じて適切な時間ステップを自動計算)
    float minPixelStep = 60.0f; // グリッドテキスト同士が重ならない最低間隔
    float maxGrids = timelineWidth / minPixelStep;
    if (maxGrids < 1.0f) maxGrids = 1.0f;
    
    float rawStep = timelineViewDuration_ / maxGrids;
    float gridStep = 1.0f;
    if (rawStep < 0.01f) gridStep = 0.01f;
    else if (rawStep < 0.02f) gridStep = 0.02f;
    else if (rawStep < 0.05f) gridStep = 0.05f;
    else if (rawStep < 0.1f)  gridStep = 0.1f;
    else if (rawStep < 0.2f)  gridStep = 0.2f;
    else if (rawStep < 0.5f)  gridStep = 0.5f;
    else if (rawStep < 1.0f)  gridStep = 1.0f;
    else if (rawStep < 2.0f)  gridStep = 2.0f;
    else if (rawStep < 5.0f)  gridStep = 5.0f;
    else gridStep = 10.0f;

    // キリの良い開始時間を決定
    float firstGridTime = std::ceil(timelineViewStart_ / gridStep) * gridStep;
    for (float t = firstGridTime; t <= timelineViewStart_ + timelineViewDuration_ + 0.0001f; t += gridStep) {
        if (t < timelineViewStart_ || t > timelineViewStart_ + timelineViewDuration_) {
            continue;
        }
        float gridX = timeToX(t);
        drawList->AddLine(ImVec2(gridX, canvasPos.y), ImVec2(gridX, canvasPos.y + totalHeight), ImGui::GetColorU32(ImGuiCol_Border, 0.3f));
        
        char buf[32];
        if (gridStep < 0.1f) {
            sprintf_s(buf, "%.3fs", t);
        } else if (gridStep < 1.0f) {
            sprintf_s(buf, "%.2fs", t);
        } else {
            sprintf_s(buf, "%.1fs", t);
        }
        drawList->AddText(ImVec2(gridX + 2.0f, canvasPos.y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), buf);
    }
    
    float currentY = canvasPos.y + headerHeight;
    ImVec2 mousePos = io.MousePos;
    bool clicked = ImGui::IsMouseClicked(0);
    
    // 1. Camera Track
    bool anyKeyframeClicked = false;
    {
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_TableRowBg));
        drawList->PushClipRect(ImVec2(canvasPos.x, currentY), ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), "Camera Keys");
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));
        
        for (int i = 0; i < static_cast<int>(cameraKeyframes_.size()); ++i) {
            float kx = timeToX(cameraKeyframes_[i].time);
            ImVec2 center(kx, currentY + trackHeight * 0.5f);
            
            bool hovered = (std::abs(mousePos.x - center.x) <= 6.0f && std::abs(mousePos.y - center.y) <= 6.0f);
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::CameraKeyframe;
                dragKeyframeIndex_ = i;
                selectedKeyframeType_ = DragTarget::CameraKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = -1;
                anyKeyframeClicked = true;
            }
            
            bool isSelected = (selectedKeyframeType_ == DragTarget::CameraKeyframe && selectedKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
        }
        currentY += trackHeight;
    }

    // 2. PlayerAttack special hitboxes
    if (playerAttackEditorEnabled_) {
        const auto& hitboxKeys = CurrentPlayerSpecialTimeline_().hitboxes;
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight),
            ImGui::GetColorU32(ImGuiCol_TableRowBg));
        drawList->PushClipRect(ImVec2(canvasPos.x, currentY),
            ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f),
            ImColor(255, 165, 70), "HitBox Keys");
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight),
            ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));

        for (int i = 0; i < static_cast<int>(hitboxKeys.size()); ++i) {
            const float startX = timeToX(hitboxKeys[i].time);
            const float endX = timeToX(hitboxKeys[i].time + hitboxKeys[i].duration);
            const ImVec2 center(startX, currentY + trackHeight * 0.5f);
            const bool hovered = std::abs(mousePos.x - center.x) <= 7.0f &&
                std::abs(mousePos.y - center.y) <= 7.0f;

            const bool selected = selectedKeyframeType_ == DragTarget::PlayerSpecialHitboxKeyframe &&
                selectedKeyframeIndex_ == i;
            const ImU32 barColor = selected ? IM_COL32(255, 205, 70, 180) : IM_COL32(255, 125, 55, 135);
            drawList->AddRectFilled(
                ImVec2(startX, currentY + 7.0f),
                ImVec2(std::max(startX + 2.0f, endX), currentY + trackHeight - 7.0f),
                barColor, 2.0f);

            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::PlayerSpecialHitboxKeyframe;
                dragKeyframeIndex_ = i;
                currentSpecialHitbox_ = hitboxKeys[i];
                timelineTime_ = hitboxKeys[i].time;
                selectedKeyframeType_ = DragTarget::PlayerSpecialHitboxKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = -1;
                anyKeyframeClicked = true;
                EvaluatePlayerSpecialTimeline_();
            }

            const ImU32 keyColor = selected ? ImColor(255, 220, 70) : ImColor(255, 145, 55);
            drawList->AddQuadFilled(
                ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y),
                ImVec2(center.x, center.y + 6.0f), ImVec2(center.x - 6.0f, center.y), keyColor);
            if (hovered) {
                ImGui::SetTooltip("HitBox %d  %.3f sec / Duration %.3f sec", i, hitboxKeys[i].time, hitboxKeys[i].duration);
            }
        }
        currentY += trackHeight;
    }

    // 3. PlayerAttack movement points
    if (playerAttackEditorEnabled_) {
        const auto& movementKeys = CurrentPlayerSpecialTimeline_().positionKeyframes;
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight),
            ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
        drawList->PushClipRect(ImVec2(canvasPos.x, currentY),
            ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f),
            ImColor(100, 225, 255), "Movement Points");
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight),
            ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));

        for (int i = 0; i < static_cast<int>(movementKeys.size()); ++i) {
            const float kx = timeToX(movementKeys[i].time);
            const ImVec2 center(kx, currentY + trackHeight * 0.5f);
            const bool hovered = std::abs(mousePos.x - center.x) <= 7.0f &&
                std::abs(mousePos.y - center.y) <= 7.0f;
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                dragTarget_ = DragTarget::PlayerSpecialPositionKeyframe;
                dragKeyframeIndex_ = i;
                selectedPlayerSpecialPositionKey_ = i;
                currentSpecialPosition_ = movementKeys[i];
                timelineTime_ = movementKeys[i].time;
                selectedKeyframeType_ = DragTarget::PlayerSpecialPositionKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = -1;
                anyKeyframeClicked = true;
                EvaluatePlayerSpecialTimeline_();
            }

            const bool selected = selectedPlayerSpecialPositionKey_ == i;
            const ImU32 color = selected ? ImColor(255, 210, 50) : ImColor(80, 220, 255);
            drawList->AddQuadFilled(
                ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y),
                ImVec2(center.x, center.y + 6.0f), ImVec2(center.x - 6.0f, center.y), color);
            if (hovered) {
                ImGui::SetTooltip("Point %d  %.3f sec", i, movementKeys[i].time);
            }
        }
        currentY += trackHeight;
    }

    // 4. Model Objects Tracks
    for (int objIdx = 0; objIdx < static_cast<int>(editorObjects_.size()); ++objIdx) {
        auto& item = editorObjects_[objIdx];
        bool isSelectedObj = (selectedEditorObject_ == objIdx);
        
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), isSelectedObj ? ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.4f) : ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
        
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 2.0f, currentY + 2.0f));
        ImGui::PushID(40000 + item.id);
        if (ImGui::SmallButton(expandedObjectTracks[item.id] ? "v" : ">")) {
			expandedObjectTracks[item.id] = !expandedObjectTracks[item.id];
		}
        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 22.0f, currentY));
        char selectId[128];
        sprintf_s(selectId, "##select_obj_%d", objIdx);
        if (ImGui::InvisibleButton(selectId, ImVec2(labelWidth - 22.0f, trackHeight))) {
            selectedEditorObject_ = objIdx;
            selectedParticleNode_ = -1;
        }
        
        drawList->PushClipRect(ImVec2(canvasPos.x, currentY), ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
        drawList->AddText(ImVec2(canvasPos.x + 25.0f, currentY + 4.0f), isSelectedObj ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), item.name.c_str());
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));

		// Draw the interpolation owned by each key as a colored segment leading
		// to the next key: Linear / EaseIn / EaseOut / EaseInOut.
		const ImU32 interpolationColors[] = {
			IM_COL32(75, 210, 255, 230),
			IM_COL32(180, 120, 255, 230),
			IM_COL32(255, 155, 80, 230),
			IM_COL32(100, 235, 145, 230),
		};
		for (int i = 0; i + 1 < static_cast<int>(item.keyframes.size()); ++i) {
			const float startX = timeToX(item.keyframes[i].time);
			const float endX = timeToX(item.keyframes[i + 1].time);
			const float lineY = currentY + trackHeight * 0.5f;
			const int interpolation = std::clamp(item.keyframes[i].interpolationType, 0, 3);
			const bool selectedSegment = selectedKeyframeType_ == DragTarget::ModelKeyframe &&
				selectedKeyframeObjectIndex_ == objIdx && selectedKeyframeIndex_ == i;
			drawList->AddLine(ImVec2(startX, lineY), ImVec2(endX, lineY),
				interpolationColors[interpolation], selectedSegment ? 4.0f : 2.0f);
		}
        
        for (int i = 0; i < static_cast<int>(item.keyframes.size()); ++i) {
            float kx = timeToX(item.keyframes[i].time);
            ImVec2 center(kx, currentY + trackHeight * 0.5f);
            
            bool hovered = (std::abs(mousePos.x - center.x) <= 6.0f && std::abs(mousePos.y - center.y) <= 6.0f);
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ModelKeyframe;
                dragObjectIndex_ = objIdx;
                dragKeyframeIndex_ = i;
                selectedEditorObject_ = objIdx;
                selectedParticleNode_ = -1;
                selectedKeyframeType_ = DragTarget::ModelKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = objIdx;
				selectedModelKeyframeChannel_ = 0;
                anyKeyframeClicked = true;
            }
            
            bool isSelectedKey = (selectedKeyframeType_ == DragTarget::ModelKeyframe && selectedKeyframeObjectIndex_ == objIdx && selectedKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
        }
        currentY += trackHeight;

		if (expandedObjectTracks[item.id]) {
			const char* channelNames[] = { "Position", "Rotation", "Scale", "Color" };
			const ImU32 channelColors[] = {
				IM_COL32(90, 210, 255, 255), IM_COL32(255, 170, 80, 255),
				IM_COL32(120, 235, 130, 255), IM_COL32(235, 110, 210, 255)
			};
			for (int channel = 0; channel < 4; ++channel) {
				drawList->AddRectFilled(ImVec2(canvasPos.x, currentY),
					ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_TableRowBg));
				drawList->PushClipRect(ImVec2(canvasPos.x, currentY),
					ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
				drawList->AddText(ImVec2(canvasPos.x + 36.0f, currentY + 4.0f), channelColors[channel], channelNames[channel]);
				drawList->PopClipRect();
				drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight),
					ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));

				for (int i = 0; i < static_cast<int>(item.keyframes.size()); ++i) {
					const float kx = timeToX(item.keyframes[i].time);
					const ImVec2 center(kx, currentY + trackHeight * 0.5f);
					const bool hovered = std::abs(mousePos.x - center.x) <= 6.0f &&
						std::abs(mousePos.y - center.y) <= 6.0f;
					if (hovered && clicked && dragTarget_ == DragTarget::None) {
						PushUndoSnapshot_();
						dragTarget_ = DragTarget::ModelKeyframe;
						dragObjectIndex_ = objIdx;
						dragKeyframeIndex_ = i;
						selectedEditorObject_ = objIdx;
						selectedParticleNode_ = -1;
						selectedKeyframeType_ = DragTarget::ModelKeyframe;
						selectedKeyframeIndex_ = i;
						selectedKeyframeObjectIndex_ = objIdx;
						selectedModelKeyframeChannel_ = channel + 1;
						anyKeyframeClicked = true;
					}
					const bool selected = selectedKeyframeType_ == DragTarget::ModelKeyframe &&
						selectedKeyframeObjectIndex_ == objIdx && selectedKeyframeIndex_ == i &&
						selectedModelKeyframeChannel_ == channel + 1;
					const ImU32 keyColor = selected ? IM_COL32(255, 215, 60, 255) : channelColors[channel];
					drawList->AddQuadFilled(
						ImVec2(center.x, center.y - 5.0f), ImVec2(center.x + 5.0f, center.y),
						ImVec2(center.x, center.y + 5.0f), ImVec2(center.x - 5.0f, center.y), keyColor);
				}
				currentY += trackHeight;
			}
		}
    }

    // Dope Sheet背景がクリックされたかつ、キーフレームがどれもクリックされていなかったら選択解除
    if (clicked && !anyKeyframeClicked && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        selectedKeyframeType_ = DragTarget::None;
        selectedKeyframeIndex_ = -1;
        selectedKeyframeObjectIndex_ = -1;
		selectedModelKeyframeChannel_ = 0;
    }
    
    // 3. Particle Nodes Tracks
    int deleteParticleNodeIndex = -1;
    for (int nodeIdx = 0; nodeIdx < static_cast<int>(particleNodes_.size()); ++nodeIdx) {
        auto& node = particleNodes_[nodeIdx];
        bool isSelectedNode = (selectedParticleNode_ == nodeIdx);
        
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), isSelectedNode ? ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.4f) : ImGui::GetColorU32(ImGuiCol_TableRowBg));
        
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, currentY));
        char selectId[128];
        sprintf_s(selectId, "##select_node_%d", nodeIdx);
        if (ImGui::InvisibleButton(selectId, ImVec2(labelWidth, trackHeight))) {
            selectedParticleNode_ = nodeIdx;
            selectedEditorObject_ = -1;
        }
		if (ImGui::BeginPopupContextItem()) {
			ImGui::TextUnformatted(node.name.c_str());
			if (ImGui::MenuItem(node.isEffectNode ? "Delete Effect Track" : "Delete Particle Track")) {
				deleteParticleNodeIndex = nodeIdx;
			}
			ImGui::EndPopup();
		}

		// Keep deletion available directly from the dope sheet instead of requiring
		// the user to find the corresponding Inspector section.
		ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + labelWidth - 25.0f, currentY + 2.0f));
		ImGui::PushID(30000 + nodeIdx);
		if (ImGui::SmallButton("X")) {
			deleteParticleNodeIndex = nodeIdx;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(node.isEffectNode ? "Delete this effect track" : "Delete this particle track");
		}
		ImGui::PopID();
        
        drawList->PushClipRect(ImVec2(canvasPos.x, currentY), ImVec2(canvasPos.x + labelWidth - 6.0f, currentY + trackHeight), true);
		const std::string nodeTrackLabel = node.name + (node.isEffectNode ? " [Effect]" : " [Particle]");
		drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), isSelectedNode ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), nodeTrackLabel.c_str());
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));
        
        float barStartX = timeToX(node.startTime);
        float barEndX = timeToX(node.endTime);
        ImVec2 barMin(barStartX, currentY + 3.0f);
        ImVec2 barMax(barEndX, currentY + trackHeight - 3.0f);
        
		ImU32 barColor = node.isEffectNode
			? (isSelectedNode ? ImColor(190, 110, 255, 210) : ImColor(130, 70, 190, 170))
			: (isSelectedNode ? ImColor(100, 220, 100, 180) : ImColor(60, 160, 60, 140));
        drawList->AddRectFilled(barMin, barMax, barColor, 4.0f);
        drawList->AddRect(barMin, barMax, ImColor(255, 255, 255, 100), 4.0f, 0, 1.0f);
        
        bool hoveredBar = (mousePos.x >= barMin.x && mousePos.x <= barMax.x && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        bool hoveredStart = (std::abs(mousePos.x - barMin.x) <= 6.0f && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        bool hoveredEnd = (std::abs(mousePos.x - barMax.x) <= 6.0f && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        
        if (clicked && dragTarget_ == DragTarget::None) {
            if (hoveredStart) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeStart;
                dragParticleNodeIndex_ = nodeIdx;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            } else if (hoveredEnd) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeEnd;
                dragParticleNodeIndex_ = nodeIdx;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            } else if (hoveredBar) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeBar;
                dragParticleNodeIndex_ = nodeIdx;
                dragStartOffset_ = xToTime(mousePos.x) - node.startTime;
                dragStartVal1_ = node.startTime;
                dragStartVal2_ = node.endTime;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            }
        }
        
        drawList->AddRectFilled(ImVec2(barMin.x - 2.0f, barMin.y), ImVec2(barMin.x + 2.0f, barMax.y), ImColor(255, 255, 255, 200), 1.0f);
        drawList->AddRectFilled(ImVec2(barMax.x - 2.0f, barMin.y), ImVec2(barMax.x + 2.0f, barMax.y), ImColor(255, 255, 255, 200), 1.0f);
        
        currentY += trackHeight;
    }

	if (deleteParticleNodeIndex >= 0 && deleteParticleNodeIndex < static_cast<int>(particleNodes_.size())) {
		PushUndoSnapshot_();
		particleNodes_.erase(particleNodes_.begin() + deleteParticleNodeIndex);
		selectedParticleNode_ = -1;
		dragParticleNodeIndex_ = -1;
		dragTarget_ = DragTarget::None;
		RequestTimelineRebuild_(timelineTime_);
	}
    
    // 再生ヘッドの縦線描画
    float curX = timeToX(timelineTime_);
    drawList->AddLine(ImVec2(curX, canvasPos.y), ImVec2(curX, canvasPos.y + totalHeight), ImColor(50, 150, 255, 200), 2.0f);
    
    // 再生ヘッドのつまみ描画
    ImVec2 headCenter(curX, canvasPos.y + headerHeight);
    drawList->AddTriangleFilled(ImVec2(headCenter.x - 6.0f, headCenter.y - 12.0f), ImVec2(headCenter.x + 6.0f, headCenter.y - 12.0f), ImVec2(headCenter.x, headCenter.y), ImColor(50, 150, 255));
    
    bool hoveredHead = (mousePos.x >= curX - 6.0f && mousePos.x <= curX + 6.0f && mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + headerHeight);
    bool hoveredHeaderArea = (mousePos.x >= timelineStartX && mousePos.x <= timelineEndX && mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + headerHeight);
    // The time cursor can also be moved from the empty time area of any track.
    // Keyframe and particle-node hit tests run first, so their drag operation
    // still takes priority when the user clicks directly on them.
    bool hoveredTrackTimeArea = (mousePos.x >= timelineStartX && mousePos.x <= timelineEndX &&
        mousePos.y > canvasPos.y + headerHeight && mousePos.y <= canvasPos.y + totalHeight);
    
    // クリッピングを終了
    drawList->PopClipRect();
    
    if (clicked && dragTarget_ == DragTarget::None) {
        if (hoveredHead || hoveredHeaderArea || (hoveredTrackTimeArea && !anyKeyframeClicked)) {
            dragTarget_ = DragTarget::TimelineTime;
        }
    }
    
    // ドラッグ中のインタラクション処理
    if (ImGui::IsMouseDown(0) && dragTarget_ != DragTarget::None) {
        float mouseT = xToTime(mousePos.x);
        
        switch (dragTarget_) {
        case DragTarget::TimelineTime:
            timelineTime_ = mouseT;
            RequestTimelineRebuild_(timelineTime_);
            break;
            
        case DragTarget::ModelKeyframe:
            if (dragObjectIndex_ >= 0 && dragObjectIndex_ < static_cast<int>(editorObjects_.size())) {
                auto& item = editorObjects_[dragObjectIndex_];
                if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(item.keyframes.size())) {
                    item.keyframes[dragKeyframeIndex_].time = mouseT;
					// Do not sort while dragging. Sorting changes the vector index and
					// previously caused the drag to jump to a different overlapping key.
                }
            }
            break;
            
        case DragTarget::CameraKeyframe:
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(cameraKeyframes_.size())) {
                cameraKeyframes_[dragKeyframeIndex_].time = mouseT;
                SortCameraKeyframes_();
                EvaluateTimeline_(false);
            }
            break;

        case DragTarget::PlayerAttackHitboxKeyframe:
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(playerAttackHitboxKeyframes_.size())) {
                playerAttackHitboxKeyframes_[dragKeyframeIndex_].time = mouseT;
                SortPlayerAttackHitboxKeyframes_();
                EvaluateTimeline_(false);
            }
            break;

        case DragTarget::PlayerSpecialHitboxKeyframe: {
            auto& hitboxKeys = CurrentPlayerSpecialTimeline_().hitboxes;
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(hitboxKeys.size())) {
                hitboxKeys[dragKeyframeIndex_].time = mouseT;
                currentSpecialHitbox_ = hitboxKeys[dragKeyframeIndex_];
                timelineTime_ = mouseT;
                CurrentPlayerSpecialTimeline_().totalSec = std::max(
                    CurrentPlayerSpecialTimeline_().totalSec,
                    mouseT + hitboxKeys[dragKeyframeIndex_].duration);
                timelineDuration_ = std::max(timelineDuration_, CurrentPlayerSpecialTimeline_().totalSec);
                EvaluatePlayerSpecialTimeline_();
            }
            break;
        }

        case DragTarget::PlayerSpecialPositionKeyframe: {
            auto& movementKeys = CurrentPlayerSpecialTimeline_().positionKeyframes;
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(movementKeys.size())) {
                movementKeys[dragKeyframeIndex_].time = mouseT;
                currentSpecialPosition_ = movementKeys[dragKeyframeIndex_];
                timelineTime_ = mouseT;
                CurrentPlayerSpecialTimeline_().totalSec = std::max(
                    CurrentPlayerSpecialTimeline_().totalSec, mouseT);
				SortCurrentPlayerSpecialTimeline_();
				const auto& sortedKeys = CurrentPlayerSpecialTimeline_().positionKeyframes;
				for (int i = 0; i < static_cast<int>(sortedKeys.size()); ++i) {
					if (std::abs(sortedKeys[i].time - mouseT) < 0.001f) {
						dragKeyframeIndex_ = i;
						selectedPlayerSpecialPositionKey_ = i;
						selectedKeyframeIndex_ = i;
						break;
					}
				}
                EvaluatePlayerSpecialTimeline_();
            }
            break;
        }
            
        case DragTarget::ParticleNodeStart:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                node.startTime = std::min(mouseT, node.endTime - 0.01f);
                node.startTime = std::max(0.0f, node.startTime);
                node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        case DragTarget::ParticleNodeEnd:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                node.endTime = std::max(mouseT, node.startTime + 0.01f);
                node.endTime = std::min(timelineDuration_, node.endTime);
                node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        case DragTarget::ParticleNodeBar:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                float duration = dragStartVal2_ - dragStartVal1_;
                float targetStart = mouseT - dragStartOffset_;
                node.startTime = std::clamp(targetStart, 0.0f, timelineDuration_ - duration);
                node.endTime = node.startTime + duration;
                node.presetDuration = duration;
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        default:
            break;
        }
    }
    
    if (ImGui::IsMouseReleased(0) && dragTarget_ != DragTarget::None) {
        if (dragTarget_ == DragTarget::ModelKeyframe) {
            if (dragObjectIndex_ >= 0 && dragObjectIndex_ < static_cast<int>(editorObjects_.size())) {
				auto& item = editorObjects_[dragObjectIndex_];
				const float movedTime = dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(item.keyframes.size())
					? item.keyframes[dragKeyframeIndex_].time : 0.0f;
				SortKeyframes_(item);
				for (int i = 0; i < static_cast<int>(item.keyframes.size()); ++i) {
					if (std::abs(item.keyframes[i].time - movedTime) < 0.001f) {
						selectedKeyframeIndex_ = i;
						break;
					}
				}
                EvaluateTimeline_(false);
            }
        } else if (dragTarget_ == DragTarget::CameraKeyframe) {
            SortCameraKeyframes_();
            EvaluateTimeline_(false);
        } else if (dragTarget_ == DragTarget::PlayerAttackHitboxKeyframe) {
            SortPlayerAttackHitboxKeyframes_();
            EvaluateTimeline_(false);
		} else if (dragTarget_ == DragTarget::PlayerSpecialHitboxKeyframe) {
			const float selectedTime = currentSpecialHitbox_.time;
			SortCurrentPlayerSpecialTimeline_();
			selectedKeyframeIndex_ = -1;
			const auto& hitboxKeys = CurrentPlayerSpecialTimeline_().hitboxes;
			for (int i = 0; i < static_cast<int>(hitboxKeys.size()); ++i) {
				if (std::abs(hitboxKeys[i].time - selectedTime) < 0.001f) {
					selectedKeyframeIndex_ = i;
					break;
				}
			}
			EvaluatePlayerSpecialTimeline_();
		} else if (dragTarget_ == DragTarget::PlayerSpecialPositionKeyframe) {
			const float selectedTime = currentSpecialPosition_.time;
			SortCurrentPlayerSpecialTimeline_();
			selectedPlayerSpecialPositionKey_ = -1;
			const auto& movementKeys = CurrentPlayerSpecialTimeline_().positionKeyframes;
			for (int i = 0; i < static_cast<int>(movementKeys.size()); ++i) {
				if (std::abs(movementKeys[i].time - selectedTime) < 0.001f) {
					selectedPlayerSpecialPositionKey_ = i;
					break;
				}
			}
			EvaluatePlayerSpecialTimeline_();
        }
        dragTarget_ = DragTarget::None;
    }
    
    ImGui::EndChild();
#endif
}

