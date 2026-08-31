#include "DebugScene.h"
#include "GameApp.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include <format>
#include <cassert>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void DebugScene::OnEnter(GameApp& app)
{
    input_ = app.GetInput();
    assert(input_);

    // ===== カメラ初期設定 =====
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 10.0f, -30.0f });
    camera_->SetRotate({ camPitch_, camYaw_, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    // ===== LevelLoader: JSONを読み込む =====
    // resources/levels/stage1.json を配置してからコメントアウトを外す
    
    levelData_ = LevelLoader::Load("stage1");

    for (auto& objData : levelData_.objects) {
        if (objData.fileName.empty()) { continue; }

        // モデルをロード
        ModelManager::GetInstance()->LoadModel(objData.fileName);

        // Object3dを生成・初期化
        auto obj = std::make_unique<Object3d>();
        obj->Initialize(app.ObjCom(), app.Dx());
        obj->SetCamera(camera_.get());
        obj->SetModel(objData.fileName);

        // Blenderのトランスフォームを適用
        obj->SetTranslate(objData.translation);
        obj->SetRotate(objData.rotation);
        obj->SetScale(objData.scaling);

        levelObjects_.push_back(std::move(obj));
    }
    
}

void DebugScene::OnExit(GameApp& /*app*/)
{
    levelObjects_.clear();
    camera_.reset();
}

void DebugScene::Update(GameApp& app, float dt)
{
    (void)app;
    (void)dt;

    camera_->Update();

    // LevelLoader で読み込んだオブジェクトの更新
    for (auto& obj : levelObjects_) {
        obj->Update(dt);
    }

    // Escでシーン終了（タイトルへ戻る）
    if (input_ && input_->IsKeyTrigger(DIK_ESCAPE)) {
        RequestChangeScene_("Title");
    }
}

void DebugScene::DrawRender(GameApp& app)
{
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // LevelLoader で読み込んだオブジェクトの描画
    for (auto& obj : levelObjects_) {
        obj->Draw();
    }
}

void DebugScene::Draw(GameApp& /*app*/)
{
}

void DebugScene::DrawImGui(GameApp& /*app*/)
{
#ifdef USE_IMGUI
    ImGui::Begin("DebugScene - LevelLoader");

    ImGui::Text("Objects loaded: %d", (int)levelData_.objects.size());
    ImGui::Separator();

    // 読み込んだオブジェクト一覧を表示
    for (int i = 0; i < (int)levelData_.objects.size(); ++i) {
        const auto& obj = levelData_.objects[i];
        if (ImGui::TreeNode(std::format("[{}] {}", i, obj.name).c_str())) {
            ImGui::Text("File: %s", obj.fileName.c_str());
            ImGui::Text("Pos:  %.2f, %.2f, %.2f",
                obj.translation.x, obj.translation.y, obj.translation.z);
            ImGui::Text("Rot:  %.2f, %.2f, %.2f",
                obj.rotation.x, obj.rotation.y, obj.rotation.z);
            ImGui::Text("Scale:%.2f, %.2f, %.2f",
                obj.scaling.x, obj.scaling.y, obj.scaling.z);
            ImGui::Text("Collider: %s", obj.hasCollider ? obj.colliderType.c_str() : "none");
            ImGui::TreePop();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Back to Title")) {
        RequestChangeScene_("Title");
    }

    ImGui::End();
#endif
}
