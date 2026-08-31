#include "TitleScene.h"
#include <Windows.h>

#include "GameApp.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "Camera.h"


namespace {

	std::vector<Model::VertexData> MakePrimitiveVertices(int typeIndex) {
		switch (typeIndex) {
		case 0: return GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f);
		case 1: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
		case 2: return GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f);
		case 3: return GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f);
		case 4: return GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f);
		case 5: return GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f);
		case 6: return GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f);
		case 7: return GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f);
		default: return GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f);
		}
	}

}

namespace {

	Model::ModelData MakePrimitiveModelData(const std::vector<Model::VertexData>& vertices) {
		Model::ModelData md{};

		md.materials.push_back({ "" });

		Model::MeshData mesh{};
		mesh.materialIndex = 0;
		mesh.vertices = vertices;
		mesh.skinned = false;
		mesh.startVertex = 0;
		mesh.vertexCount = static_cast<uint32_t>(vertices.size());
		mesh.startIndex = 0;
		mesh.indexCount = static_cast<uint32_t>(vertices.size());

		md.meshes.push_back(std::move(mesh));

		// 非indexedでも今の Model 側が扱いやすいように index を連番で入れておく
		md.indices.resize(vertices.size());
		for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
			md.indices[i] = i;
		}

		md.rootNode.name = "PrimitiveRoot";
		md.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
		md.rootNode.meshIndices.push_back(0);

		return md;
	}

}

void TitleScene::RebuildPrimitive_() {
	if (!primitiveObj_) {
		return;
	}

	auto vertices = MakePrimitiveVertices(primitiveTypeIndex_);
	auto modelData = MakePrimitiveModelData(vertices);

	std::string key = "primitive_" + std::to_string(primitiveTypeIndex_);

	Model* model = ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
	primitiveObj_->SetModel(model);

	primitiveObj_->SetEnableLighting(2);
	primitiveObj_->SetMaterialColor({ 1,1,1,1 });
	primitiveObj_->SetShininess(64.0f);
}

