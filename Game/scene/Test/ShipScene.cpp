#include "ShipScene.h"

#include "Camera.h"
#include "DebugCamera.h"
#include "GameApp.h"
#include "GeometryGenerator.h"
#include "Input.h"
#include "Matrix4x4.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"

#include <algorithm>
#include <cmath>
#include <random>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
Model::ModelData MakePrimitiveModelData(
    const std::vector<Model::VertexData>& vertices, const char* rootName) {
    Model::ModelData data{};
    data.materials.push_back({ "" });
    Model::MeshData mesh{};
    mesh.materialIndex = 0;
    mesh.vertices = vertices;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(vertices.size());
    data.meshes.push_back(std::move(mesh));
    data.indices.resize(vertices.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
        data.indices[i] = i;
    }
    data.rootNode.name = rootName;
    data.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
    data.rootNode.meshIndices.push_back(0);
    return data;
}

Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2]
    };
}

Vector3 ComponentMultiply(const Vector3& a, const Vector3& b) {
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}
}

ShipScene::ShipScene() = default;
ShipScene::~ShipScene() = default;

void ShipScene::OnEnter(GameApp& app) {
    camera_ = std::make_unique<Camera>();
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInput(app.GetInput());
    debugCamera_->SetMoveSpeed(18.0f);
    debugCamera_->SetMouseLookEnabled(false);

    camera_->SetFarClip(2000.0f);
    app.ObjCom()->SetDefaultCamera(camera_.get());

    defaultShipTransform_.position = { 0.0f, 0.0f, 18.0f };
    defaultShipTransform_.rotation = {};
    defaultShipTransform_.scale = { 1.0f, 1.0f, 1.0f };
    shipTransform_ = defaultShipTransform_;

    CreatePrimitiveModels_();
    CreateShip_(app);
    SetCameraPreset_(0);
    ApplyTransforms_(0.0f);

    if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
}

void ShipScene::OnExit(GameApp& app) {
    if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
    parts_.clear();
    debugCamera_.reset();
    camera_.reset();
    boxModel_ = nullptr;
    cylinderModel_ = nullptr;
    sphereModel_ = nullptr;
    importedShipModel_ = nullptr;
}

void ShipScene::CreatePrimitiveModels_() {
    auto findOrCreate = [](const char* key, const std::vector<Model::VertexData>& vertices) {
        if (Model* model = ModelManager::GetInstance()->FindModel(key)) return model;
        return ModelManager::GetInstance()->CreatePrimitiveModel(
            key, MakePrimitiveModelData(vertices, key));
    };
    boxModel_ = findOrCreate("ShipScene_Box", GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f));
    // Deliberately low segment counts keep the boss ship faceted and readable.
    cylinderModel_ = findOrCreate("ShipScene_Cylinder", GeometryGenerator::GenerateCylinderTriList(12, 1.0f, 2.0f));
    sphereModel_ = findOrCreate("ShipScene_Sphere", GeometryGenerator::GenerateSphereTriList(12, 6, 1.0f));

    constexpr const char* kShipModelPath = "Boss_Ship/sip.gltf";
    ModelManager::GetInstance()->LoadModel(kShipModelPath);
    importedShipModel_ = ModelManager::GetInstance()->FindModel(kShipModelPath);
}

void ShipScene::CreateShip_(GameApp& app) {
    parts_.clear();
    // Blender exported the ship along local X. Rotate it into ShipScene's
    // forward Z axis and offset the stern-origin model around the preview root.
    AddPart_(app, "sip.gltf (113 meshes)", PrimitiveType::ImportedShip,
        { 0.0f, 0.0f, 12.0f }, { 0.0f, -1.5707963f, 0.0f },
        { 1.5f, 1.5f, 1.5f }, { 1.0f, 1.0f, 1.0f, 1.0f });
    InitializeExplosionFragments_();
}

