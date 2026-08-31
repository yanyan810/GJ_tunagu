#include "CGTestScene.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "Input.h"
#include "Model.h"
#include "Object3d.h"
#include "Object3dCommon.h"

#include <algorithm>
#include <dinput.h>
#include <functional>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr const char* kEvaluationModelPath = "CGTest/BrainStem.glb";
}

void CGTestScene::OnEnter(GameApp& app) {
    input_ = app.GetInput();

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 1.25f, -4.0f });
    camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
    camera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());

    model_ = std::make_unique<Object3d>();
    model_->Initialize(app.ObjCom(), app.Dx());
    model_->SetCamera(camera_.get());
    model_->SetModel(kEvaluationModelPath);
    model_->SetTranslate({ 0.0f, 0.0f, 0.0f });
    model_->SetScale({ modelScale_, modelScale_, modelScale_ });
    model_->SetEnableLighting(1);
    model_->SetDebugDrawBones(drawBones_);
    model_->SetDebugBoneMarkerScale(boneMarkerScale_);
    model_->SetDebugBoneViewOffset(boneViewOffset_);

    animationNames_ = model_->GetAnimationNames();
    if (!animationNames_.empty()) {
        SelectAnimation_(0);
        status_ = "Evaluation model loaded.";
    } else {
        status_ = "Model loaded, but no animation clips were found.";
        animationPlaying_ = false;
    }
}

void CGTestScene::OnExit(GameApp&) {
    model_.reset();
    camera_.reset();
    animationNames_.clear();
    input_ = nullptr;
}

void CGTestScene::SelectAnimation_(int index) {
    if (!model_ || animationNames_.empty()) {
        return;
    }

    selectedAnimation_ = std::clamp(index, 0, static_cast<int>(animationNames_.size()) - 1);
    model_->CrossFadeTo(animationNames_[selectedAnimation_], 0.20f, true);
    animationPlaying_ = true;
}

void CGTestScene::Update(GameApp&, float dt) {
    if (input_) {
        if (input_->IsKeyTrigger(DIK_ESCAPE)) {
            RequestChangeScene_("Title");
            return;
        }
        if (input_->IsKeyTrigger(DIK_1)) SelectAnimation_(0);
        if (input_->IsKeyTrigger(DIK_2)) SelectAnimation_(1);
        if (input_->IsKeyTrigger(DIK_3)) SelectAnimation_(2);

        if (input_->IsKeyTrigger(DIK_SPACE) && model_ && !animationNames_.empty()) {
            animationPlaying_ = !animationPlaying_;
            if (animationPlaying_) {
                model_->PlayAnimation(animationNames_[selectedAnimation_], true);
            } else {
                model_->StopAnimation();
            }
        }
        if (input_->IsKeyTrigger(DIK_B) && model_) {
            drawBones_ = !drawBones_;
            model_->SetDebugDrawBones(drawBones_);
        }
        if (input_->IsKeyTrigger(DIK_R)) {
            autoRotate_ = !autoRotate_;
        }
    }

    if (autoRotate_) {
        modelYaw_ += dt * 0.5f;
    }
    if (model_) {
        model_->SetRotate({ 0.0f, modelYaw_, 0.0f });
        model_->SetScale({ modelScale_, modelScale_, modelScale_ });
        model_->Update(dt);
    }
    if (camera_) {
        camera_->Update();
    }
}

void CGTestScene::DrawRender(GameApp&) {
    if (model_) {
        model_->Draw();
    }
}

void CGTestScene::Draw(GameApp&) {
}