void TitleScene::OnEnter(GameApp& app) {

	if (auto* input = app.GetInput()) {
		input->SetCameraControlEnabled(false);
	}
	app.Render()->SetMode(PostEffectMode::FullScreen);

	state_ = State::Idle;
	circle_ = 1.0f;     // 最初は表示されている
	softness_ = 0.6f;
	prevSpace_ = false;

	TextureManager::GetInstance()->LoadTexture("resources/ui/char/title.png");


	// カメラ（Particle は内部カメラでも動くけど、外部カメラを渡すと安定）
	camera_ = std::make_unique<Camera>();
	camera_->SetRotate({ 0, 0, 0 });
	camera_->SetTranslate({ 0, 3, -20 });

	app.ObjCom()->SetDefaultCamera(camera_.get());

	//player
	titlePlayer = std::make_unique<Player>();
	titlePlayer->Initialize(app.ObjCom(), app.Dx(), camera_.get());
	titlePlayer->ChangeModelSet_(Player::PlayerModelSet::Player2Gltf);
	titlePlayer->SetSpawnPos({ -12.0f, 0.0f, 5.0f }); // 好みで調整
	titlePlayer->SetLighting(light_); // 操作禁止
	titlePlayer->ResetTitleAttackDemo();
	UpdateTitleBoneDebug_();

	//ground
	ground_ = std::make_unique<Object3d>();
	ground_->Initialize(app.ObjCom(), app.Dx());
	ground_->SetCamera(camera_.get());
	ground_->SetModel("ground/ground.obj");

	// 位置・大きさは好みで調整
	ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
	ground_->SetScale({ 1.0f, 1.0f, 1.0f });
	ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
	ground_->SetEnableLighting(0);//ライティング設定
	ground_->SetUseEnvironmentMap(true);//環境マップ有効か
	ground_->SetEnvironmentCoefficient(0.5f);//環境マップの影響度（0.0f〜1.0f）
	//ground_->SetEnvironmentTexturePath("resources/skybox/skybox.dds");

	ground_->SetEnableDissolve(true);
	ground_->SetDissolveThreshold(0.5f);
	ground_->SetMaskTexturePath("resources/noise0.png");

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(app.SkyboxCom(), app.Dx());
	skybox_->SetCamera(camera_.get());
	skybox_->SetScale({ 500.0f, 500.0f, 500.0f });


	skybox_->SetTextureHandle(
		TextureManager::GetInstance()->GetSrvHandleGPU("resources/skybox/skybox.dds")
	);

	// Particle
	particle_ = std::make_unique<Particle>();
	particle_->Initialize(app.ParticleCom(), app.Dx(), app.Srv());

	// ★これが無いと「model_ が null」で Draw しても何も出ません
	particle_->SetModel("plane.obj");
	assimpPlaneIndexPrev_ = assimpPlaneIndex_;

	// カメラを渡せるなら渡す（Particle.cpp は camera_ があればそれを使う仕様）
	particle_->SetCamera(camera_.get());
	Vector3 rotate = { 0.0f,0.0f,0.0f };

	particle_->SetRotate(rotate);
	// 見えやすいように（任意）
	particle_->SetBlendMode(ParticleCommon::BlendMode::kBlendModeAdd);
	particle_->SetMaterialColor({ 1, 1, 1, 1 });

	// GPU Particle 初期化（テスト用）
	// Disabled: debug GPU particle group is not required in TitleScene.


	//primitive
	primitiveObj_ = std::make_unique<Object3d>();
	primitiveObj_->Initialize(app.ObjCom(), app.Dx());
	primitiveObj_->SetPrimitiveCommon(app.PrimitiveCom());
	primitiveObj_->SetCamera(camera_.get());
	primitiveObj_->SetEnableLighting(0);
	primitiveObj_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
	primitiveObj_->SetTexture("resources/gradationLine.png");
	primitiveObj_->SetShininess(64.0f);
	primitiveObj_->SetMaterialColor({ 1,1,1,0.3f });

	srtPrimitive_.pos = { 0.0f, 1.0f, 0.0f };
	srtPrimitive_.rot = { 0.0f, 0.0f, 0.0f };
	srtPrimitive_.scale = { 2.0f, 2.0f, 2.0f };

	skyDome_ = std::make_unique<Object3d>();
	skyDome_->Initialize(app.ObjCom(), app.Dx());
	skyDome_->SetModel("skydome/SkyDome.obj");

	// ★スカイドームは基本「ライト無視」
	skyDome_->SetEnableLighting(0);              // ← あなたの仕様の「無照明モード」に合わせて
	skyDome_->SetMaterialColor({ 1,1,1,1 });       // 念のため
	skyDome_->SetShininess(1.0f);                // 影響しないけど保険
	skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

	bg_ = std::make_unique<Sprite>();
	bg_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/Title.png");
	bg_->SetAnchorPoint({ 0,0 });
	bg_->SetPosition({ 0,0 });
	bg_->SetScale({ 1,1,1 }); // 1280x720ならそのまま


	pressStart_ = std::make_unique<Sprite>();
	pressStart_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/pressSpace.png"); // 例
	pressStart_->SetAnchorPoint({ 0, 0 });
	pressStart_->SetPosition({ 100,100 });
	pressStart_->SetScale({ 1,1,1 }); // 128x128なら等倍でOK

	// ===== Video Plane =====
	videoPlane_ = std::make_unique<Object3d>();
	videoPlane_->Initialize(app.ObjCom(), app.Dx());
	videoPlane_->SetModel("video/plane.obj");

	// 表示しやすい設定（ライト無視）
	videoPlane_->SetEnableLighting(0);
	videoPlane_->SetMaterialColor({ 1,1,1,1 });
	videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

	// 位置調整（カメラの前に置く）
	videoPlane_->SetTranslate({ 0.0f, 3.0f, 3.1f });  // 例：Zはカメラ向きに合わせて調整
	videoPlane_->SetScale({ 9.5f, 5.3f, 2.0f });
	videoPlane_->SetRotate({ 0.0f, 3.17f, 0.0f });

	// ===== Video Player =====
	video_ = std::make_unique<VideoPlayerMF>();

	// ※ ここはあなたの実装の関数名に合わせて
	video_->Open("resources/video/title.mp4", true); // loop = true
	video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv()); // SRV確保 + texture作成
	video_->SetVolume(1.5f); // 音量セット（0.0f〜1.0f）
	enableVideo_ = true;

	bool ok = video_->ReadNextFrame();  // ★最初の1枚
	if (!ok) {
		OutputDebugStringA("[TitleScene] First ReadNextFrame failed\n");
	}


	// Sky
	srtSky_.pos = { 0,0,0 };
	srtSky_.rot = { 0,0,0 };
	srtSky_.scale = { 1,1,1 };

	// VideoPlane（あなたの初期値と合わせる）
	srtVideo_.pos = { 0.0f, 3.0f, 3.1f };
	srtVideo_.scale = { 9.5f, 5.3f, 2.0f };
	srtVideo_.rot = { 0.0f, 3.14f, 0.0f };

	// BG / Press（2D）
	srtBG_.pos = { 0,0,0 };
	srtBG_.scale = { 1,1,1 };
	srtPress_.pos = { 0,0,0 };
	srtPress_.scale = { 1,1,1 };

	dt_ = 0.0f;

	// Ground（いま SetTranslate/Scale/Rotate してる値を反映）
	srtGround_.pos = { 0.0f, -5.0f, 0.0f };
	srtGround_.rot = { 0.0f,  1.34f, 0.0f };
	srtGround_.scale = { 1.0f,  1.0f, 1.0f };

	// Player（SetSpawnPos の値を反映。回転/スケールはとりあえず既定）
	srtPlayer_.pos = { -0.2f, 0.2f, -5.1f };
	srtPlayer_.rot = { 0.0f, 1.14f, 0.0f };
	srtPlayer_.scale = { 2.0f, 2.0f, 2.0f };

	// 切替初期化（最初はプレイヤー表示）
	showVideo_ = false;     // ← 最初は titlePlayer を出す
	switchT_ = 0.0f;        // ← タイマーリセット

	primitiveTypePrevIndex_ = -1;
	RebuildPrimitive_();
	primitiveTypePrevIndex_ = primitiveTypeIndex_;

}

