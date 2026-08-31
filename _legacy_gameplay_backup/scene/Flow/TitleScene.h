#pragma once
#include "IScene.h"
#include <memory>
#include "Matrix4x4.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Sprite.h"
#include <array>     
#include <string>   
#include "SpriteCommon.h"
#include "Particle.h"
#include "VideoPlayerMF.h"

#include "Player.h"
#include "Skybox.h"

#include "GeometryGenerator.h"

class Particle;
class Camera;

class TitleScene : public IScene {
public:
	void OnEnter(GameApp& app) override;
	void OnExit(GameApp& app) override;

	void Update(GameApp& app, float dt) override;
	void DrawRender(GameApp& app) override;
	void Draw3D(GameApp& app) override;
	void Draw2D(GameApp& app) override;
	void Draw(GameApp& app) override;
	void DrawImGui(GameApp& app) override;

private:

private:
	void DrawImGui_ModelSwitchersOneWindow();
	void DrawImGui_ModelSwitchBlock(const char* header,
		const char* comboLabel,
		int& index,
		const char* const* paths,
		int count,
		const char* const* labels,
		int labelCount);
	void UpdateTitleBoneDebug_();


	bool prevSpace_ = false;

	std::unique_ptr<Camera> camera_;

	Vector3 imguiCamPos_ = { 0.0f, 3.0f, -20.0f };
	Vector3 imguiCamRot_ = { 0.0f, 0.0f, 0.0f };

	std::unique_ptr<Particle> particle_;

	std::unique_ptr<Object3d> skyDome_;

	std::unique_ptr < Sprite> bg_;

	std::unique_ptr < Sprite> pressStart_;

	enum class State {
		Idle,
		ExitClose
	};

	State state_ = State::Idle;

	float circle_ = 1.0f;   // ★Titleは最初から開いている
	float softness_ = 0.6f;

	const char* kNextScene_ = "Game"; // SPACE後に行く先

	//確認
	std::unique_ptr<Object3d> testObj_;
	std::unique_ptr<Object3d> terrainObj_;
	std::unique_ptr<Object3d> nodeObj_;

	Vector3 testPos_{ 0.0f, 1.0f, 0.0f };
	Vector3 testRot_{ 0.0f, 0.0f, 0.0f };   // ラジアンなら 0.01刻み、度なら 1.0刻み
	Vector3 testScale_{ 2.0f, 2.0f, 2.0f };

	// 確認用パラメータ
	float shininess_ = 64.0f;
	int lightingMode_ = 2;     // 1:Lambert 2:HalfLambert 3:SpecOnly
	bool orbitCam_ = false;
	float orbitSpeed_ = 0.6f;
	float orbitRadius_ = 10.0f;
	float orbitT_ = 0.0f;

	Vector3 lightDir_ = { 0.0f, -1.0f, -1.0f };
	float   lightIntensity_ = 0.0f;
	Vector4 lightColor_ = { 1,1,1,1 };

	Vector3 pointPos_{ 0.0f, 3.0f, -3.0f };
	Vector4 pointColor_{ 1,1,1,1 };
	float   pointIntensity_ = 1.0f;
	float pointRadius_ = 10.0f;
	float pointDecay_ = 1.0f;

	// 確認用：Pointだけ見る
	int lightViewMode_ = 0; // 0:全部 1:Directionalのみ 2:Pointのみ

	// ---- SpotLight (Debug) ----
	Vector3 spotPos_ = { 0.0f, 3.0f, 0.0f };
	Vector3 spotDir_ = { 0.0f, -1.0f, 0.0f };      // ※正規化推奨
	float   spotIntensity_ = 2.0f;
	float   spotDistance_ = 15.0f;
	float   spotDecay_ = 1.0f;
	float spotAngleDeg_ = 30.0f;         // 外側
	float spotFalloffStartDeg_ = 15.0f;  // ★内側（外側より小さく）
	Vector3 spotColor_ = { 1.0f, 1.0f, 1.0f }; // RGB
	float spotCos;

	enum class EditTarget {
		SkyDome,
		VideoPlane,
		Particle,
		Ground,      // 追加
		TitlePlayer, // 追加
		BG,
		PressStart,
	};



	int editTarget_ = 0; // ImGui Combo用（EditTarget を int で持つ）

	struct SRT {
		Vector3 pos{ 0.0f, 1.0f, 0.0f };
		Vector3 rot{ 0.0f, 0.0f, 0.0f };
		Vector3 scale{ 2.0f, 2.0f, 2.0f };
	};

	SRT srtTest_{};
	SRT srtTerrain_{};
	SRT srtNode_{};
	SRT srtNodeMisc_{};
	SRT srtSkin_{};
	SRT srtMeshPrim_{};
	SRT srtAlphaBlend_{};
	SRT srtTexSampler_{};
	SRT srtSky_{};
	SRT srtVideo_{};
	SRT srtParticle_{};
	SRT srtBG_{};
	SRT srtPress_{};


	// === Assimp plane 切替 ===
	std::array<const char*, 2> assimpPlanePaths_ = {
		"plane.obj",
		"plane.gltf",
	};
	int assimpPlaneIndex_ = 0;
	int assimpPlaneIndexPrev_ = 0;

	std::unique_ptr<Object3d> videoPlane_;
	std::unique_ptr<Player> titlePlayer;
	std::unique_ptr<VideoPlayerMF> video_; // もしくは値型でもOK
	bool enableVideo_ = true;              // ImGuiでON/OFFできるように

	std::unique_ptr<Skybox> skybox_;

	// ===== Lighting params =====
	LightingParam light_;

	std::unique_ptr<Object3d> ground_;

	// TitleScene.h の private に追加
	bool showVideo_ = true;        // true: Video表示 / false: Player表示
	float showTimer_ = 0.0f;       // 経過時間
	float switchInterval_ = 2.0f;  // 何秒ごとに切り替えるか（好み）
	float dt_;

	float switchT_ = 0.0f;
	float playerSec_ = 28.0f; // 前半プレイヤー秒
	float videoSec_ = 28.0f; // 後半動画秒

	SRT srtGround_{};
	SRT srtPlayer_{};

	// Title player bone editor (PMX / glTF / GLB 共通)
	int selectedBone_ = 0;
	Vector3 boneTranslate_{ 0.0f, 0.0f, 0.0f };
	Vector3 boneRotate_{ 0.0f, 0.0f, 0.0f };
	Vector3 boneScale_{ 1.0f, 1.0f, 1.0f };
	bool showBones_ = false;

public:
	//パーティクル用


private:
	//パーティクル用

	enum class PrimitiveType {
		Ring,
		Sphere,
		Box,
		Plane,
		Torus,
		Cylinder,
		Cone,
		Triangle,
	};

	std::unique_ptr<Object3d> primitiveObj_;

	int primitiveTypeIndex_ = 0;
	int primitiveTypePrevIndex_ = -1;

	std::array<const char*, 8> primitiveLabels_ = {
		"Ring",
		"Sphere",
		"Box",
		"Plane",
		"Torus",
		"Cylinder",
		"Cone",
		"Triangle"
	};

	SRT srtPrimitive_{};

	//再構築関数
	void RebuildPrimitive_();

};
