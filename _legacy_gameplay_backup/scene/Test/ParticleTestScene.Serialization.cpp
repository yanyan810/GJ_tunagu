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
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;
using namespace ParticleTestSceneSupport;

std::string ParticleTestScene::MakeEffectsJsonPath_(const std::string& path) const
{
	std::filesystem::path source(path);
	std::filesystem::path fileName = source.filename();
	if (fileName.empty()) {
		fileName = "effect_editor.json";
	}
	if (fileName.extension().empty()) {
		fileName.replace_extension(".json");
	}
	return (std::filesystem::path("resources/effects") / fileName).generic_string();
}

void ParticleTestScene::SaveEffectJson_(const std::string& path) const
{
	json root;
	root["timeline"] = {
		{ "duration", timelineDuration_ },
		{ "loop", timelineLoop_ }
	};
	root["animationCamera"] = {
		{ "position", { animationCameraPosition_.x, animationCameraPosition_.y, animationCameraPosition_.z } },
		{ "rotation", { animationCameraRotation_.x, animationCameraRotation_.y, animationCameraRotation_.z } },
		{ "fovY", animationCameraFovY_ },
		{ "preview", useAnimationCameraPreview_ },
		{ "previewSwapped", animationCameraPreviewSwapped_ },
		{ "keyframes", json::array() }
	};
	for (const auto& key : cameraKeyframes_) {
		root["animationCamera"]["keyframes"].push_back({
			{ "time", key.time },
			{ "position", { key.position.x, key.position.y, key.position.z } },
			{ "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
			{ "fovY", key.fovY }
			});
	}
	root["objects"] = json::array();
	for (const auto& item : editorObjects_) {
		json object;
		object["id"] = item.id;
		object["name"] = item.name;
		object["modelPath"] = item.modelPath;
		object["texturePath"] = item.texturePath;
		object["geometryType"] = item.geometryType;
		object["position"] = { item.position.x, item.position.y, item.position.z };
		object["rotation"] = { item.rotation.x, item.rotation.y, item.rotation.z };
		object["scale"] = { item.scale.x, item.scale.y, item.scale.z };
		object["color"] = { item.color.x, item.color.y, item.color.z, item.color.w };
		object["blendMode"] = static_cast<int>(item.blendMode);
		object["billboard"] = item.billboard;
		object["bloomPostEffect"] = item.bloomPostEffect;
		object["outlineBloomPostEffect"] = item.outlineBloomPostEffect;
		object["visible"] = item.visible;
		object["bloomColor"] = { item.bloomColor.x, item.bloomColor.y, item.bloomColor.z, item.bloomColor.w };
		object["outlineBloomColor"] = { item.outlineBloomColor.x, item.outlineBloomColor.y, item.outlineBloomColor.z, item.outlineBloomColor.w };
		object["showBones"] = item.showBones;
		object["selectedBone"] = item.selectedBone;
		object["attachToBone"] = item.attachToBone;
		object["attachParentId"] = item.attachParentId;
		object["attachJointName"] = item.attachJointName;
		object["attachOffset"] = { item.attachOffset.x, item.attachOffset.y, item.attachOffset.z };
		object["attachRotation"] = { item.attachRotation.x, item.attachRotation.y, item.attachRotation.z };
		object["attachScale"] = { item.attachScale.x, item.attachScale.y, item.attachScale.z };
		object["bonePoses"] = json::array();
		for (const auto& pose : item.bonePoses) {
			object["bonePoses"].push_back({
				{ "name", pose.name },
				{ "translate", { pose.translate.x, pose.translate.y, pose.translate.z } },
				{ "rotate", { pose.rotate.x, pose.rotate.y, pose.rotate.z } },
				{ "scale", { pose.scale.x, pose.scale.y, pose.scale.z } }
				});
		}
		object["keyframes"] = json::array();
		for (const auto& key : item.keyframes) {
			json keyJson = {
				{ "time", key.time },
				{ "position", { key.position.x, key.position.y, key.position.z } },
				{ "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
				{ "scale", { key.scale.x, key.scale.y, key.scale.z } },
				{ "color", { key.color.x, key.color.y, key.color.z, key.color.w } },
				{ "bloomPostEffect", key.bloomPostEffect },
				{ "outlineBloomPostEffect", key.outlineBloomPostEffect },
				{ "bloomColor", { key.bloomColor.x, key.bloomColor.y, key.bloomColor.z, key.bloomColor.w } },
				{ "outlineBloomColor", { key.outlineBloomColor.x, key.outlineBloomColor.y, key.outlineBloomColor.z, key.outlineBloomColor.w } },
				{ "interpolationType", key.interpolationType }
			};
			keyJson["bonePoses"] = json::array();
			for (const auto& pose : key.bonePoses) {
				keyJson["bonePoses"].push_back({
					{ "name", pose.name },
					{ "translate", { pose.translate.x, pose.translate.y, pose.translate.z } },
					{ "rotate", { pose.rotate.x, pose.rotate.y, pose.rotate.z } },
					{ "scale", { pose.scale.x, pose.scale.y, pose.scale.z } }
					});
			}
			keyJson["vertexOffsets"] = json::array();
			for (const auto& [idx, offset] : key.vertexOffsets) {
				keyJson["vertexOffsets"].push_back({
					{ "index", idx },
					{ "offset", { offset.x, offset.y, offset.z } }
					});
			}
			object["keyframes"].push_back(std::move(keyJson));
		}
		object["vertexOffsets"] = json::array();
		for (const auto& [idx, offset] : item.vertexOffsets) {
			object["vertexOffsets"].push_back({
				{ "index", idx },
				{ "offset", { offset.x, offset.y, offset.z } }
				});
		}
		root["objects"].push_back(std::move(object));
	}

	root["particleNodes"] = json::array();
	for (const auto& node : particleNodes_) {
		json jNode;
		jNode["name"] = node.name;
		jNode["particleFileName"] = node.particleFileName;
		jNode["nodeType"] = node.isEffectNode ? "effect" : "particle";
		jNode["startTime"] = node.startTime;
		jNode["endTime"] = node.endTime;
		jNode["position"] = { node.position.x, node.position.y, node.position.z };
		jNode["rotation"] = { node.rotation.x, node.rotation.y, node.rotation.z };
		jNode["scale"] = { node.scale.x, node.scale.y, node.scale.z };
		jNode["emitCount"] = node.emitCount;
		jNode["presetDuration"] = node.presetDuration;
		root["particleNodes"].push_back(std::move(jNode));
	}

	root["playerAttackEditor"] = {
		{ "enabled", playerAttackEditorEnabled_ },
		{ "drawHitbox", drawPlayerAttackHitbox_ },
		{ "playerObjectIndex", playerAttackObjectIndex_ },
		{ "currentHitbox", {
			{ "offset", { currentPlayerAttackHitbox_.offset.x, currentPlayerAttackHitbox_.offset.y, currentPlayerAttackHitbox_.offset.z } },
			{ "halfSize", { currentPlayerAttackHitbox_.halfSize.x, currentPlayerAttackHitbox_.halfSize.y, currentPlayerAttackHitbox_.halfSize.z } },
			{ "active", currentPlayerAttackHitbox_.active }
		} },
		{ "hitboxKeyframes", json::array() }
	};
	for (const auto& key : playerAttackHitboxKeyframes_) {
		root["playerAttackEditor"]["hitboxKeyframes"].push_back({
			{ "time", key.time },
			{ "offset", { key.offset.x, key.offset.y, key.offset.z } },
			{ "halfSize", { key.halfSize.x, key.halfSize.y, key.halfSize.z } },
			{ "active", key.active }
			});
	}

	root["playerAttackEditor"]["sideSpecialTimelines"] = json::array();
	for (int level = 0; level < static_cast<int>(sideSpecialTimelines_.size()); ++level) {
		const PlayerSpecialTimeline& timeline = sideSpecialTimelines_[level];
		json jTimeline;
		jTimeline["level"] = level;
		jTimeline["name"] = timeline.name;
		jTimeline["totalSec"] = timeline.totalSec;
		jTimeline["hitboxes"] = json::array();
		for (const PlayerSpecialHitboxKeyframe& key : timeline.hitboxes) {
			jTimeline["hitboxes"].push_back({
				{ "time", key.time },
				{ "duration", key.duration },
				{ "hitStopSec", key.hitStopSec },
				{ "offset", { key.offset.x, key.offset.y, key.offset.z } },
				{ "halfSize", { key.halfSize.x, key.halfSize.y, key.halfSize.z } },
				{ "damage", key.damage },
				{ "active", key.active },
				{ "multiHit", key.multiHit }
				});
		}
		jTimeline["motions"] = json::array();
		for (const PlayerSpecialMotionKeyframe& key : timeline.motions) {
			jTimeline["motions"].push_back({
				{ "time", key.time },
				{ "duration", key.duration },
				{ "velocity", { key.velocity.x, key.velocity.y, key.velocity.z } },
				{ "lockVelocity", key.lockVelocity }
				});
		}
		jTimeline["animations"] = json::array();
		for (const PlayerSpecialAnimationKeyframe& key : timeline.animations) {
			jTimeline["animations"].push_back({
				{ "time", key.time },
				{ "animationName", key.animationName },
				{ "blendSec", key.blendSec },
				{ "loop", key.loop }
				});
		}
		jTimeline["events"] = json::array();
		for (const PlayerSpecialEventKeyframe& key : timeline.events) {
			jTimeline["events"].push_back({
				{ "time", key.time },
				{ "duration", key.duration },
				{ "type", key.type },
				{ "value", key.value }
				});
		}
		root["playerAttackEditor"]["sideSpecialTimelines"].push_back(std::move(jTimeline));
	}

	std::filesystem::path outputPath(MakeEffectsJsonPath_(path));
	if (outputPath.has_parent_path()) {
		std::filesystem::create_directories(outputPath.parent_path());
	}
	std::ofstream file(outputPath);
	if (file.is_open()) {
		file << root.dump(4);
	}
}

void ParticleTestScene::LoadEffectJson_(GameApp& app, const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}

	json root;
	file >> root;

	EditorSnapshot snapshot;
	snapshot.timelineDuration = root.value("timeline", json::object()).value("duration", 1.0f);
	snapshot.timelineLoop = root.value("timeline", json::object()).value("loop", true);
	snapshot.timelineTime = 0.0f;
	snapshot.selectedObject = -1;
	snapshot.nextObjectId = 1;
	const json cameraJson = root.value("animationCamera", json::object());
	auto cp = cameraJson.value("position", json::array({ 0.0f, 3.0f, -12.0f }));
	auto cr = cameraJson.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
	snapshot.animationCameraPosition = { cp[0], cp[1], cp[2] };
	snapshot.animationCameraRotation = { cr[0], cr[1], cr[2] };
	snapshot.animationCameraFovY = cameraJson.value("fovY", 0.45f);
	snapshot.useAnimationCameraPreview = cameraJson.value("preview", false);
	snapshot.animationCameraPreviewSwapped = cameraJson.value("previewSwapped", false);
	for (const auto& keySource : cameraJson.value("keyframes", json::array())) {
		CameraKeyframe key;
		key.time = keySource.value("time", 0.0f);
		auto kp = keySource.value("position", json::array({ snapshot.animationCameraPosition.x, snapshot.animationCameraPosition.y, snapshot.animationCameraPosition.z }));
		auto kr = keySource.value("rotation", json::array({ snapshot.animationCameraRotation.x, snapshot.animationCameraRotation.y, snapshot.animationCameraRotation.z }));
		key.position = { kp[0], kp[1], kp[2] };
		key.rotation = { kr[0], kr[1], kr[2] };
		key.fovY = keySource.value("fovY", snapshot.animationCameraFovY);
		snapshot.cameraKeyframes.push_back(key);
	}

	for (const auto& source : root.value("objects", json::array())) {
		EditorObjectSnapshot object;
		object.id = source.value("id", snapshot.nextObjectId);
		object.name = source.value("name", std::string("EffectObject"));
		object.modelPath = source.value("modelPath", std::string("cube/cube.obj"));
		object.texturePath = source.value("texturePath", std::string{});
		object.geometryType = source.value("geometryType", -1);
		auto p = source.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
		auto r = source.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
		auto s = source.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
		auto c = source.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }));
		object.position = { p[0], p[1], p[2] };
		object.rotation = { r[0], r[1], r[2] };
		object.scale = { s[0], s[1], s[2] };
		object.color = { c[0], c[1], c[2], c[3] };
		object.blendMode = static_cast<Object3dCommon::BlendMode>(std::clamp(
			source.value("blendMode", static_cast<int>(Object3dCommon::BlendMode::kBlendModeNormal)),
			0,
			static_cast<int>(Object3dCommon::BlendMode::kCountOfBlendMode) - 1));
		object.billboard = source.value("billboard", false);
		object.bloomPostEffect = source.value("bloomPostEffect", false);
		object.outlineBloomPostEffect = source.value("outlineBloomPostEffect", false);
		object.visible = source.value("visible", true);
		auto bc = source.value("bloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
		object.bloomColor = { bc[0], bc[1], bc[2], bc[3] };
		auto obc = source.value("outlineBloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
		object.outlineBloomColor = { obc[0], obc[1], obc[2], obc[3] };
		object.showBones = source.value("showBones", false);
		object.selectedBone = source.value("selectedBone", 0);
		object.attachToBone = source.value("attachToBone", false);
		object.attachParentId = source.value("attachParentId", -1);
		object.attachJointName = source.value("attachJointName", std::string{});
		auto ao = source.value("attachOffset", json::array({ 0.0f, 0.0f, 0.0f }));
		auto ar = source.value("attachRotation", json::array({ 0.0f, 0.0f, 0.0f }));
		auto as = source.value("attachScale", json::array({ 1.0f, 1.0f, 1.0f }));
		object.attachOffset = { ao[0], ao[1], ao[2] };
		object.attachRotation = { ar[0], ar[1], ar[2] };
		object.attachScale = { as[0], as[1], as[2] };

		for (const auto& poseSource : source.value("bonePoses", json::array())) {
			EditorBonePose pose;
			pose.name = poseSource.value("name", std::string{});
			auto bt = poseSource.value("translate", json::array({ 0.0f, 0.0f, 0.0f }));
			auto br = poseSource.value("rotate", json::array({ 0.0f, 0.0f, 0.0f }));
			auto bs = poseSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
			pose.translate = { bt[0], bt[1], bt[2] };
			pose.rotate = { br[0], br[1], br[2] };
			pose.scale = { bs[0], bs[1], bs[2] };
			object.bonePoses.push_back(std::move(pose));
		}

		for (const auto& keySource : source.value("keyframes", json::array())) {
			EffectKeyframe key;
			key.time = keySource.value("time", 0.0f);
			auto kp = keySource.value("position", json::array({ object.position.x, object.position.y, object.position.z }));
			auto kr = keySource.value("rotation", json::array({ object.rotation.x, object.rotation.y, object.rotation.z }));
			auto ks = keySource.value("scale", json::array({ object.scale.x, object.scale.y, object.scale.z }));
			auto kc = keySource.value("color", json::array({ object.color.x, object.color.y, object.color.z, object.color.w }));
			key.position = { kp[0], kp[1], kp[2] };
			key.rotation = { kr[0], kr[1], kr[2] };
			key.scale = { ks[0], ks[1], ks[2] };
			key.color = { kc[0], kc[1], kc[2], kc[3] };
			key.bloomPostEffect = keySource.value("bloomPostEffect", object.bloomPostEffect);
			key.outlineBloomPostEffect = keySource.value("outlineBloomPostEffect", object.outlineBloomPostEffect);
			auto kbc = keySource.value("bloomColor", json::array({ object.bloomColor.x, object.bloomColor.y, object.bloomColor.z, object.bloomColor.w }));
			key.bloomColor = { kbc[0], kbc[1], kbc[2], kbc[3] };
			auto kobc = keySource.value("outlineBloomColor", json::array({ object.outlineBloomColor.x, object.outlineBloomColor.y, object.outlineBloomColor.z, object.outlineBloomColor.w }));
			key.outlineBloomColor = { kobc[0], kobc[1], kobc[2], kobc[3] };
			key.interpolationType = keySource.value("interpolationType", 0);
			for (const auto& poseSource : keySource.value("bonePoses", json::array())) {
				EditorBonePose pose;
				pose.name = poseSource.value("name", std::string{});
				auto bt = poseSource.value("translate", json::array({ 0.0f, 0.0f, 0.0f }));
				auto br = poseSource.value("rotate", json::array({ 0.0f, 0.0f, 0.0f }));
				auto bs = poseSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
				pose.translate = { bt[0], bt[1], bt[2] };
				pose.rotate = { br[0], br[1], br[2] };
				pose.scale = { bs[0], bs[1], bs[2] };
				key.bonePoses.push_back(std::move(pose));
			}
			for (const auto& offsetSource : keySource.value("vertexOffsets", json::array())) {
				uint32_t index = offsetSource.value("index", 0);
				auto offsetVal = offsetSource.value("offset", json::array({ 0.0f, 0.0f, 0.0f }));
				key.vertexOffsets[index] = { offsetVal[0], offsetVal[1], offsetVal[2] };
			}
			object.keyframes.push_back(key);
		}

		// vertexOffsetsのデシリアライズ
		for (const auto& offsetSource : source.value("vertexOffsets", json::array())) {
			uint32_t index = offsetSource.value("index", 0);
			auto offsetVal = offsetSource.value("offset", json::array({ 0.0f, 0.0f, 0.0f }));
			object.vertexOffsets[index] = { offsetVal[0], offsetVal[1], offsetVal[2] };
		}

		snapshot.nextObjectId = std::max(snapshot.nextObjectId, object.id + 1);
		snapshot.objects.push_back(std::move(object));
	}

	for (const auto& nodeSource : root.value("particleNodes", json::array())) {
		ParticleNode node;
		node.name = nodeSource.value("name", "ParticleNode");
		node.isEffectNode = nodeSource.value("nodeType", std::string("particle")) == "effect";
		if (nodeSource.contains("particleFileName")) {
			node.particleFileName = nodeSource.value("particleFileName", "");
		} else {
			node.particleFileName = nodeSource.value("particleGroup", "");
		}
		node.startTime = nodeSource.value("startTime", 0.0f);
		node.endTime = nodeSource.value("endTime", 1.0f);
		auto p = nodeSource.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
		auto r = nodeSource.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
		auto s = nodeSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
		node.position = { p[0], p[1], p[2] };
		node.rotation = { r[0], r[1], r[2] };
		node.scale = { s[0], s[1], s[2] };
		node.emitCount = nodeSource.value("emitCount", 10);
		node.presetDuration = nodeSource.value("presetDuration", 1.0f);
		node.hasEmitted = false;
		snapshot.particleNodes.push_back(std::move(node));
	}

	playerAttackEditorEnabled_ = false;
	drawPlayerAttackHitbox_ = true;
	playerAttackObjectIndex_ = -1;
	playerAttackHitboxKeyframes_.clear();
	currentPlayerAttackHitbox_ = {};
	sideSpecialTimelines_ = {};
	EnsurePlayerSpecialTimelineDefaults_();
	if (root.contains("playerAttackEditor") && root.at("playerAttackEditor").is_object()) {
		const json& attackEditor = root.at("playerAttackEditor");
		playerAttackEditorEnabled_ = attackEditor.value("enabled", false);
		drawPlayerAttackHitbox_ = attackEditor.value("drawHitbox", true);
		playerAttackObjectIndex_ = attackEditor.value("playerObjectIndex", -1);

		if (attackEditor.contains("currentHitbox")) {
			const json& current = attackEditor.at("currentHitbox");
			auto o = current.value("offset", json::array({ 1.0f, 1.0f, 0.0f }));
			auto h = current.value("halfSize", json::array({ 0.6f, 0.8f, 0.5f }));
			currentPlayerAttackHitbox_.offset = { o[0], o[1], o[2] };
			currentPlayerAttackHitbox_.halfSize = { h[0], h[1], h[2] };
			currentPlayerAttackHitbox_.active = current.value("active", true);
		}

		for (const auto& keySource : attackEditor.value("hitboxKeyframes", json::array())) {
			PlayerAttackHitboxKeyframe key;
			key.time = keySource.value("time", 0.0f);
			auto o = keySource.value("offset", json::array({ 1.0f, 1.0f, 0.0f }));
			auto h = keySource.value("halfSize", json::array({ 0.6f, 0.8f, 0.5f }));
			key.offset = { o[0], o[1], o[2] };
			key.halfSize = { h[0], h[1], h[2] };
			key.active = keySource.value("active", true);
			playerAttackHitboxKeyframes_.push_back(key);
		}
		SortPlayerAttackHitboxKeyframes_();

		if (attackEditor.contains("sideSpecialTimelines") && attackEditor.at("sideSpecialTimelines").is_array()) {
			for (const auto& timelineSource : attackEditor.at("sideSpecialTimelines")) {
				const int level = std::clamp(timelineSource.value("level", 0), 0, static_cast<int>(sideSpecialTimelines_.size()) - 1);
				PlayerSpecialTimeline timeline;
				timeline.name = timelineSource.value("name", std::string("SideSpecial Lv") + std::to_string(level));
				timeline.totalSec = timelineSource.value("totalSec", 0.45f);
				for (const auto& keySource : timelineSource.value("hitboxes", json::array())) {
					PlayerSpecialHitboxKeyframe key;
					key.time = keySource.value("time", 0.0f);
					key.duration = keySource.value("duration", 0.08f);
					key.hitStopSec = keySource.value("hitStopSec", 0.14f);
					auto o = keySource.value("offset", json::array({ 1.0f, 1.0f, 0.0f }));
					auto h = keySource.value("halfSize", json::array({ 0.6f, 0.8f, 0.5f }));
					key.offset = { o[0], o[1], o[2] };
					key.halfSize = { h[0], h[1], h[2] };
					key.damage = keySource.value("damage", 12);
					key.active = keySource.value("active", true);
					key.multiHit = keySource.value("multiHit", false);
					timeline.hitboxes.push_back(key);
				}
				for (const auto& keySource : timelineSource.value("motions", json::array())) {
					PlayerSpecialMotionKeyframe key;
					key.time = keySource.value("time", 0.0f);
					key.duration = keySource.value("duration", 0.10f);
					auto v = keySource.value("velocity", json::array({ 0.0f, 0.0f, 0.0f }));
					key.velocity = { v[0], v[1], v[2] };
					key.lockVelocity = keySource.value("lockVelocity", false);
					timeline.motions.push_back(key);
				}
				for (const auto& keySource : timelineSource.value("animations", json::array())) {
					PlayerSpecialAnimationKeyframe key;
					key.time = keySource.value("time", 0.0f);
					key.animationName = keySource.value("animationName", std::string("Attak_O"));
					key.blendSec = keySource.value("blendSec", 0.10f);
					key.loop = keySource.value("loop", false);
					timeline.animations.push_back(key);
				}
				for (const auto& keySource : timelineSource.value("events", json::array())) {
					PlayerSpecialEventKeyframe key;
					key.time = keySource.value("time", 0.0f);
					key.duration = keySource.value("duration", 0.08f);
					key.type = keySource.value("type", 0);
					key.value = keySource.value("value", 1.0f);
					timeline.events.push_back(key);
				}
				sideSpecialTimelines_[level] = std::move(timeline);
			}
			EnsurePlayerSpecialTimelineDefaults_();
		}
	}

	if (!snapshot.objects.empty()) {
		snapshot.selectedObject = 0;
	}
	RestoreEditorSnapshot_(app, snapshot);
	undoStack_.clear();
	redoStack_.clear();
}