void TitleScene::OnExit(GameApp&) {
	enableVideo_ = false;
}

void TitleScene::Update(GameApp& app, float dt) {

	const Input* input = app.GetInput();

	// ESC で終了
	if (input->IsKeyTrigger(DIK_ESCAPE)) {
		app.RequestQuit();
		return;
	}

	skybox_->Update();

	bool spaceNow = input->IsKeyPressed(DIK_SPACE);
	bool spaceTrig = input->IsKeyTrigger(DIK_SPACE);
	prevSpace_ = spaceNow;

	dt_ = dt;

	if (input->IsKeyTrigger(DIK_D)) {

		RequestChangeScene_("GameOver");

	}

	if (input->IsKeyTrigger(DIK_P)) {
		RequestChangeScene_("ParticleTest");
		return;
	}

	if (input->IsKeyTrigger(DIK_M)) {
		RequestChangeScene_("CGTest");
		return;
	}

	if (input->IsKeyTrigger(DIK_F9)) {
		RequestChangeScene_("DebugAITest");
		return;
	}

	if (input->IsKeyTrigger(DIK_T)) {
		RequestChangeScene_("Test");
		return;
	}

	switch (state_) {
	case State::Idle:
		if (spaceTrig) {
			state_ = State::ExitClose; // ★すぐ遷移しない
		}
		break;

	case State::ExitClose:
		circle_ -= 1.8f * dt;          // ★閉じる
		if (circle_ <= 0.0f) {
			circle_ = 0.0f;
			RequestChangeScene_(kNextScene_);
		}
		break;
	}


	// 3D更新など…
	skyDome_->Update(dt);
	ground_->Update(dt);

	// 動画のデコード/音声は「表示中のみ」
	if (enableVideo_ && video_ && showVideo_) {

		if (videoPlane_) {
			videoPlane_->SetTranslate(srtVideo_.pos);
			videoPlane_->SetRotate(srtVideo_.rot);
			videoPlane_->SetScale(srtVideo_.scale);
			videoPlane_->Update(dt);
		}

		video_->ReadNextVideoFrame();
		video_->PumpAudio();
	}


	// ---- 表示切替（自前タイマーで安定） ----
	switchT_ += dt;

	if (showVideo_) {
		// 動画表示中
		if (switchT_ >= videoSec_) {
			switchT_ = 0.0f;
			showVideo_ = false; // 次はプレイヤー
		}
	} else {
		// プレイヤー表示中
		if (switchT_ >= playerSec_) {
			switchT_ = 0.0f;
			showVideo_ = true; // 次は動画
		}
	}



	// タイトル用プレイヤーがいるならデモ再生
	if (titlePlayer) {
		UpdateTitleBoneDebug_();
		titlePlayer->UpdateTitleAttackDemo(dt, 1.0f); // 1秒ごとに I/O 交互
	}

	if (titlePlayer) {
		titlePlayer->SetTitleTransform(srtPlayer_.pos, srtPlayer_.rot, srtPlayer_.scale);
	}

	if (primitiveTypeIndex_ != primitiveTypePrevIndex_) {
		RebuildPrimitive_();
		primitiveTypePrevIndex_ = primitiveTypeIndex_;
	}

	if (primitiveObj_) {
		primitiveObj_->SetTranslate(srtPrimitive_.pos);
		primitiveObj_->SetRotate(srtPrimitive_.rot);
		primitiveObj_->SetScale(srtPrimitive_.scale);
		primitiveObj_->Update(dt);
	}



	spotCos = std::cosf(spotAngleDeg_ * (std::numbers::pi_v<float> / 180.0f));

	// ---- Spot angle deg -> cos ----
//    const float kPi = 3.14159265f;

	// 外側は spotAngleDeg_
// 内側は spotFalloffStartDeg_（必ず外側より小さく）
	spotFalloffStartDeg_ = std::min(spotFalloffStartDeg_, spotAngleDeg_ - 0.1f);

	float cosOuter = std::cosf(spotAngleDeg_ * (std::numbers::pi_v<float> / 180.0f));
	float cosInner = std::cosf(spotFalloffStartDeg_ * (std::numbers::pi_v<float> / 180.0f));

	// cosは「角度が小さいほど大きい」ので、内側のcosは外側より大きいのが正しい
	// cosInner > cosOuter になってる状態が正常



	if (orbitCam_ && camera_) {
		orbitT_ += orbitSpeed_ * dt;

		Vector3 target{ 0.0f, 1.0f, 0.0f }; // testObjを見る想定
		Vector3 eye{
			target.x + std::cosf(orbitT_) * orbitRadius_,
			target.y + 3.0f,
			target.z + std::sinf(orbitT_) * orbitRadius_
		};

		camera_->SetTranslate(eye);

		// 回転は「LookAt」関数が無いなら、いったん手動でYawだけでもOK
		// ここはあなたのCamera仕様次第なので、とりあえずImGui回転を残す運用でもOK
	}

	int lighting = lightingMode_;
	if (lightViewMode_ == 1) lighting = 11; // DirOnly
	if (lightViewMode_ == 2) lighting = 12; // PointOnly



	// ===== カメラ反映 =====
	if (camera_) {
		if (orbitCam_) {
			orbitT_ += orbitSpeed_ * dt;

			Vector3 target{ 0.0f, 1.0f, 0.0f };
			Vector3 eye{
				target.x + std::cosf(orbitT_) * orbitRadius_,
				target.y + 3.0f,
				target.z + std::sinf(orbitT_) * orbitRadius_
			};

			camera_->SetTranslate(eye);
			camera_->SetRotate(imguiCamRot_); // 仮
		} else {
			camera_->SetTranslate(imguiCamPos_);
			camera_->SetRotate(imguiCamRot_);
		}

		camera_->Update();
	}

	auto ApplyObject3dSRT = [](Object3d* o, const SRT& s) {
		if (!o) return;
		o->SetTranslate(s.pos);
		o->SetRotate(s.rot);   // ※Object3dがdeg前提ならOK。radなら変換が必要。
		o->SetScale(s.scale);
		};

	auto ApplySpriteSRT = [](Sprite* sp, const SRT& s) {
		if (!sp) return;
		// Spriteが2D(Vector2)なら適宜合わせる
		sp->SetPosition({ s.pos.x, s.pos.y });
		sp->SetScale({ s.scale.x, s.scale.y, 1.0f }); // あなたのSpriteがVector3ならそのまま
		// Spriteに回転があるなら sp->SetRotation(s.rot.z) など
		};

	// 3D
	ApplyObject3dSRT(skyDome_.get(), srtSky_);
	ApplyObject3dSRT(videoPlane_.get(), srtVideo_);
	//if (particle_) {
	//	particle_->SetTranslate(srtParticle_.pos);
	//	particle_->SetRotate(srtParticle_.rot);
	//	particle_->SetScale(srtParticle_.scale); // Particleに無ければ外す
	//}

	// 2D
	ApplySpriteSRT(bg_.get(), srtBG_);
	//ApplySpriteSRT(pressStart_.get(), srtPress_);
	ApplyObject3dSRT(ground_.get(), srtGround_);


	if (particle_) {
		particle_->Update();
	}

	// GPU Particle 更新
	// Disabled: debug GPU particle update is not required in TitleScene.
}

