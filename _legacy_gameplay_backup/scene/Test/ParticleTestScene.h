#pragma once

#include "IScene.h"
#include "ParticleTestEditorTypes.h"
#include "Object3dCommon.h"
#include "Vector3.h"

#include <deque>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class Camera;
class Object3d;
class Particle;
class GameApp;

class ParticleTestScene : public IScene {
public:

    //初期化部分
    void OnEnter(GameApp& app) override;

    //終了したときの片づけ
    void OnExit(GameApp& app) override;

    //実行部分
    void Update(GameApp& app, float dt) override;


    //=========
    //描画部分
    //=========

    //
    void DrawRender(GameApp& app) override;

    //3Dモデル描画用
    void Draw3D(GameApp& app) override;

    //スプライトなどの描画
    void Draw2D(GameApp& app) override;

    //全体描画
    void Draw(GameApp& app) override;

    //ImGui描画
    void DrawImGui(GameApp& app) override;


    void DrawPreview(GameApp& app) override;

	//対象のポストエフェクトターゲットを描画する
    void DrawPostEffectTargets(GameApp& app) override;
    bool HasObjectBloomTargets() const override;
    bool HasObjectOutlineBloomTargets() const override;

private:
    using ParticleNode = ParticleTestEditor::ParticleNode;
    using EffectKeyframe = ParticleTestEditor::EffectKeyframe;
    using CameraKeyframe = ParticleTestEditor::CameraKeyframe;
    using PlayerAttackHitboxKeyframe = ParticleTestEditor::PlayerAttackHitboxKeyframe;
    using PlayerSpecialHitboxKeyframe = ParticleTestEditor::PlayerSpecialHitboxKeyframe;
    using PlayerSpecialMotionKeyframe = ParticleTestEditor::PlayerSpecialMotionKeyframe;
    using PlayerSpecialAnimationKeyframe = ParticleTestEditor::PlayerSpecialAnimationKeyframe;
    using PlayerSpecialEventKeyframe = ParticleTestEditor::PlayerSpecialEventKeyframe;
    using PlayerSpecialTimeline = ParticleTestEditor::PlayerSpecialTimeline;
    using PlayerSpecialPositionKeyframe = ParticleTestEditor::PlayerSpecialPositionKeyframe;
    using PlayerSpecialRotationKeyframe = ParticleTestEditor::PlayerSpecialRotationKeyframe;
    using PlayerSpecialOpacityKeyframe = ParticleTestEditor::PlayerSpecialOpacityKeyframe;
	using PlayerSpecialVisualZKeyframe = ParticleTestEditor::PlayerSpecialVisualZKeyframe;
	using PlayerSpecialEffectKeyframe = ParticleTestEditor::PlayerSpecialEffectKeyframe;
	using PlayerSpecialParticleKeyframe = ParticleTestEditor::PlayerSpecialParticleKeyframe;
	using PlayerSpecialAttackType = ParticleTestEditor::PlayerSpecialAttackType;

    using EditorBonePose = ParticleTestEditor::EditorBonePose;
    using EditorObject = ParticleTestEditor::EditorObject;
    using EditorObjectSnapshot = ParticleTestEditor::EditorObjectSnapshot;
    using EditorSnapshot = ParticleTestEditor::EditorSnapshot;
    using GizmoMode = ParticleTestEditor::GizmoMode;
    using EditorMode = ParticleTestEditor::EditorMode;