bool ParticleTestScene::SavePlayerSpecialTimelinesJson_(
	const std::string& path,
	int attackFilter,
	int levelFilter
)const {

	json root;
	root["version"] = 3;
	root["attacks"] = json::array();
	root["editorBossTarget"] = {
		{ "show", showBossDummy_ },
		{ "showHitbox", showBossDummyHitbox_ },
		{ "matchTestSceneLayout", matchTestSceneLayout_ },
		{ "feetPosition", { bossDummyPosition_.x, bossDummyPosition_.y, bossDummyPosition_.z } },
		{ "bodyHalfSize", { bossDummyHalfSize_.x, bossDummyHalfSize_.y, bossDummyHalfSize_.z } }
	};

	const char* attackNames[] = {
	"NeutralSpecial",
	"SideSpecial",
	"UpSpecial",
	"DownSpecial",
	};

	//全攻撃をJsonへ変換

	for (size_t attackIndex = 0;
		attackIndex < playerSpecialTimelines_.size();
		++attackIndex) {
		if (attackFilter >= 0 && attackIndex != static_cast<size_t>(attackFilter)) continue;

		json attackJson;
		attackJson["type"] = attackNames[attackIndex];
		attackJson["levels"] = json::array();

		for (size_t level = 0;
			level < playerSpecialTimelines_[attackIndex].size(); ++level) {
			if (levelFilter >= 0 && level != static_cast<size_t>(levelFilter)) continue;

			const PlayerSpecialTimeline& timeline =
				playerSpecialTimelines_[attackIndex][level];

			json timelineJson;
			timelineJson["level"] = level;
			timelineJson["name"] = timeline.name;
			timelineJson["totalSec"] = timeline.totalSec;
			timelineJson["freezeBossDuringAttack"] = timeline.freezeBossDuringAttack;
			timelineJson["positionKeyframes"] = json::array();
			timelineJson["hitboxes"] = json::array();
			timelineJson["motionKeyframes"] = json::array();
			timelineJson["opacityKeyframes"] = json::array();
			timelineJson["visualZKeyframes"] = json::array();
			timelineJson["rotationKeyframes"] = json::array();
			timelineJson["animationKeyframes"] = json::array();
			timelineJson["effectKeyframes"] = json::array();
			timelineJson["particleKeyframes"] = json::array();
			timelineJson["eventKeyframes"] = json::array();


			for (const PlayerSpecialPositionKeyframe& key
				: timeline.positionKeyframes) {

				timelineJson["positionKeyframes"].push_back({

					{"time",key.time},
					{"advanceOnHit", key.advanceOnHit},
					{"space", key.space == ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget
						? "bossTarget" : "playerStart"},
					{"interpolation", [&key]() {
						switch (key.interpolation) {
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn: return "easeIn";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut: return "easeOut";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut: return "easeInOut";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::Step: return "step";
						default: return "linear";
						}
					}()},
					{
						"offset",
					  { 
						key.offset.x,
						key.offset.y,
						key.offset.z
			          }
                    }

			    });

			}

			//hitboxを保存

			for (const PlayerSpecialHitboxKeyframe& key
				: timeline.hitboxes) {

				timelineJson["hitboxes"].push_back({
					{ "time",key.time },
					{ "duration",key.duration},
					{ "hitStopSec",key.hitStopSec},
					{
						"offset",
						{
						key.offset.x,
						key.offset.y,
						key.offset.z
					}
				},
				{
					"halfSize",
					{
						key.halfSize.x,
						key.halfSize.y,
						key.halfSize.z
					}
				},
				{ "damage", key.damage},
				{ "active", key.active},
				{ "multiHit", key.multiHit},
				{ "followPlayerMovement", key.followPlayerMovement}
					});
			}

			for (const PlayerSpecialOpacityKeyframe& key : timeline.opacityKeyframes) {
				timelineJson["opacityKeyframes"].push_back({
					{ "time", key.time },
					{ "alpha", key.alpha },
					{ "interpolation", key.interpolation == ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step ? "step" : "linear" }
				});
			}

			for (const PlayerSpecialVisualZKeyframe& key : timeline.visualZKeyframes) {
				timelineJson["visualZKeyframes"].push_back({
					{ "time", key.time },
					{ "offsetZ", key.offsetZ },
					{ "interpolation", [&key]() {
						switch (key.interpolation) {
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn: return "easeIn";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut: return "easeOut";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut: return "easeInOut";
						case ParticleTestEditor::PlayerSpecialPositionInterpolation::Step: return "step";
						default: return "linear";
						}
					}() }
				});
			}

			for (const PlayerSpecialMotionKeyframe& key : timeline.motions) {
				timelineJson["motionKeyframes"].push_back({
					{ "time", key.time }, { "duration", key.duration },
					{ "velocity", { key.velocity.x, key.velocity.y, key.velocity.z } },
					{ "lockVelocity", key.lockVelocity }
				});
			}

			for (const PlayerSpecialRotationKeyframe& key : timeline.rotationKeyframes) {
				timelineJson["rotationKeyframes"].push_back({
					{ "time", key.time },
					{ "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
					{ "interpolation", key.interpolation == ParticleTestEditor::PlayerSpecialRotationInterpolation::Step ? "step" : "linear" }
				});
			}

			for (const PlayerSpecialAnimationKeyframe& key : timeline.animations) {
				timelineJson["animationKeyframes"].push_back({
					{ "time", key.time },
					{ "animationName", key.animationName },
					{ "blendSec", key.blendSec },
					{ "loop", key.loop }
				});
			}

			// In PlayerAttack mode, an Effect Node on the dope sheet belongs to the
			// currently selected attack/level as well. Merge those references into
			// effectKeyframes so the runtime Player can consume them.
			std::vector<PlayerSpecialEffectKeyframe> effectKeys = timeline.effectKeyframes;
			const bool isCurrentAttackLevel =
				attackIndex == static_cast<size_t>(selectedPlayerSpecialAttackType_) &&
				level == static_cast<size_t>(std::clamp(selectedPlayerSpecialLevel_, 0, 3));
			if (playerAttackEditorEnabled_ && isCurrentAttackLevel) {
				for (const ParticleNode& node : particleNodes_) {
					if (!node.isEffectNode || node.particleFileName.empty()) continue;
					const bool alreadyExists = std::any_of(effectKeys.begin(), effectKeys.end(),
						[&node](const PlayerSpecialEffectKeyframe& key) {
							return std::abs(key.time - node.startTime) < 0.001f &&
								std::filesystem::path(key.jsonPath).lexically_normal() ==
								std::filesystem::path(node.particleFileName).lexically_normal();
						});
					if (!alreadyExists) {
						PlayerSpecialEffectKeyframe key;
						key.time = node.startTime;
						key.name = node.name;
						key.jsonPath = node.particleFileName;
						key.offset = node.position;
						effectKeys.push_back(std::move(key));
					}
				}
			}
			std::sort(effectKeys.begin(), effectKeys.end(),
				[](const PlayerSpecialEffectKeyframe& a, const PlayerSpecialEffectKeyframe& b) {
					return a.time < b.time;
				});

			for (const PlayerSpecialEffectKeyframe& key : effectKeys) {
				timelineJson["effectKeyframes"].push_back({
					{ "time", key.time },
					{ "name", key.name },
					{ "jsonPath", key.jsonPath },
					{ "offset", { key.offset.x, key.offset.y, key.offset.z } },
					{ "followPlayerMovement", key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer },
					{ "positionMode", key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint
						? "movementPoint" : (key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer
							? "followPlayer" : "fixedAtSpawn") },
					{ "movementPointIndex", key.movementPointIndex }
				});
			}

			std::vector<PlayerSpecialParticleKeyframe> particleKeys = timeline.particleKeyframes;
			if (playerAttackEditorEnabled_ && isCurrentAttackLevel) {
				for (const ParticleNode& node : particleNodes_) {
					if (node.isEffectNode || node.particleFileName.empty()) continue;
					const bool alreadyExists = std::any_of(particleKeys.begin(), particleKeys.end(),
						[&node](const PlayerSpecialParticleKeyframe& key) {
							return std::abs(key.time - node.startTime) < 0.001f &&
								std::filesystem::path(key.jsonPath).lexically_normal() ==
								std::filesystem::path(node.particleFileName).lexically_normal();
						});
					if (!alreadyExists) {
						PlayerSpecialParticleKeyframe key;
						key.time = node.startTime;
						key.name = node.name;
						key.jsonPath = node.particleFileName;
						key.duration = GetParticleNodeDuration_(node);
						key.position = node.position;
						key.rotation = node.rotation;
						key.scale = node.scale;
						key.emitCount = node.emitCount;
						particleKeys.push_back(std::move(key));
					}
				}
			}
			std::sort(particleKeys.begin(), particleKeys.end(),
				[](const PlayerSpecialParticleKeyframe& a, const PlayerSpecialParticleKeyframe& b) {
					return a.time < b.time;
				});
			for (const PlayerSpecialParticleKeyframe& key : particleKeys) {
				timelineJson["particleKeyframes"].push_back({
					{ "time", key.time }, { "name", key.name }, { "jsonPath", key.jsonPath },
					{ "duration", key.duration }, { "emitCount", key.emitCount },
					{ "position", { key.position.x, key.position.y, key.position.z } },
					{ "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
					{ "scale", { key.scale.x, key.scale.y, key.scale.z } }
				});
			}

			for (const PlayerSpecialEventKeyframe& key : timeline.events) {
				timelineJson["eventKeyframes"].push_back({
					{ "time", key.time }, { "duration", key.duration },
					{ "type", key.type }, { "value", key.value }
				});
			}


			// 位置キーをtimelineJsonへ追加

			attackJson["levels"].push_back(std::move(timelineJson));

		}

		root["attacks"].push_back(std::move(attackJson));

	}


	//フォルダを作成

	std::filesystem::path outputPath(path);
	if (outputPath.has_parent_path()) {
		std::filesystem::create_directories(
			outputPath.parent_path()
		);

	}

	//ファイルを開く
	std::ofstream file(outputPath);

	if (!file.is_open()) {
		return false;
	}

	// ファイルへ書き込む
	file << root.dump(4);

	return file.good();

}

bool ParticleTestScene::LoadPlayerSpecialTimelinesJson_(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) return false;

	try {
		json root;
		file >> root;
		if (!root.contains("attacks") || !root["attacks"].is_array()) return false;

		auto loadedTimelines = playerSpecialTimelines_;
		auto readVector3 = [](const json& owner, const char* name, const Vector3& fallback) {
			if (!owner.contains(name) || !owner[name].is_array() || owner[name].size() < 3) return fallback;
			return Vector3{
				owner[name][0].get<float>(), owner[name][1].get<float>(), owner[name][2].get<float>()
			};
		};
		auto attackIndexFromName = [](const std::string& name) -> int {
			if (name == "NeutralSpecial") return 0;
			if (name == "SideSpecial") return 1;
			if (name == "UpSpecial") return 2;
			if (name == "DownSpecial") return 3;
			return -1;
		};
		if (root.contains("editorBossTarget") && root["editorBossTarget"].is_object()) {
			const json& target = root["editorBossTarget"];
			showBossDummy_ = target.value("show", showBossDummy_);
			showBossDummyHitbox_ = target.value("showHitbox", showBossDummyHitbox_);
			matchTestSceneLayout_ = target.value("matchTestSceneLayout", true);
			bossDummyPosition_ = readVector3(target, "feetPosition", bossDummyPosition_);
			bossDummyHalfSize_ = readVector3(target, "bodyHalfSize", bossDummyHalfSize_);
			bossDummyHalfSize_.x = std::max(0.05f, bossDummyHalfSize_.x);
			bossDummyHalfSize_.y = std::max(0.05f, bossDummyHalfSize_.y);
			bossDummyHalfSize_.z = std::max(0.05f, bossDummyHalfSize_.z);
		}

		for (const json& attackJson : root["attacks"]) {
			const int attackIndex = attackIndexFromName(attackJson.value("type", std::string{}));
			if (attackIndex < 0 || !attackJson.contains("levels") || !attackJson["levels"].is_array()) continue;
			for (const json& timelineJson : attackJson["levels"]) {
				const int level = timelineJson.value("level", -1);
				if (level < 0 || level >= static_cast<int>(loadedTimelines[attackIndex].size())) continue;

				PlayerSpecialTimeline timeline;
				timeline.name = timelineJson.value("name", std::string{});
				timeline.totalSec = std::max(0.05f, timelineJson.value("totalSec", 0.45f));
				const bool legacyFreezeDefault = level == 3 && (attackIndex == 1 || attackIndex == 2);
				timeline.freezeBossDuringAttack =
					timelineJson.value("freezeBossDuringAttack", legacyFreezeDefault);

				for (const json& value : timelineJson.value("positionKeyframes", json::array())) {
					PlayerSpecialPositionKeyframe key;
					key.time = std::max(0.0f, value.value("time", 0.0f));
					key.offset = readVector3(value, "offset", key.offset);
					key.advanceOnHit = value.value("advanceOnHit", false);
					key.space = value.value("space", std::string("playerStart")) == "bossTarget"
						? ParticleTestEditor::PlayerSpecialPositionSpace::BossTarget
						: ParticleTestEditor::PlayerSpecialPositionSpace::PlayerStart;
					const std::string interpolation = value.value("interpolation", std::string("linear"));
					if (interpolation == "easeIn") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn;
					else if (interpolation == "easeOut") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut;
					else if (interpolation == "easeInOut") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut;
					else if (interpolation == "step") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::Step;
					timeline.positionKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("hitboxes", json::array())) {
					PlayerSpecialHitboxKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.duration = std::max(0.0f, value.value("duration", key.duration));
					key.hitStopSec = std::clamp(value.value("hitStopSec", key.hitStopSec), 0.0f, 1.0f);
					key.offset = readVector3(value, "offset", key.offset);
					key.halfSize = readVector3(value, "halfSize", key.halfSize);
					key.damage = value.value("damage", key.damage);
					key.active = value.value("active", key.active);
					key.multiHit = value.value("multiHit", key.multiHit);
					key.followPlayerMovement = value.value("followPlayerMovement", key.followPlayerMovement);
					timeline.hitboxes.push_back(key);
				}

				for (const json& value : timelineJson.value("motionKeyframes", json::array())) {
					PlayerSpecialMotionKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.duration = std::max(0.0f, value.value("duration", key.duration));
					key.velocity = readVector3(value, "velocity", key.velocity);
					key.lockVelocity = value.value("lockVelocity", key.lockVelocity);
					timeline.motions.push_back(key);
				}

				for (const json& value : timelineJson.value("opacityKeyframes", json::array())) {
					PlayerSpecialOpacityKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.alpha = std::clamp(value.value("alpha", key.alpha), 0.0f, 1.0f);
					key.interpolation = value.value("interpolation", std::string("linear")) == "step"
						? ParticleTestEditor::PlayerSpecialOpacityInterpolation::Step
						: ParticleTestEditor::PlayerSpecialOpacityInterpolation::Linear;
					timeline.opacityKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("visualZKeyframes", json::array())) {
					PlayerSpecialVisualZKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.offsetZ = value.value("offsetZ", key.offsetZ);
					const std::string interpolation = value.value("interpolation", std::string("linear"));
					if (interpolation == "easeIn") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseIn;
					else if (interpolation == "easeOut") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseOut;
					else if (interpolation == "easeInOut") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::EaseInOut;
					else if (interpolation == "step") key.interpolation = ParticleTestEditor::PlayerSpecialPositionInterpolation::Step;
					timeline.visualZKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("rotationKeyframes", json::array())) {
					PlayerSpecialRotationKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.rotation = readVector3(value, "rotation", key.rotation);
					key.interpolation = value.value("interpolation", std::string("linear")) == "step"
						? ParticleTestEditor::PlayerSpecialRotationInterpolation::Step
						: ParticleTestEditor::PlayerSpecialRotationInterpolation::Linear;
					timeline.rotationKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("animationKeyframes", json::array())) {
					PlayerSpecialAnimationKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.animationName = value.value("animationName", key.animationName);
					key.blendSec = std::max(0.0f, value.value("blendSec", key.blendSec));
					key.loop = value.value("loop", key.loop);
					timeline.animations.push_back(key);
				}

				for (const json& value : timelineJson.value("effectKeyframes", json::array())) {
					PlayerSpecialEffectKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.name = value.value("name", key.name);
					key.jsonPath = value.value("jsonPath", key.jsonPath);
					key.offset = readVector3(value, "offset", key.offset);
					key.followPlayerMovement = value.value("followPlayerMovement", true);
					const std::string positionMode = value.value("positionMode",
						key.followPlayerMovement ? std::string("followPlayer") : std::string("fixedAtSpawn"));
					if (positionMode == "movementPoint") {
						key.positionMode = ParticleTestEditor::PlayerSpecialEffectPositionMode::MovementPoint;
					} else if (positionMode == "followPlayer") {
						key.positionMode = ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer;
					} else {
						key.positionMode = ParticleTestEditor::PlayerSpecialEffectPositionMode::FixedAtSpawn;
					}
					key.followPlayerMovement =
						key.positionMode == ParticleTestEditor::PlayerSpecialEffectPositionMode::FollowPlayer;
					key.movementPointIndex = value.value("movementPointIndex", -1);
					timeline.effectKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("particleKeyframes", json::array())) {
					PlayerSpecialParticleKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.name = value.value("name", key.name);
					key.jsonPath = value.value("jsonPath", key.jsonPath);
					key.duration = std::max(0.01f, value.value("duration", key.duration));
					key.emitCount = std::max(1, value.value("emitCount", key.emitCount));
					key.position = readVector3(value, "position", key.position);
					key.rotation = readVector3(value, "rotation", key.rotation);
					key.scale = readVector3(value, "scale", key.scale);
					timeline.particleKeyframes.push_back(key);
				}

				for (const json& value : timelineJson.value("eventKeyframes", json::array())) {
					PlayerSpecialEventKeyframe key;
					key.time = std::max(0.0f, value.value("time", key.time));
					key.duration = std::max(0.0f, value.value("duration", key.duration));
					key.type = value.value("type", key.type);
					key.value = value.value("value", key.value);
					timeline.events.push_back(key);
				}

				loadedTimelines[attackIndex][level] = std::move(timeline);
			}
		}

		playerSpecialTimelines_ = std::move(loadedTimelines);
		EnsurePlayerSpecialTimelineDefaults_();
		SortCurrentPlayerSpecialTimeline_();
		selectedPlayerSpecialPositionKey_ = -1;
		PlayerSpecialTimeline& current = CurrentPlayerSpecialTimeline_();
		timelineDuration_ = current.totalSec;
		timelineTime_ = std::clamp(timelineTime_, 0.0f, timelineDuration_);
		if (!current.hitboxes.empty()) currentSpecialHitbox_ = current.hitboxes.front();
		if (!current.motions.empty()) currentSpecialMotion_ = current.motions.front();
		if (!current.positionKeyframes.empty()) currentSpecialPosition_ = current.positionKeyframes.front();
		if (!current.opacityKeyframes.empty()) currentSpecialOpacity_ = current.opacityKeyframes.front();
		if (!current.visualZKeyframes.empty()) currentSpecialVisualZ_ = current.visualZKeyframes.front();
		if (!current.rotationKeyframes.empty()) currentSpecialRotation_ = current.rotationKeyframes.front();
		if (!current.animations.empty()) currentSpecialAnimation_ = current.animations.front();
		if (!current.effectKeyframes.empty()) currentSpecialEffect_ = current.effectKeyframes.front();
		SyncPlayerSpecialPreviewNodes_();
		EvaluatePlayerSpecialTimeline_();
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