void TitleScene::UpdateTitleBoneDebug_()
{
	Object3d* playerObject = titlePlayer ? titlePlayer->GetModelObject() : nullptr;
	if (!playerObject) {
		return;
	}

	// タイトルシーンでは骨デバッグ描画を使用しない。
	playerObject->SetDebugDrawBones(false);
}

//========================
//レンダー反映するモデル
//========================
void TitleScene::DrawRender(GameApp& app)
{
	//if (skyDome_) skyDome_->Draw();

	//if (!showVideo_) {
	//	if (ground_) ground_->Draw();
	//	if (titlePlayer) titlePlayer->Draw();
	//}

	//if (enableVideo_ && videoPlane_ && video_ && showVideo_) {
		/*auto* cmd = app.Dx()->GetCommandList();

		video_->UploadToGpu(cmd);

		D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
		videoPlane_->DrawWithOverrideSrv(vh);

		video_->EndFrame(cmd);*/
	//}

	skybox_->Draw();

	if (ground_) ground_->Draw();
	if (titlePlayer) titlePlayer->Draw();

	//if (primitiveObj_) primitiveObj_->Draw();

	// GPU Particle 描画
	// Disabled: debug GPU particle draw is not required in TitleScene.



}

//========================
//レンダー反映しないモデル
//========================
void TitleScene::Draw3D(GameApp& app)
{
//	skybox_->Draw();

	////if (skyDome_) skyDome_->Draw();

	////if (!showVideo_) {
//	if (ground_) ground_->Draw();
//	if (titlePlayer) titlePlayer->Draw();
	////}

	if (enableVideo_ && videoPlane_ && video_ && showVideo_) {
		auto* cmd = app.Dx()->GetCommandList();

		video_->UploadToGpu(cmd);

		D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
		videoPlane_->DrawWithOverrideSrv(vh);

		video_->EndFrame(cmd);
	}

	//if (primitiveObj_) primitiveObj_->Draw();

	//if (particle_) {
	//	app.ParticleCom()->SetGraphicsPipelineState();
	//	particle_->Draw();
	//}
	//
	//// GPU Particle 描画
	//ParticleManager::GetInstance()->Draw(app.Dx()->GetCommandList());
	

}