    void EnsureHitEffectGroup_();
    void ReloadParticleJson_();
    void SpawnHitEffectPreview_();
    void AddEditorObject_(GameApp& app, const std::string& modelPath);
    void AddGeometryObject_(GameApp& app, int geometryType);
    void PasteEditorObject_(GameApp& app);
    void DuplicateSelectedObject_(GameApp& app);
    void RequestDeleteSelectedObject_();
    void DeleteSelectedObject_();
    void ApplyEditorObjectTransform_(EditorObject& item);
    Camera* GetSceneCamera_() const;
    Camera* GetPreviewCamera_() const;
    void ApplyCameraToEditorObjects_();
    void ApplyAnimationCamera_();
    void DrawSceneContent_(GameApp& app);
    void SyncEditorObjectBones_(EditorObject& item);
    void ApplyEditorObjectBonePose_(EditorObject& item);
    EditorObjectSnapshot CaptureSelectedObject_() const;
    void CopySelectedObject_();
    void AddKeyframeToSelected_();
    void AddKeyframeToObject_(EditorObject& item);
    void CopySelectedModelKeyframe_();
    void PasteCopiedModelKeyframe_();
    void DeleteNearestKeyframeFromSelected_();
    void SortKeyframes_(EditorObject& item);
    void SortCameraKeyframes_();
    void EvaluateTimeline_(bool emitParticles = true);
    float GetParticleNodeDuration_(const ParticleNode& node) const;
    void EmitParticleNode_(const ParticleNode& node, float initialAge);
    void RequestTimelineRebuild_(float targetTime);
    void RebuildParticleTimeline_(float targetTime);
    void AddCameraKeyframe_();
    void DeleteNearestCameraKeyframe_();
    void EnsurePlayerAttackEditor_(GameApp& app);
    void AddPlayerAttackHitboxKeyframe_();
    void DeleteNearestPlayerAttackHitboxKeyframe_();
    void SortPlayerAttackHitboxKeyframes_();
    void EvaluatePlayerAttackHitbox_();
    void EnsurePlayerSpecialTimelineDefaults_();
    PlayerSpecialTimeline& CurrentPlayerSpecialTimeline_();
    const PlayerSpecialTimeline& CurrentPlayerSpecialTimeline_() const;
    void SortCurrentPlayerSpecialTimeline_();
    void EvaluatePlayerSpecialTimeline_();
    void SyncPlayerSpecialPreviewNodes_();
    EditorSnapshot CaptureEditorSnapshot_() const;
    void RestoreEditorSnapshot_(GameApp& app, const EditorSnapshot& snapshot);
    void PushUndoSnapshot_(const EditorSnapshot& snapshot);
    void PushUndoSnapshot_();
    void Undo_(GameApp& app);
    void Redo_(GameApp& app);
    void SaveEffectJson_(const std::string& path) const;
    void LoadEffectJson_(GameApp& app, const std::string& path);
    std::string MakeEffectsJsonPath_(const std::string& path) const;
    bool OpenModelFileDialog_();
    bool OpenModelFileDialog_(std::string& outModelPath);
    bool OpenTextureFileDialog_(std::string& outTexturePath);
    bool OpenEffectJsonFileDialog_(bool saveDialog, std::string& outJsonPath);
    bool OpenPlayerSpecialJsonFileDialog_(std::string& outJsonPath);
    void HandleEffectEditorShortcuts_(GameApp& app);
    void EnsureUniqueModelForObject_(EditorObject& item);
    void SyncParticleModelsWithEditorObjects_();
    void UpdateCameraControls_(GameApp& app, float dt);
    void ApplyVertexOffsets_(EditorObject& item);
    std::unordered_map<uint32_t, Vector3> LerpVertexOffsets_(const EditorObject& item, const EffectKeyframe& a, const EffectKeyframe& b, float t) const;
    void UpdateVertexPositionGroup_(EditorObject& item, int baseVertexIndex, const Vector3& localDelta);
    void MoveSelectedVertices_(EditorObject& item, const Vector3& localDelta);
 
	void EvaluatePlayerSpecialPosition_();//プレイヤー必殺技の位置オフセットを補完する
	Vector3 ResolvePlayerSpecialPositionOffset_(const PlayerSpecialPositionKeyframe& key) const;
	void EvaluatePlayerSpecialOpacity_();
	void EvaluatePlayerSpecialVisualZ_();
	void EvaluatePlayerSpecialRotation_();
	void EvaluatePlayerSpecialAnimation_();
	void TriggerPlayerSpecialEffects_();
    
    void ApplyPlayerSpecialPreviewPosition_();//補間位置をモデルへ適応する