void ShipScene::AddPart_(GameApp& app, const char* name, PrimitiveType primitive,
    const Vector3& position, const Vector3& rotation, const Vector3& scale,
    const Vector4& color) {
    ShipPart part{};
    part.name = name;
    part.primitive = primitive;
    part.transform = { position, rotation, scale };
    part.defaultTransform = part.transform;
    part.color = color;
    part.defaultColor = color;
    part.object = std::make_unique<Object3d>();
    part.object->Initialize(app.ObjCom(), app.Dx());
    part.object->SetCamera(camera_.get());
    part.object->SetModel(GetPrimitiveModel_(primitive));
    part.object->SetMaterialColor(color);
    part.object->SetEnableLighting(0);
    parts_.push_back(std::move(part));
}

Model* ShipScene::GetPrimitiveModel_(PrimitiveType primitive) const {
    if (primitive == PrimitiveType::ImportedShip) return importedShipModel_;
    if (primitive == PrimitiveType::Cylinder) return cylinderModel_;
    if (primitive == PrimitiveType::Sphere) return sphereModel_;
    return boxModel_;
}

void ShipScene::ApplyTransforms_(float dt) {
    const Matrix4x4 rootMatrix = Matrix4x4::MakeAffineMatrix(
        shipTransform_.scale, shipTransform_.rotation, shipTransform_.position);
    for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
        ShipPart& part = parts_[i];
        if (!part.object) continue;
        part.object->SetTranslate(TransformPoint(part.transform.position, rootMatrix));
        part.object->SetRotate(part.transform.rotation + shipTransform_.rotation);
        part.object->SetScale(ComponentMultiply(part.transform.scale, shipTransform_.scale));
        part.object->SetMaterialColor(part.color);
        const bool selected = showSelectionOutline_ && i == selectedPart_ && part.visible;
        part.object->SetEnableOutline(selected);
        part.object->SetOutlineColor(selectionOutlineColor_);
        part.object->SetOutlineThickness(selectionOutlineThickness_);
        part.object->SetIsVisible(part.visible);
        part.object->Update(dt);
    }
}

void ShipScene::ProcessPendingTextureChange_() {
    if (pendingTextureAction_ == TextureAction::None) return;
    auto apply = [&](ShipPart& part, bool clear) {
        if (!part.object) return;
        if (clear) {
            part.texturePath.clear();
            part.object->ClearTextureOverride();
        } else if (!pendingTexturePath_.empty()) {
            part.texturePath = pendingTexturePath_;
            part.object->SetTexture(part.texturePath);
        }
    };

    const bool clear = pendingTextureAction_ == TextureAction::ClearSelected ||
        pendingTextureAction_ == TextureAction::ClearAll;
    const bool all = pendingTextureAction_ == TextureAction::ApplyAll ||
        pendingTextureAction_ == TextureAction::ClearAll;
    if (all) {
        for (ShipPart& part : parts_) apply(part, clear);
    } else if (pendingTexturePart_ >= 0 && pendingTexturePart_ < static_cast<int>(parts_.size())) {
        apply(parts_[pendingTexturePart_], clear);
    }
    pendingTextureAction_ = TextureAction::None;
    pendingTexturePart_ = -1;
    pendingTexturePath_.clear();
}

void ShipScene::InitializeExplosionFragments_() {
    explosionFragments_.clear();
    if (parts_.empty() || !parts_[0].object) return;
    explosionFragments_.resize(parts_[0].object->GetMeshInstanceCount());
    parts_[0].object->ResetMeshInstanceExplosionOffsets();
}

void ShipScene::TriggerExplosion_() {
    if (parts_.empty() || !parts_[0].object || explosionFragments_.empty()) return;
    static std::mt19937 random{ std::random_device{}() };
    std::uniform_real_distribution<float> horizontal(-1.0f, 1.0f);
    std::uniform_real_distribution<float> vertical(0.15f, 1.0f);
    std::uniform_real_distribution<float> speed(0.65f, 1.25f);
    std::uniform_real_distribution<float> spin(-1.0f, 1.0f);

    for (ExplosionFragment& fragment : explosionFragments_) {
        fragment = {};
        Vector3 direction{ horizontal(random), vertical(random), horizontal(random) };
        const float length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length > 0.0001f) direction = direction * (1.0f / length);
        const float fragmentSpeed = explosionPower_ * speed(random);
        fragment.velocity = direction * fragmentSpeed;
        fragment.velocity.y += explosionUpwardPower_;
        fragment.angularVelocity = {
            spin(random) * explosionRotationPower_,
            spin(random) * explosionRotationPower_,
            spin(random) * explosionRotationPower_
        };
    }
    explosionActive_ = true;
}