void CGTestScene::DrawImGui(GameApp&) {
#ifdef USE_IMGUI
    ImGui::Begin("CG Evaluation Test");
    ImGui::TextUnformatted("BrainStem - MultiMesh / MultiMaterial / Animation");
    ImGui::Separator();

    if (model_ && model_->GetModel()) {
        const Model* loadedModel = model_->GetModel();
        ImGui::Text("Meshes: %u", loadedModel->GetMeshCount());
        ImGui::Text("Materials: %d", static_cast<int>(loadedModel->GetMaterials().size()));
        ImGui::Text("Animations: %d", static_cast<int>(animationNames_.size()));
        ImGui::Text("Skinning: %s", loadedModel->HasSkinning() ? "GPU Compute Skinning" : "None");
        const Model::Skeleton* skeleton = model_->GetSkeleton();
        ImGui::Text("Bones: %d", skeleton ? static_cast<int>(skeleton->joints.size()) : 0);
    }
    ImGui::Text("Playing: %s",
        model_ && !model_->GetPlayingAnimName().empty()
            ? model_->GetPlayingAnimName().c_str()
            : "(stopped)");
    ImGui::Text("Status: %s", status_.c_str());

    ImGui::SeparatorText("Animation Clips");
    if (!animationNames_.empty()) {
        const char* preview = animationNames_[selectedAnimation_].c_str();
        if (ImGui::BeginCombo("Clip", preview)) {
            for (int i = 0; i < static_cast<int>(animationNames_.size()); ++i) {
                const bool selected = i == selectedAnimation_;
                if (ImGui::Selectable(animationNames_[i].c_str(), selected)) {
                    SelectAnimation_(i);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::Button(animationPlaying_ ? "Pause" : "Play") && model_ && !animationNames_.empty()) {
        animationPlaying_ = !animationPlaying_;
        if (animationPlaying_) {
            model_->PlayAnimation(animationNames_[selectedAnimation_], true);
        } else {
            model_->StopAnimation();
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Draw Bones", &drawBones_) && model_) {
        model_->SetDebugDrawBones(drawBones_);
    }
    ImGui::Checkbox("Auto Rotate", &autoRotate_);
    ImGui::SliderFloat("Model Scale", &modelScale_, 0.05f, 5.0f, "%.2f");

    ImGui::SeparatorText("Bone Debug Inspector");
    if (model_ && model_->GetSkeleton()) {
        const Model::Skeleton& skeleton = *model_->GetSkeleton();

        if (ImGui::SliderFloat("Marker Size", &boneMarkerScale_, 0.005f, 0.15f, "%.3f")) {
            model_->SetDebugBoneMarkerScale(boneMarkerScale_);
        }
        if (ImGui::DragFloat3("View Offset", &boneViewOffset_.x, 0.005f, -2.0f, 2.0f, "%.3f")) {
            model_->SetDebugBoneViewOffset(boneViewOffset_);
        }
        ImGui::InputTextWithHint("Bone Search", "name...", boneSearch_, sizeof(boneSearch_));

        const std::string search = boneSearch_;
        auto containsSearch = [&](const Model::Joint& joint) {
            if (search.empty()) return true;
            std::string name = joint.name;
            std::string filter = search;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            return name.find(filter) != std::string::npos;
        };

        ImGui::BeginChild("BoneHierarchy", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
        std::function<bool(int32_t)> subtreeMatches = [&](int32_t index) {
            if (index < 0 || index >= static_cast<int32_t>(skeleton.joints.size())) return false;
            const Model::Joint& joint = skeleton.joints[index];
            if (containsSearch(joint)) return true;
            for (int32_t child : joint.children) {
                if (subtreeMatches(child)) return true;
            }
            return false;
        };
        std::function<void(int32_t)> drawJoint = [&](int32_t index) {
            if (index < 0 || index >= static_cast<int32_t>(skeleton.joints.size()) ||
                !subtreeMatches(index)) {
                return;
            }
            const Model::Joint& joint = skeleton.joints[index];
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (joint.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (selectedBone_ == index) flags |= ImGuiTreeNodeFlags_Selected;
            if (!search.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;

            const bool open = ImGui::TreeNodeEx(
                reinterpret_cast<void*>(static_cast<intptr_t>(index + 1)),
                flags, "%s", joint.name.c_str());
            if (ImGui::IsItemClicked()) {
                selectedBone_ = index;
                model_->SetDebugSelectedBone(selectedBone_);
            }
            if (open) {
                for (int32_t child : joint.children) drawJoint(child);
                ImGui::TreePop();
            }
        };
        drawJoint(skeleton.root);
        ImGui::EndChild();

        if (selectedBone_ >= 0 &&
            selectedBone_ < static_cast<int>(skeleton.joints.size())) {
            const Model::Joint& joint = skeleton.joints[selectedBone_];
            ImGui::Text("Selected: %s", joint.name.c_str());
            if (joint.parent.has_value() &&
                *joint.parent >= 0 &&
                *joint.parent < static_cast<int32_t>(skeleton.joints.size())) {
                ImGui::Text("Parent: %s", skeleton.joints[*joint.parent].name.c_str());
            } else {
                ImGui::TextUnformatted("Parent: (root)");
            }
            ImGui::Text("Local Position: %.3f, %.3f, %.3f",
                joint.transform.translate.x,
                joint.transform.translate.y,
                joint.transform.translate.z);
            ImGui::Text("Local Scale: %.3f, %.3f, %.3f",
                joint.transform.scale.x,
                joint.transform.scale.y,
                joint.transform.scale.z);
            ImGui::TextDisabled("Yellow marker = selected bone, red = other bones");
        } else {
            ImGui::TextDisabled("Select a bone to inspect its local transform.");
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("1/2/3: Select animation  Space: Play/Pause");
    ImGui::TextUnformatted("B: Bones  R: Auto Rotate  Esc: Back to Title");
    if (ImGui::Button("Back to Title")) {
        RequestChangeScene_("Title");
    }
    ImGui::End();
#endif
}