void TitleScene::Draw2D(GameApp& app)
{
	app.SpriteCom()->SetGraphicsPipelineState();

	Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
	Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
		0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

	if (bg_) {
		bg_->Update(view, proj);
		bg_->Draw();
	}

	if (pressStart_) {
		pressStart_->Update(view, proj);
		pressStart_->Draw();
	}

}

void TitleScene::Draw(GameApp& app) {

	app.SpriteCom()->DrawCircleMask(circle_, softness_);


}

void TitleScene::DrawImGui_ModelSwitchBlock(const char* header,
	const char* comboLabel,
	int& index,
	const char* const* paths,
	int count,
	const char* const* labels,
	int labelCount)
{
	// 折りたたみ（好きなら TreeNode でもOK）
	if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {

		if (labels && labelCount == count) {
			ImGui::Combo(comboLabel, &index, labels, labelCount);
		} else {
			ImGui::Combo(comboLabel, &index, paths, count);
		}

		ImGui::Text("Path: %s", paths[index]);
		ImGui::Separator();
	}
}

void TitleScene::DrawImGui_ModelSwitchersOneWindow()
{
	ImGui::Begin("Model Switchers");  // ★ウィンドウはこれ1個だけ


	// Assimp Plane
	{
		static const char* labelsPlane[] = { "plane.obj", "plane.gltf" };
		DrawImGui_ModelSwitchBlock(
			"Assimp Plane",
			"Plane##Assimp",
			assimpPlaneIndex_,
			assimpPlanePaths_.data(),
			(int)assimpPlanePaths_.size(),
			labelsPlane,
			IM_ARRAYSIZE(labelsPlane)
		);
	}

	ImGui::End();
}

void TitleScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI

	// ===== ImGui =====
	ImGui::Begin("Camera Debug");

	ImGui::DragFloat3("Position", &imguiCamPos_.x, 0.1f);
	ImGui::DragFloat3("Rotation", &imguiCamRot_.x, 0.01f);

	ImGui::End();

	ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Title Bone Control (PMX / glTF / GLB)")) {
		Object3d* playerObject = titlePlayer ? titlePlayer->GetModelObject() : nullptr;
		const Model::Skeleton* skeleton =
			playerObject && playerObject->HasSkinningModel() ? playerObject->GetSkeleton() : nullptr;

		if (!skeleton || skeleton->joints.empty()) {
			ImGui::TextUnformatted("Skinning bones are not ready.");
		} else {
			selectedBone_ = std::clamp(selectedBone_, 0, static_cast<int>(skeleton->joints.size()) - 1);
			const char* preview = skeleton->joints[selectedBone_].name.c_str();

			ImGui::Text("Bones: %d", static_cast<int>(skeleton->joints.size()));
			if (ImGui::BeginCombo("Bone", preview)) {
				for (int i = 0; i < static_cast<int>(skeleton->joints.size()); ++i) {
					const bool selected = i == selectedBone_;
					if (ImGui::Selectable(skeleton->joints[i].name.c_str(), selected)) {
						selectedBone_ = i;
						boneTranslate_ = { 0.0f, 0.0f, 0.0f };
						boneRotate_ = { 0.0f, 0.0f, 0.0f };
						boneScale_ = { 1.0f, 1.0f, 1.0f };
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			bool changed = false;
			changed |= ImGui::DragFloat3("Translate", &boneTranslate_.x, 0.01f, 0.0f, 0.0f);
			changed |= ImGui::DragFloat3("Rotate (rad)", &boneRotate_.x, 0.01f, -6.283f, 6.283f);
			changed |= ImGui::DragFloat3("Scale", &boneScale_.x, 0.01f, 0.01f, 10.0f);
			if (changed) {
				playerObject->SetManualJointTransform(
					skeleton->joints[selectedBone_].name,
					boneTranslate_,
					boneRotate_,
					boneScale_);
			}

			ImGui::TextDisabled("Bone rendering is disabled in TitleScene.");

			if (ImGui::Button("Reset Selected")) {
				boneTranslate_ = { 0.0f, 0.0f, 0.0f };
				boneRotate_ = { 0.0f, 0.0f, 0.0f };
				boneScale_ = { 1.0f, 1.0f, 1.0f };
				playerObject->SetManualJointTransform(
					skeleton->joints[selectedBone_].name,
					boneTranslate_,
					boneRotate_,
					boneScale_);
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset All")) {
				playerObject->ResetManualJointTransforms();
				boneTranslate_ = { 0.0f, 0.0f, 0.0f };
				boneRotate_ = { 0.0f, 0.0f, 0.0f };
				boneScale_ = { 1.0f, 1.0f, 1.0f };
			}
		}
	}
	ImGui::End();

	ImGui::Begin("Phong Check");

	ImGui::Checkbox("Orbit Camera", &orbitCam_);
	ImGui::DragFloat("Orbit Speed", &orbitSpeed_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Orbit Radius", &orbitRadius_, 0.1f, 1.0f, 50.0f);

	ImGui::SliderFloat("Shininess", &shininess_, 1.0f, 256.0f);

	// 1:Lambert 2:Half 3:SpecOnly
	ImGui::RadioButton("Lambert", &lightingMode_, 1); ImGui::SameLine();
	ImGui::RadioButton("HalfLambert", &lightingMode_, 2); ImGui::SameLine();
	ImGui::RadioButton("SpecOnly(Phong)", &lightingMode_, 3); ImGui::SameLine();
	ImGui::RadioButton("SpecOnly(Blinn)", &lightingMode_, 4);

	ImGui::Separator();
	ImGui::Text("Light");

	ImGui::DragFloat3("Dir", &lightDir_.x, 0.01f, -1.0f, 1.0f);
	ImGui::SliderFloat("Intensity", &lightIntensity_, 0.0f, 5.0f);
	ImGui::ColorEdit3("Color", &lightColor_.x); // RGBだけ


	ImGui::End();

	ImGui::Begin("Object SRT (Per-Object)");

	static const char* targetLabels[] = {
	"SkyDome",
	"VideoPlane",
	"Particle",
	"Ground",        // 追加
	"TitlePlayer",   // 追加
	"BG(Sprite)",
	"PressStart(Sprite)",
	};

	ImGui::Combo("Target", &editTarget_, targetLabels, IM_ARRAYSIZE(targetLabels));


	// ターゲットに応じて参照先を切り替え
	SRT* cur = nullptr;
	switch ((EditTarget)editTarget_) {
	case EditTarget::SkyDome:     cur = &srtSky_; break;
	case EditTarget::VideoPlane:  cur = &srtVideo_; break;
	case EditTarget::Particle:    cur = &srtParticle_; break;
	case EditTarget::Ground:      cur = &srtGround_; break;
	case EditTarget::TitlePlayer: cur = &srtPlayer_; break;
	case EditTarget::BG:          cur = &srtBG_; break;
	case EditTarget::PressStart:  cur = &srtPress_; break;
	default: break;
	}

	if (cur) {
		ImGui::DragFloat3("T", &cur->pos.x, 0.1f);
		ImGui::DragFloat3("R", &cur->rot.x, 0.01f);
		ImGui::DragFloat3("S", &cur->scale.x, 0.1f, 0.001f, 100.0f);
	}

	ImGui::Separator();
	ImGui::Text("Outline Settings");
	Object3d* curObj = nullptr;
	switch ((EditTarget)editTarget_) {
	case EditTarget::SkyDome:     curObj = skyDome_.get(); break;
	case EditTarget::VideoPlane:  curObj = videoPlane_.get(); break;
	case EditTarget::Ground:      curObj = ground_.get(); break;
	case EditTarget::TitlePlayer: curObj = titlePlayer ? titlePlayer->GetModelObject() : nullptr; break;
	default: break;
	}

	if (curObj) {
		bool enable = curObj->GetEnableOutline();
		if (ImGui::Checkbox("Enable Outline", &enable)) {
			curObj->SetEnableOutline(enable);
		}
		if (enable) {
			Vector4 color = curObj->GetOutlineColor();
			if (ImGui::ColorEdit3("Outline Color", &color.x)) {
				curObj->SetOutlineColor(color);
			}
			float thickness = curObj->GetOutlineThickness();
			if (ImGui::SliderFloat("Outline Thickness", &thickness, 0.0f, 1.0f)) {
				curObj->SetOutlineThickness(thickness);
			}
		}

		ImGui::Separator();
		ImGui::Text("Dissolve Settings");
		bool enableD = curObj->GetEnableDissolve();
		if (ImGui::Checkbox("Enable Dissolve", &enableD)) {
			curObj->SetEnableDissolve(enableD);
		}

		ImGui::Separator();
		ImGui::Text("Random Settings");
		bool enableR = curObj->GetEnableRandom();
		if (ImGui::Checkbox("Enable Random", &enableR)) {
			curObj->SetEnableRandom(enableR);
		}
		if (enableR) {
			curObj->SetRandomTime((float)ImGui::GetTime());
		}
	}

	ImGui::Separator();
	ImGui::Text("PointLight");

	ImGui::DragFloat3("Point Pos", &pointPos_.x, 0.1f);
	ImGui::SliderFloat("Point Intensity", &pointIntensity_, 0.0f, 10.0f);
	ImGui::DragFloat(
		"Point Radius",
		&pointRadius_,
		0.1f,        // 1ドラッグの変化量
		0,        // 最小
		100.0f,      // 最大（まずは100で十分）
		"%.2f"
	);
	ImGui::DragFloat(
		"Point Decay",
		&pointDecay_,
		0.05f,
		0.0f,
		8.0f,
		"%.2f"
	);

	ImGui::ColorEdit3("Point Color", &pointColor_.x);

	ImGui::Separator();
	ImGui::RadioButton("View: All", &lightViewMode_, 0); ImGui::SameLine();
	ImGui::RadioButton("View: DirOnly", &lightViewMode_, 1); ImGui::SameLine();
	ImGui::RadioButton("View: PointOnly", &lightViewMode_, 2);

	ImGui::Separator();
	ImGui::Text("SpotLight");

	ImGui::DragFloat3("Spot Pos", &spotPos_.x, 0.1f);
	ImGui::DragFloat3("Spot Dir", &spotDir_.x, 0.01f, -1.0f, 1.0f);

	ImGui::SliderFloat("Spot Intensity", &spotIntensity_, 0.0f, 20.0f);
	ImGui::DragFloat("Spot Distance", &spotDistance_, 0.1f, 0.0f, 200.0f, "%.2f");
	ImGui::DragFloat("Spot Decay", &spotDecay_, 0.05f, 0.0f, 8.0f, "%.2f");

	ImGui::ColorEdit3("Spot Color", &spotColor_.x);

	ImGui::SliderFloat("Spot Angle (deg)", &spotAngleDeg_, 1.0f, 89.0f);
	ImGui::SliderFloat("Falloff Start (deg)", &spotFalloffStartDeg_, 0.5f, 89.0f);

	ImGui::End();

	DrawImGui_ModelSwitchersOneWindow();

	ImGui::Begin("Video");
	ImGui::Checkbox("Enable Video", &enableVideo_);
	ImGui::End();

	ImGui::Begin("Video");

	if (video_) {
		int n = video_->GetAudioTrackCount();
		ImGui::Text("Audio tracks: %d", n);

		static int track = 0;
		if (n > 0) {
			if (track >= n) track = n - 1;

			if (ImGui::BeginCombo("Audio Track", ("Track" + std::to_string(track + 1)).c_str())) {
				for (int i = 0; i < n; ++i) {
					std::string label = "Track" + std::to_string(i + 1);
					bool selected = (i == track);
					if (ImGui::Selectable(label.c_str(), selected)) {
						track = i;
						video_->SetAudioTrack(i);
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
	}

	ImGui::End();

	ImGui::Begin("Primitive Check");

	ImGui::Combo(
		"Primitive Type",
		&primitiveTypeIndex_,
		primitiveLabels_.data(),
		static_cast<int>(primitiveLabels_.size())
	);

	ImGui::DragFloat3("Primitive Pos", &srtPrimitive_.pos.x, 0.1f);
	ImGui::DragFloat3("Primitive Rot", &srtPrimitive_.rot.x, 0.01f);
	ImGui::DragFloat3("Primitive Scale", &srtPrimitive_.scale.x, 0.1f, 0.01f, 100.0f);

	ImGui::End();


	// ===== 課題用ウィンドウ =====
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiCond_Once);

	if (ImGui::Begin("Sprite Position Control"))
	{
		// 現在のpressStartの座標を取得
		Vector2 pos = pressStart_->GetPosition();

		// スライダーで操作（小数1桁表示）
		ImGui::SliderFloat2(
			"PressStart Pos",
			&pos.x,
			0.0f,
			1280.0f,
			"%04.1f"
		);

		// 変更を反映
		pressStart_->SetPosition(pos);

		// 数値表示（整数4桁、小数1桁）
		ImGui::Text("X: %04.1f  Y: %04.1f", pos.x, pos.y);
	}

	ImGui::End();

#endif
}
