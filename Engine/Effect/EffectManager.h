#pragma once
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include "Vector3.h"

class Object3dCommon;
class DirectXCommon;
class Camera;
class Object3d;
class Model;

class EffectManager {
public:
    struct EffectParticleNode {
        std::string name;
        std::string particleFileName;
        float startTime = 0.0f;
        float endTime = 1.0f;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        int emitCount = 10;
        float presetDuration = 1.0f;
    };

	struct EffectReferenceNode {
		std::string name;
		std::string jsonPath;
		std::string templateName;
		float startTime = 0.0f;
		Vector3 position{ 0.0f, 0.0f, 0.0f };
	};

    struct EffectObjectKeyframe {
        float time = 0.0f;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool bloomPostEffect = false;
        bool outlineBloomPostEffect = false;
        Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        std::unordered_map<uint32_t, Vector3> vertexOffsets;
    };

    struct EffectObjectNode {
        int id = 0;
        std::string name;
        std::string modelPath;
        std::string texturePath;
        int geometryType = -1;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int blendMode = 0; // Object3dCommon::BlendMode (or int)
        bool billboard = false;
        bool bloomPostEffect = false;
        bool outlineBloomPostEffect = false;
        Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        std::vector<EffectObjectKeyframe> keyframes;
        std::unordered_map<uint32_t, Vector3> vertexOffsets;
    };

    struct EffectTemplate {
        std::string name;
        float duration = 1.0f;
        std::vector<EffectParticleNode> particleNodes;
        std::vector<EffectObjectNode> objectNodes;
		std::vector<EffectReferenceNode> effectNodes;
    };

    struct ActiveEffectObject {
        int id = 0;
        std::unique_ptr<Object3d> object;
        int geometryType = -1;
        std::string modelPath;
        std::string texturePath;
        int blendMode = 0;
        bool billboard = false;
        bool bloomPostEffect = false;
        bool outlineBloomPostEffect = false;
        Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
        std::vector<EffectObjectKeyframe> keyframes;
        std::unordered_map<uint32_t, Vector3> vertexOffsets;
        Model* baseModel = nullptr;
    };

    struct ActiveEffect {
        std::string templateName;
        Vector3 worldPosition;
        float currentTime = 0.0f;
        float duration = 1.0f;
        std::vector<bool> hasEmitted;
		std::vector<bool> hasPlayedEffects;
        std::vector<ActiveEffectObject> objects;
    };

    static EffectManager* GetInstance();

    void Initialize();
    void Finalize();

    void SetGraphicsResources(Object3dCommon* objCommon, DirectXCommon* dxCommon, Camera* camera);
    void SetCamera(Camera* camera);
    void Draw();
    void DrawPostEffectTargets();
    bool HasBloomPostEffectTargets() const;
    bool HasOutlineBloomPostEffectTargets() const;
    Vector4 GetPrimaryBloomColor() const;
    Vector4 GetPrimaryOutlineBloomColor() const;

    void LoadEffect(const std::string& effectName, const std::string& jsonPath);
    void Play(const std::string& effectName, const Vector3& worldPosition, float initialTime = 0.0f);
    void SetActiveEffectWorldPosition(const std::string& effectName, const Vector3& worldPosition);
    void Update(float dt);
    bool HasEffect(const std::string& effectName) const;
    void ClearActiveEffects();

private:
    EffectManager() = default;
    ~EffectManager() = default;
    EffectManager(const EffectManager&) = delete;
    EffectManager& operator=(const EffectManager&) = delete;

private:
    std::unordered_map<std::string, EffectTemplate> templates_;
    std::list<ActiveEffect> activeEffects_;

    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Camera* camera_ = nullptr;
};