    void DrawEffectInspectorImGui_(GameApp& app);
    void DrawPlayerAttackInspectorImGui_(GameApp& app);
    void DrawAnimationCameraControls_();
    void DrawGizmoControls_(EditorObject& item);
    void DrawBoneControls_(EditorObject& item);
    void DrawViewportBones_();
    void DrawViewportGizmo_(GameApp& app);
    void DrawEditorCameraControls_();
    void DrawEffectEditorImGui_(GameApp& app);
    void DrawParticleModeImGui_();
    void DrawPlayerAttackEditorImGui_(GameApp& app);
	void DrawPlayerSpecialPathPreview_();
	void FocusPlayerSpecialPathCamera_();

    private:

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Camera> animationCamera_;
    std::unique_ptr<Camera> gamePreviewCamera_;
    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Object3d> playerAttackHitboxCube_;
    std::unique_ptr<Object3d> bossDummy_;
    std::unique_ptr<Object3d> bossDummyHitboxCube_;
    std::unique_ptr<Particle> editorParticle_;
    std::vector<EditorObject> editorObjects_;
    std::deque<EditorSnapshot> undoStack_;
    std::deque<EditorSnapshot> redoStack_;
    EditorObjectSnapshot copiedObject_{};   
    bool hasCopiedObject_ = false;
    EffectKeyframe copiedModelKeyframe_{};
    bool hasCopiedModelKeyframe_ = false;

    char hitEffectGroupName_[64] = "HitEffect";
    char editorModelPath_[128] = "cube/cube.obj";
    char effectJsonPath_[128] = "resources/effects/effect_editor.json";
    int hitEffectSpawnCount_ = 24;
    int selectedGeometryType_ = 1;
    int selectedEditorObject_ = -1;
    int nextEditorObjectId_ = 1;
    Vector3 hitEffectSpawnPosition_{ 0.0f, 1.0f, 0.0f };
    float timelineTime_ = 0.0f;
    float timelineDuration_ = 1.0f;
    float timelineViewStart_ = 0.0f;
    float timelineViewDuration_ = 1.0f;
    bool timelinePlaying_ = false;
    bool timelineLoop_ = true;
    bool useAnimationCameraPreview_ = false;
    bool animationCameraPreviewSwapped_ = false;
    std::vector<CameraKeyframe> cameraKeyframes_;
    std::vector<PlayerAttackHitboxKeyframe> playerAttackHitboxKeyframes_;
    PlayerAttackHitboxKeyframe currentPlayerAttackHitbox_{};
    PlayerAttackHitboxKeyframe previewPlayerAttackHitbox_{};
    std::array<PlayerSpecialTimeline, 4> sideSpecialTimelines_{};

    std::array<std::array<PlayerSpecialTimeline,
        4>, static_cast<size_t>(PlayerSpecialAttackType::Count)>
        playerSpecialTimelines_{};

    PlayerSpecialHitboxKeyframe currentSpecialHitbox_{};
    PlayerSpecialMotionKeyframe currentSpecialMotion_{};
    PlayerSpecialPositionKeyframe currentSpecialPosition_{};
    PlayerSpecialOpacityKeyframe currentSpecialOpacity_{};
    PlayerSpecialVisualZKeyframe currentSpecialVisualZ_{};
    PlayerSpecialRotationKeyframe currentSpecialRotation_{};
    PlayerSpecialEffectKeyframe currentSpecialEffect_{};
    PlayerSpecialAnimationKeyframe currentSpecialAnimation_{};
    PlayerSpecialEventKeyframe currentSpecialEvent_{};
    bool playerAttackEditorEnabled_ = false;
    bool drawPlayerAttackHitbox_ = true;
    int playerAttackObjectIndex_ = -1;