void ShipScene::ResetExplosion_() {
    explosionActive_ = false;
    for (ExplosionFragment& fragment : explosionFragments_) fragment = {};
    if (!parts_.empty() && parts_[0].object) {
        parts_[0].object->ResetMeshInstanceExplosionOffsets();
    }
}

void ShipScene::UpdateExplosion_(float dt) {
    if (!explosionActive_ || parts_.empty() || !parts_[0].object) return;
    const float damping = std::max(0.0f, 1.0f - explosionDrag_ * dt);
    for (size_t i = 0; i < explosionFragments_.size(); ++i) {
        ExplosionFragment& fragment = explosionFragments_[i];
        fragment.velocity.y -= explosionGravity_ * dt;
        fragment.velocity = fragment.velocity * damping;
        fragment.position += fragment.velocity * dt;
        fragment.rotation += fragment.angularVelocity * dt;
        parts_[0].object->SetMeshInstanceExplosionOffset(i, fragment.position, fragment.rotation);
    }
}

void ShipScene::ResetAll_() {
    ResetExplosion_();
    shipTransform_ = defaultShipTransform_;
    for (ShipPart& part : parts_) {
        part.transform = part.defaultTransform;
        part.color = part.defaultColor;
        part.visible = true;
    }
}

void ShipScene::SetCameraPreset_(int preset) {
    if (!debugCamera_) return;
    const Vector3 focus = shipTransform_.position + Vector3{ 0, 5.0f, 0 };
    const float d = std::max(cameraDistance_, 10.0f);
    switch (preset) {
    case 1: // side
        debugCamera_->SetPosition(focus + Vector3{ d, 6.0f, 0 });
        debugCamera_->SetRotation({ 0.08f, -1.5707963f, 0 });
        break;
    case 2: // top
        debugCamera_->SetPosition(focus + Vector3{ 0, d, 0 });
        debugCamera_->SetRotation({ 1.5707963f, 0, 0 });
        break;
    case 3: // rear
        debugCamera_->SetPosition(focus + Vector3{ 0, 8.0f, d });
        debugCamera_->SetRotation({ 0.12f, 3.1415926f, 0 });
        break;
    default: // front three-quarter
        debugCamera_->SetPosition(focus + Vector3{ 30.0f, 15.0f, -d });
        debugCamera_->SetRotation({ 0.16f, -0.42f, 0 });
        break;
    }
}

void ShipScene::Update(GameApp& app, float dt) {
    Input* input = app.GetInput();
    if (input && input->IsCameraControlEnabled()) input->SetCameraControlEnabled(false);
    if (input && input->IsKeyTrigger(DIK_F2)) {
        RequestChangeScene_("Game");
        return;
    }
    if (input && input->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }
    ProcessPendingTextureChange_();
    if (pendingExplosion_) {
        TriggerExplosion_();
        pendingExplosion_ = false;
    }
    if (pendingExplosionReset_) {
        ResetExplosion_();
        pendingExplosionReset_ = false;
    }
    UpdateExplosion_(dt);
    if (debugCamera_ && camera_) {
        debugCamera_->Update(dt);
        camera_->SetTranslate(debugCamera_->GetPosition());
        camera_->SetRotate(debugCamera_->GetRotation());
        camera_->Update();
    }
    ApplyTransforms_(dt);
}

void ShipScene::Draw(GameApp& /*app*/) {
    for (const ShipPart& part : parts_) {
        if (part.visible && part.object) part.object->Draw();
    }
}

void ShipScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSizeConstraints(ImVec2(430, 420), ImVec2(900, 1200));
    ImGui::Begin("Ship Scene");
    ImGui::TextUnformatted("Primitive boss ship preview");
    ImGui::TextDisabled("F2: return to Game / WASD: move camera");

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Preset Distance", &cameraDistance_, 1.0f, 10.0f, 300.0f);
        if (ImGui::Button("Perspective")) SetCameraPreset_(0);
        ImGui::SameLine();
        if (ImGui::Button("Side")) SetCameraPreset_(1);
        ImGui::SameLine();
        if (ImGui::Button("Top")) SetCameraPreset_(2);
        ImGui::SameLine();
        if (ImGui::Button("Rear")) SetCameraPreset_(3);
    }

    if (ImGui::CollapsingHeader("Ship Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position##Ship", &shipTransform_.position.x, 0.1f);
        ImGui::DragFloat3("Rotation##Ship", &shipTransform_.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale##Ship", &shipTransform_.scale.x, 0.01f, 0.05f, 20.0f);
        if (ImGui::Button("Reset Entire Ship")) ResetAll_();
    }

    if (ImGui::CollapsingHeader("Explosion Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Power", &explosionPower_, 0.2f, 0.0f, 100.0f);
        ImGui::DragFloat("Upward Power", &explosionUpwardPower_, 0.2f, 0.0f, 100.0f);
        ImGui::DragFloat("Rotation Power", &explosionRotationPower_, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Gravity", &explosionGravity_, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Drag", &explosionDrag_, 0.01f, 0.0f, 5.0f);
        ImGui::Text("Fragments: %zu", explosionFragments_.size());
        if (ImGui::Button("Explode")) pendingExplosion_ = true;
        ImGui::SameLine();
        if (ImGui::Button("Reset Explosion")) pendingExplosionReset_ = true;
    }

    if (ImGui::CollapsingHeader("Parts", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!parts_.empty()) {
            selectedPart_ = std::clamp(selectedPart_, 0, static_cast<int>(parts_.size()) - 1);
            if (ImGui::BeginCombo("Selected Part", parts_[selectedPart_].name.c_str())) {
                for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
                    const bool selected = i == selectedPart_;
                    if (ImGui::Selectable(parts_[i].name.c_str(), selected)) selectedPart_ = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ShipPart& part = parts_[selectedPart_];
            if (ImGui::CollapsingHeader("Selection Display", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Selection Outline", &showSelectionOutline_);
                ImGui::ColorEdit4("Outline Color", &selectionOutlineColor_.x);
                ImGui::DragFloat("Outline Thickness", &selectionOutlineThickness_, 0.002f, 0.001f, 0.2f);
            }
            ImGui::Checkbox("Visible", &part.visible);
            ImGui::DragFloat3("Local Position", &part.transform.position.x, 0.05f);
            ImGui::DragFloat3("Local Rotation", &part.transform.rotation.x, 0.01f);
            ImGui::DragFloat3("Local Scale", &part.transform.scale.x, 0.02f, 0.02f, 100.0f);
            ImGui::ColorEdit4("Part Color", &part.color.x);
            if (ImGui::Button("Reset Selected Part")) {
                part.transform = part.defaultTransform;
                part.color = part.defaultColor;
            }

            ImGui::SeparatorText("External Texture");
            ImGui::InputText("Texture Path", texturePathInput_, sizeof(texturePathInput_));
            ImGui::TextWrapped("Current: %s", part.texturePath.empty() ? "(primitive color only)" : part.texturePath.c_str());
            if (ImGui::Button("Apply to Selected")) {
                pendingTextureAction_ = TextureAction::ApplySelected;
                pendingTexturePart_ = selectedPart_;
                pendingTexturePath_ = texturePathInput_;
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply to All")) {
                pendingTextureAction_ = TextureAction::ApplyAll;
                pendingTexturePath_ = texturePathInput_;
            }
            if (ImGui::Button("Clear Selected Texture")) {
                pendingTextureAction_ = TextureAction::ClearSelected;
                pendingTexturePart_ = selectedPart_;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All Textures")) {
                pendingTextureAction_ = TextureAction::ClearAll;
            }
            ImGui::SeparatorText("Explosion-ready hierarchy");
            ImGui::TextUnformatted("Ship");
            for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(("  " + parts_[i].name).c_str(), i == selectedPart_)) selectedPart_ = i;
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
#endif
}
