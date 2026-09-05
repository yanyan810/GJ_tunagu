#pragma once

#include "IScene.h"
#include "Vector3.h"

#include <memory>
#include <string>
#include <vector>

class Camera;
class DebugCamera;
class GameApp;
class Model;
class Object3d;

// A primitive-only preview scene for authoring the boss ship silhouette.
// Every visible piece owns an Object3d and a local transform so it can later
// become an independently simulated explosion fragment.
class ShipScene final : public IScene {
public:
    ShipScene();
    ~ShipScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    enum class PrimitiveType { Box, Cylinder, Sphere, ImportedShip };

    struct PartTransform {
        Vector3 position{};
        Vector3 rotation{};
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct ShipPart {
        std::string name;
        PrimitiveType primitive = PrimitiveType::Box;
        PartTransform transform{};
        PartTransform defaultTransform{};
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 defaultColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string texturePath;
        std::unique_ptr<Object3d> object;
        bool visible = true;
    };

    void CreatePrimitiveModels_();
    void CreateShip_(GameApp& app);
    void AddPart_(GameApp& app, const char* name, PrimitiveType primitive,
        const Vector3& position, const Vector3& rotation, const Vector3& scale,
        const Vector4& color);
    void ApplyTransforms_(float dt);
    void ProcessPendingTextureChange_();
    void InitializeExplosionFragments_();
    void TriggerExplosion_();
    void ResetExplosion_();
    void UpdateExplosion_(float dt);
    void ResetAll_();
    void SetCameraPreset_(int preset);
    Model* GetPrimitiveModel_(PrimitiveType primitive) const;

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<DebugCamera> debugCamera_;
    std::vector<ShipPart> parts_;
    Model* boxModel_ = nullptr;
    Model* cylinderModel_ = nullptr;
    Model* sphereModel_ = nullptr;
    Model* importedShipModel_ = nullptr;

    PartTransform shipTransform_{};
    PartTransform defaultShipTransform_{};
    int selectedPart_ = 0;
    float cameraDistance_ = 62.0f;
    Vector4 selectionOutlineColor_{ 1.0f, 0.72f, 0.05f, 1.0f };
    float selectionOutlineThickness_ = 0.035f;
    bool showSelectionOutline_ = true;
    char texturePathInput_[512]{};
    enum class TextureAction { None, ApplySelected, ApplyAll, ClearSelected, ClearAll };
    TextureAction pendingTextureAction_ = TextureAction::None;
    int pendingTexturePart_ = -1;
    std::string pendingTexturePath_;

    struct ExplosionFragment {
        Vector3 position{};
        Vector3 rotation{};
        Vector3 velocity{};
        Vector3 angularVelocity{};
    };
    std::vector<ExplosionFragment> explosionFragments_;
    float explosionPower_ = 12.0f;
    float explosionUpwardPower_ = 5.0f;
    float explosionRotationPower_ = 4.0f;
    float explosionGravity_ = 5.0f;
    float explosionDrag_ = 0.15f;
    bool explosionActive_ = false;
    bool pendingExplosion_ = false;
    bool pendingExplosionReset_ = false;
};