    EditorMode editorMode_ = EditorMode::Blender;
    GizmoMode gizmoMode_ = GizmoMode::Translate;
    Vector3 editorCameraPosition_{ 0.0f, 3.0f, -20.0f };
    Vector3 editorCameraRotation_{ 0.0f, 0.0f, 0.0f };
    Vector3 animationCameraPosition_{ 0.0f, 3.0f, -12.0f };
    Vector3 animationCameraRotation_{ 0.0f, 0.0f, 0.0f };
	Vector3 previewSpecialPositionOffset_{ 0.0f, 0.0f, 0.0f };//プレイヤー必殺技の位置オフセット
	float previewSpecialVisualZOffset_ = 0.0f;
	Vector3 playerSpecialPreviewOrigin_{ 0.0f, 0.0f, 0.0f };
	Vector3 playerSpecialPreviewBaseRotation_{ 0.0f, 0.0f, 0.0f };
	bool playerSpecialPreviewOriginInitialized_ = false;
	bool livePreviewSpecialEdit_ = true;
	bool drawPlayerSpecialPath_ = true;
	bool showBossDummy_ = true;
	bool showBossDummyHitbox_ = true;
	bool matchTestSceneLayout_ = true;
	bool useGameCameraPreview_ = false;
	Vector3 bossDummyPosition_{ 6.0f, 0.0f, 0.0f };
	Vector3 bossDummyHalfSize_{ 1.2f, 2.0f, 1.4f };
	int draggedPlayerSpecialPositionKey_ = -1;
	int selectedPlayerSpecialPositionKey_ = -1;
	char playerSpecialEffectPath_[260] = "resources/effects/effect_editor.json";

    float animationCameraFovY_ = 0.45f;
    float editorCameraMoveSpeed_ = 0.2f;
    float editorCameraLookSpeed_ = 0.006f;
    bool editorCameraControlActive_ = false;
    bool editorCameraPanActive_ = false;
    bool shiftMovementActive_ = false;
    bool boxSelectActive_ = false;
    ImVec2 boxSelectStart_{};
    bool transformDragActive_ = false;
    bool transformDragChanged_ = false;
    EditorSnapshot transformDragBefore_{};
    int activeViewportGizmoAxis_ = -1;
    int activeViewportBone_ = -1;
    float viewportGizmoLastMouseX_ = 0.0f;
    float viewportGizmoLastMouseY_ = 0.0f;
    float viewportBoneLastMouseX_ = 0.0f;
    float viewportBoneLastMouseY_ = 0.0f;
    bool viewportBoneDragActive_ = false;
    bool viewportBoneDragChanged_ = false;
    bool pendingDeleteSelectedObject_ = false;
    float reloadCooldown_ = 0.0f;
    std::vector<ParticleNode> particleNodes_;
    int selectedParticleNode_ = -1;
    float lastTimelineTime_ = 0.0f;
    float previousTimelineTime_ = 0.0f;
    bool pendingTimelineRebuild_ = false;
    float pendingTimelineRebuildTime_ = 0.0f;
    EditorMode lastEditorMode_ = EditorMode::Blender;

    enum class DragTarget {
        None = 0,
        TimelineTime,
        ModelKeyframe,
        CameraKeyframe,
        PlayerAttackHitboxKeyframe,
        PlayerSpecialHitboxKeyframe,
        PlayerSpecialPositionKeyframe,
        ParticleNodeStart,
        ParticleNodeEnd,
        ParticleNodeBar,
    };
    DragTarget dragTarget_ = DragTarget::None;
    int dragObjectIndex_ = -1;
    int dragKeyframeIndex_ = -1;
    int dragParticleNodeIndex_ = -1;
    float dragStartOffset_ = 0.0f;
    float dragStartVal1_ = 0.0f;
    float dragStartVal2_ = 0.0f;
    int selectedKeyframeIndex_ = -1;
    DragTarget selectedKeyframeType_ = DragTarget::None;
    int selectedKeyframeObjectIndex_ = -1;
    // 0: all, 1: position, 2: rotation, 3: scale, 4: color
    int selectedModelKeyframeChannel_ = 0;

    //jsonに保存するよう
    bool SavePlayerSpecialTimelinesJson_(
        const std::string& path,
        int attackFilter = -1,
        int levelFilter = -1
    ) const;
    bool LoadPlayerSpecialTimelinesJson_(const std::string& path);

    ParticleTestEditor::PlayerSpecialAttackType selectedPlayerSpecialAttackType_ = ParticleTestEditor::PlayerSpecialAttackType::SideSpecial;

	//jsonに保存する用
    std::string playerSpecialJsonStatus_;

    //選択中の技レベル
    int selectedPlayerSpecialLevel_ = 2;

    void DrawDopeSheet_(GameApp& app);
    bool OpenParticleFileDialog_(std::vector<std::string>& outGroupNames, std::string& outFileName);
	void AddEffectReferenceNode_(const std::string& jsonPath);
    void KeepTimelineTimeInView_();
};
