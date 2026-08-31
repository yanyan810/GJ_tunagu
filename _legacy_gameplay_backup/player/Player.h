#pragma once
#include <memory>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "AABB.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "LightingParam.h"
#include "PlayerAttackI.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;

class EnemyManager;

class Player {
    // ===== 吹っ飛び（Launch）の物理状態 =====
    enum class LaunchState {
        None,
        Launch,
        FreeFall,
        Down,
    };

public:

    // ===== プレイヤーの3Dモデルセットの種類 =====
    enum class PlayerModelSet {
        HumanWalk,
        HumanSneakWalk,
        GltfWalkGlb,
        GltfTestGltf,
        Player2Gltf,
    };

    // ===== アニメーション/モデルID =====
    enum class ModelId { Walk, I0, I1, I2, O0, O1, O2 };

    // ===== プレイヤーの基本行動アクション =====
    enum class PlayerAction {
        Idle,
        Move,
        Jump,
        Crouch,
        FastFall,
        Guard,
        Attack,
        Launched,
    };

    // ===== 攻撃技の種類 =====
    enum class PlayerAttackType {
        None,
        Weak,
        Tilt,
        Smash,
        NeutralSpecial,
        SideSpecial,
        UpSpecial,
        DownSpecial,
    };

    // ===== 攻撃グループ（通常、スマッシュ、空中） =====
    enum class PlayerAttackGroup : uint8_t {
        Ground,
        Smash,
        Air,
        Count,
    };

    // ===== 攻撃の方向派生（ニュートラル、横、上） =====
    enum class PlayerAttackVariant : uint8_t {
        Neutral,
        Side,
        Up,
        Count,
    };

    // ===== 攻撃技ごとの物理・判定パラメータ定義 =====
    struct PlayerAttackDefinition {
        std::string name;
        Vector3 offset = { 0.9f, 0.7f, 0.0f };
        Vector3 halfSize = { 0.55f, 0.70f, 0.45f };
        float startDelaySec = 0.0f;
        float activeSec = 0.12f;
        float actionSec = 0.28f;
        int damage = 8;
    };

    // ===== 上必殺技Lv3 ジグザグ移動の経由地 =====
    struct UpLv3Waypoint {
        float offsetX = 0.0f;           // ボス基準 X オフセット（facing で左右反転）
        float offsetY = 0.0f;           // ボス基準 Y オフセット
        float duration = 0.2f;          // この経由地への移動時間（秒）
        std::vector<float> hits;        // 当たり判定マーカー（セグメント内 0.0〜1.0）
        int interpolation = 0;          // 0: Linear, 1: EaseIn, 2: EaseOut, 3: EaseInOut, 4: Step
        float offsetZ = 0.0f;
        bool targetRelative = false;
        bool advanceOnHit = false;
    };

    // ===== 上必殺技Lv3 クロスマーク軌跡の線分 =====
    struct TrailLine {
        Vector3 start;
        Vector3 end;
    };

    // ===== 必殺技を指定するための Enum =====
    enum class SpecialMoveIndex : uint8_t {
        NeutralSpecial_Lv1,
        NeutralSpecial_Lv2,
        NeutralSpecial_Lv3,
        UpSpecial_Lv1,
        UpSpecial_Lv2,
        UpSpecial_Lv3,
        Count
    };

    // ===== 各必殺技用設定データ構造体 =====
    struct SpecialMoveTuning {
        float startOffsetX = 5.0f;
        float startOffsetY = 0.0f;
        bool startFollowPlayer = false;
        float speedRate = 1.0f;
        float hitStopSec = 0.06f;
        std::vector<UpLv3Waypoint> waypoints;
        float startOffsetZ = 0.0f;
        bool startTargetRelative = false;
        bool startAdvanceOnHit = false;
    };

    struct SpecialEffectKeyframe {
        float time = 0.0f;
        std::string templateName;
        std::string jsonPath;
        Vector3 offset{ 0.0f, 0.0f, 0.0f };
        bool followPlayerMovement = true;
        int positionMode = 1; // 0: fixed at spawn, 1: follow player, 2: movement point
        int movementPointIndex = -1;
        Vector3 movementPointOffset{ 0.0f, 0.0f, 0.0f };
        bool movementPointTargetRelative = false;
    };

    struct SpecialHitboxTiming {
        float time = 0.0f;
        float duration = 0.08f;
        float hitStopSec = 0.14f;
        Vector3 offset{ 1.0f, 1.0f, 0.0f };
        Vector3 halfSize{ 0.6f, 0.8f, 0.5f };
        int damage = 12;
        bool active = true;
        bool followPlayerMovement = true;
    };

    // ===== 解決済みのプレイヤー入力コマンド =====
    struct PlayerInputCommand {
        PlayerAction action = PlayerAction::Idle;
        PlayerAttackType attackType = PlayerAttackType::None;
        PlayerAttackGroup attackGroup = PlayerAttackGroup::Ground;
        PlayerAttackVariant attackVariant = PlayerAttackVariant::Neutral;
        int horizontal = 0;
        int depth = 0;
        bool down = false;
        bool jumpTriggered = false;
        bool guard = false;
        bool specialHeld = false;
        bool specialReleased = false;
    };

    struct SpecialVisualZKeyframe {
        float time = 0.0f;
        float offsetZ = 0.0f;
        int interpolation = 0; // 0 linear, 1 ease-in, 2 ease-out, 3 ease-in-out, 4 step
    };

    using InputCommandFilter = std::function<void(PlayerInputCommand&)>;


    // ===== 初期化・更新・描画・デバッグ用コマンド =====
    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);
    void SetCamera(Camera* cam);
    void Update(float dt, const Input& input, EnemyManager& enemyMgr);
    void QueueDebugCommand(const PlayerInputCommand& command);
    void SetInputCommandFilter(InputCommandFilter filter) { inputCommandFilter_ = std::move(filter); }
    void SetExternalInputBlocked(bool blocked) { externalInputBlocked_ = blocked; }
    void Draw();
    void DrawPostEffectTarget();
    void DrawDebugHitBoxes(EnemyManager& enemyMgr);

    // ===== 攻撃・判定・キャンセル関連のクエリ・通知 =====
    bool GetAttackDebugVisualBox(Vector3& outCenter, Vector3& outHalfSize, bool& outIsActive) const;
    bool GetAttackHitBox(AABB& outHitBox) const;
    bool GetAttackHitBoxes(std::vector<AABB>& outHitBoxes) const;
    int GetAttackDamage() const;
    unsigned int GetAttackSerial() const { return attackSerial_; }
    int GetCancelGauge() const { return cancelGauge_; }
    int GetMaxCancelGauge() const { return kMaxCancelGauge_; }
    bool HasSpecialCancelRight() const { return hasSpecialCancelRight_; }
    bool HasSpecialChainCancelRight() const { return hasSpecialChainCancelRight_; }
    bool CanSpecialCancelNow() const;
    bool HasSpecialHitDuringAction() const { return specialHitDuringAction_; }
    bool DidUseSpecialCancelThisAction() const { return specialCancelUsedThisAction_; }
    float GetSpecialCancelDebugFlashSec() const { return specialCancelDebugFlashSec_; }
    int GetSpecialCancelCount() const { return specialCancelCount_; }
    int GetSpecialCancelEffectLevel() const { return specialCancelEffectLevel_; }
    int GetSpecialCancelCameraLevel() const { return specialCancelCameraLevel_; }
    int GetSpecialCancelSoundLevel() const { return specialCancelSoundLevel_; }
    int GetCurrentSpecialVariantLevel() const { return static_cast<int>(iSpecialVariant_); }
    float GetCurrentSpecialHitStopRate() const;
    float GetCurrentSpecialHitStopSec() const;
    bool IsSideSpecialLv3AttackActive() const;
    bool ShouldFreezeBossForCurrentSpecial() const;
    int GetUComboStageDisplay() const { return lastUComboStage_ + 1; }
    bool IsUComboAccepting() const { return IsUComboAccepting_(); }
    float GetUComboResetTimer() const { return uComboResetTimer_; }
    float GetUComboBufferTimer() const { return uComboBufferTimer_; }
    float GetUComboDebugFlashSec() const { return uComboDebugFlashSec_; }
    void NotifyAttackHit();
    bool IsCounterActive() const;
    void NotifyCounterSuccess();

    // ===== 移動・座標取得・操作ロック =====
    void LockMove(float sec) { if (sec > moveLockSec_) moveLockSec_ = sec; }
    bool IsMoveLocked() const { return moveLockSec_ > 0.0f; }
    Vector2 GetPos2D() const { return { pos_.x, pos_.y }; }
    Vector2 GetVel2D() const { return { vel_.x, vel_.y }; }
    float GetZ() const { return pos_.z; }
    Vector3 GetPos3D() const { return pos_; }
    bool IsOnGround() const { return onGround_; }
    int  GetFacing() const { return facing_; }
    AABB GetBodyAABB() const { return body_; }
    void TriggerHitFlash(float sec = 0.20f) { hitFlashSec_ = std::max(hitFlashSec_, sec); }

    // ===== ダメージ・HP・吹っ飛び（Launch）関連 =====
    void AddHP(int heal);
    void Damage(int Damage);
    void AddDamagePercent(float damagePercent);
    void SetDamagePercent(float damagePercent);
    float GetDamagePercent() const { return damagePercent_; }
    float GetGravity() const { return gravity_; }
    void ApplyLaunch(const Vector3& velocity, float hitStunSec, float actionSpeedRatio = 0.0f);
    void ApplyBossHit(float damagePercent, float baseKnockback, float knockbackScale, const Vector3& knockbackDir, float hitStunSec, float actionSpeedRatio = 0.0f);

    // 吹っ飛び中のXZ速度減衰率（0=即停止, 1=減衰なし, 例:0.15=疾走感イーズアウト）
    float GetLaunchXZDrag() const { return launchXZDrag_; }
    void  SetLaunchXZDrag(float drag) { launchXZDrag_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchXZDragHigh() const { return launchXZDragHigh_; }
    void  SetLaunchXZDragHigh(float drag) { launchXZDragHigh_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchXZDragLow() const { return launchXZDragLow_; }
    void  SetLaunchXZDragLow(float drag) { launchXZDragLow_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchDragThreshold() const { return launchDragThreshold_; }
    void  SetLaunchDragThreshold(float th) { launchDragThreshold_ = std::clamp(th, 0.0f, 1.0f); }
    bool  GetLaunchDragUseTime() const { return launchDragUseTime_; }
    void  SetLaunchDragUseTime(bool use) { launchDragUseTime_ = use; }
    float GetLaunchBounceRestitution() const { return launchBounceRestitution_; }
    void  SetLaunchBounceRestitution(float rest) { launchBounceRestitution_ = std::clamp(rest, 0.0f, 1.0f); }
    float GetLaunchBounceFriction() const { return launchBounceFriction_; }
    void  SetLaunchBounceFriction(float fric) { launchBounceFriction_ = std::clamp(fric, 0.0f, 1.0f); }
    float GetLaunchBounceMinSpeed() const { return launchBounceMinSpeed_; }
    void  SetLaunchBounceMinSpeed(float speed) { launchBounceMinSpeed_ = std::max(0.0f, speed); }
    float GetLaunchKeepSpeedThreshold() const { return launchKeepSpeedThreshold_; }
    void  SetLaunchKeepSpeedThreshold(float speed) { launchKeepSpeedThreshold_ = std::max(0.0f, speed); }
    float GetFreeFallGroundBounceSpeed() const { return freeFallGroundBounceSpeed_; }
    void  SetFreeFallGroundBounceSpeed(float speed) { freeFallGroundBounceSpeed_ = std::max(0.0f, speed); }
    float GetFreeFallGroundBounceDamping() const { return freeFallGroundBounceDamping_; }
    void  SetFreeFallGroundBounceDamping(float damping) { freeFallGroundBounceDamping_ = std::clamp(damping, 0.0f, 1.0f); }
    bool IsLaunched() const { return launched_; }
    bool IsLaunchFastPhase() const {
        return launched_ && launchState_ == LaunchState::Launch;
    }
    void SetHP(int hp);
    float GetX() const { return pos_.x; }
	int GetHP() const { return hp_; }
	int GetMaxHP() const { return 100; }

    // ===== 衝突解決（AABB） =====
    void SetSpawnPos(const Vector3& p);
    void SetDropRespawnPos(const Vector3& p);
    void SetPos(const Vector3& p);
    bool ResolveGroundAABB(const AABB& ground);
    bool ResolveGroundAABB(const std::vector<AABB>& grounds);
    bool ResolveObstaclesAABB(const std::vector<AABB>& obstacles);
    bool IsDead() const { return dead_; }

    // ===== つかまれ状態 =====
    void SetGrabbed(bool grabbed) {
        isGrabbed_ = grabbed;
        if (grabbed) {
            SetExternalInputBlocked(true);
            vel_ = { 0.0f, 0.0f, 0.0f };
        } else {
            SetExternalInputBlocked(false);
        }
    }
    bool IsGrabbed() const { return isGrabbed_; }

    // ===== ライティング・描画・その他 =====
    void SetLighting(const LightingParam& p);
    void ChangeModelSet_(Player::PlayerModelSet set);
    void UpdateTitleAttackDemo(float dt, float intervalSec = 1.0f);
    void ResetTitleAttackDemo();
    void SetTitleTransform(const Vector3& t, const Vector3& r, const Vector3& s);
    Object3d* GetModelObject() const { return model_.get(); }
    PlayerAction GetCurrentAction() const { return action_; }
    PlayerAttackType GetCurrentAttackType() const { return attackType_; }
    PlayerAttackGroup GetCurrentAttackGroup() const { return activeAttackGroup_; }
    PlayerAttackVariant GetCurrentAttackVariant() const { return activeAttackVariant_; }
    int GetSpecialPulseIndex() const { return iSpecialPulseIndex_; }
    PlayerAttackDefinition& AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant);
    const PlayerAttackDefinition& AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) const;
    static const char* AttackGroupName(PlayerAttackGroup group);
    static const char* AttackVariantName(PlayerAttackVariant variant);

    // ===== ボーン情報・演出 =====
    bool TryGetBoneWorldPosition(
        const std::string& jointName,
        Vector3& out,
        const Vector3& localOffset = { 0.0f, 0.0f, 0.0f }) const;
    bool EmitParticleFromBone(
        const std::string& groupName,
        const std::string& jointName,
        uint32_t count,
        const Vector3& localOffset = { 0.0f, 0.0f, 0.0f }) const;

    // ===== 必殺技汎用エディタ用パラメータアクセス =====
    const SpecialMoveTuning& GetSpecialMoveTuning(SpecialMoveIndex idx) const { return specialMoveTunings_[static_cast<size_t>(idx)]; }
    SpecialMoveTuning& GetSpecialMoveTuningMutable(SpecialMoveIndex idx) { return specialMoveTunings_[static_cast<size_t>(idx)]; }
	bool LoadSpecialAttackMovementJson(const std::string& path = "resources/Data/PlayerIAttacks.json");

    // ===== 後方互換マッピング（上Lv3） =====
    float GetUpLv3StartOffsetX() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startOffsetX; }
    float GetUpLv3StartOffsetY() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startOffsetY; }
    bool GetUpLv3StartFollowPlayer() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startFollowPlayer; }
    float GetUpLv3SpeedRate() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].speedRate; }
    float GetUpLv3HitStopSec() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].hitStopSec; }

    float& GetUpLv3StartOffsetXMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startOffsetX; }
    float& GetUpLv3StartOffsetYMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startOffsetY; }
    bool& GetUpLv3StartFollowPlayerMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].startFollowPlayer; }
    float& GetUpLv3SpeedRateMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].speedRate; }
    float& GetUpLv3HitStopSecMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].hitStopSec; }

    const std::vector<UpLv3Waypoint>& GetUpLv3Waypoints() const { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].waypoints; }
    std::vector<UpLv3Waypoint>& GetUpLv3WaypointsMutable() { return specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)].waypoints; }

    // ===== 必殺技（通常Lv1・牙突）エディタ用パラメータアクセス =====
    float GetNeutralLv1ThrustSpeed() const { return neutralLv1ThrustSpeed_; }
    float GetNeutralLv1ThrustSec() const { return neutralLv1ThrustSec_; }

    float& GetNeutralLv1ThrustSpeedMutable() { return neutralLv1ThrustSpeed_; }
    float& GetNeutralLv1ThrustSecMutable() { return neutralLv1ThrustSec_; }

    // ===== デバッグ用強制必殺技発動 =====
    void DebugTriggerSpecialAttack(PlayerAttackType type, int level, const Vector3* optTarget = nullptr);

    // ===== 必殺技（上Lv3）固定ターゲット =====
    const Vector3& GetUpSpecialTarget() const { return upSpecialTarget_; }
    bool IsUpSpecialTargetFixed() const { return upSpecialTargetFixed_; }
    void SetUpSpecialTarget(const Vector3& target) { upSpecialTarget_ = target; upSpecialTargetFixed_ = true; }
    void ClearUpSpecialTarget() { upSpecialTargetFixed_ = false; }
    const Vector3& GetUpSpecialStartPos() const { return upSpecialStartPos_; }
    void SetUpSpecialStartPos(const Vector3& pos) { upSpecialStartPos_ = pos; }

private:
    // ===== 内部ヘルパー関数 (Private) =====
    PlayerInputCommand ResolveInput_(const Input& input);
    void UpdateSmashInputWindow_(const Input& input);
    void ApplyActionCommand_(const PlayerInputCommand& command);
    void StartAttackAction_(PlayerAttackType type, int horizontal, PlayerAttackGroup group, PlayerAttackVariant variant);
    void UpdateActionTimer_(float dt);
    void PlayActionAnimation_(const PlayerInputCommand& command);
    bool CanStartAttackCommand_(const PlayerInputCommand& command) const;
    bool IsUAttackType_(PlayerAttackType type) const;
    bool IsIAttackType_(PlayerAttackType type) const;
    bool IsUComboAccepting_() const;
    bool IsFinalUComboRecoveryCancelable_() const;
    bool IsSpecialRecoveryCancelable_() const;
    void ApplyLandingRecovery_();
    bool GetAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    void PrepareSpecialCommandTarget_(const PlayerInputCommand& command, const EnemyManager& enemyMgr);
    bool BuildUAttackCommand_(PlayerInputCommand& command) const;
    bool BuildIAttackCommand_(PlayerInputCommand& command) const;
    void InitializeUAttackDefinitions_();
    void InitializeIAttackDefinitions_();
    float GetAttackActionSec_(PlayerAttackType type, PlayerAttackGroup group, PlayerAttackVariant variant) const;
    bool GetUAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    bool GetIAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    int GetUAttackDamage_() const;
    int GetIAttackDamage_() const;
    void StartIAttack_(PlayerAttackType type);
    void UpdateIAttack_(float dt);
    void ChangeIAttackState_(PlayerIAttackState state);
    void ResetSpecialAttackEffects_();
    void UpdateSpecialAttackEffects_();
    void UpdateSpecialAttackVisual_();
    void UpdateMove_(float dt, const Input& input);
    void UpdateMove_(float dt, const PlayerInputCommand& command);
    void ApplyPhysics_(float dt);
    void UpdateModel_();
    void UpdateBody_();
    float GetLaunchSpeed_() const;
    void ResetLaunchState_(PlayerAction nextAction);
    void EnterFreeFall_();
    void UpdateLaunchStateAfterBounce_();
    bool HandleLaunchGroundContact_();
    bool HandleFreeFallGroundContact_();

private:
    // ===== メンバ変数 =====
    
    // ===== グラフィックス・モデル・カメラ =====
    // player.gltfは従来1.0倍で表示していたため、評価用に追加されたPMX向け縮小率を戻す。
    static constexpr float kPmxVisualScale_ = 1.0f;

    ModelId currentModel_ = ModelId::Walk;
    std::unique_ptr<Object3d> model_;
    std::unique_ptr<Object3d> debugAtkCube_;
    std::unique_ptr<Object3d> debugEnemyCube_;
    Camera* cam_ = nullptr;
    std::unique_ptr<Object3d> shadow_;
    float shadowBaseScale_ = 1.2f;
    float shadowMaxAlpha_ = 0.45f;
    float shadowLiftY_ = 0.02f;
    float shadowMinAlpha_ = 0.05f;
    PlayerModelSet currentModelSet_ = PlayerModelSet::HumanWalk;
    std::string curAnim_ = "";
    bool prevAtkI_ = false;
    bool prevAtkO_ = false;
    Model* walkModels_[5];
    Model* iAtkModels_[3] = { nullptr, nullptr, nullptr };
    Model* oAtkModels_[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };

    // ===== 座標・物理状態 =====
    Vector3 pos_{ 0.0f, 0.0f, 15.0f };
    Vector3 vel_{ 0.0f, 0.0f, 0.0f };
    bool onGround_ = true;
    int  facing_ = +1;
    int jumpCount_ = 0;
    int maxJumpCount_ = 2;
    float moveSpeed_ = 10.0f;
    float depthSpeed_ = 14.0f;
    float jumpVel_ = 12.0f;
    float gravity_ = 25.0f;
    float zView_ = 15.0f;
    AABB body_{};
    bool isMoving = false;

    // ===== アクション・タイマー =====
    float moveLockSec_ = 0.0f;
    PlayerAction action_ = PlayerAction::Idle;
    float actionTimer_ = 0.0f;

    // ===== 通常攻撃・コンボ関連 =====
    PlayerAttackType attackType_ = PlayerAttackType::None;
    PlayerAttackGroup activeAttackGroup_ = PlayerAttackGroup::Ground;
    PlayerAttackVariant activeAttackVariant_ = PlayerAttackVariant::Neutral;
    float attackElapsedSec_ = 0.0f;
    bool currentAttackHit_ = false;
    int uComboStage_ = 0;
    int lastUComboStage_ = 0;
    float uComboResetTimer_ = 0.0f;
    float uComboBufferTimer_ = 0.0f;
    float uComboDebugFlashSec_ = 0.0f;
    PlayerInputCommand bufferedUComboCommand_{};
    std::array<std::array<PlayerAttackDefinition, static_cast<size_t>(PlayerAttackVariant::Count)>, static_cast<size_t>(PlayerAttackGroup::Count)> attackDefinitions_{};

    // ===== 必殺技（Special）とキャンセル関連 =====
    static constexpr int kMaxCancelGauge_ = 3;
    static constexpr int kMaxSpecialCancelCount_ = 3;
    int cancelGauge_ = kMaxCancelGauge_;
    int specialCancelCount_ = 0;
    int specialCancelEffectLevel_ = 0;
    int specialCancelCameraLevel_ = 0;
    int specialCancelSoundLevel_ = 0;
    bool hasSpecialCancelRight_ = false;
    bool hasSpecialChainCancelRight_ = false;
    bool specialChainCancelEligible_ = false;
    bool specialHitDuringAction_ = false;
    bool specialCancelUsedThisAction_ = false;
    float specialCancelDebugFlashSec_ = 0.0f;
    PlayerInputCommand bufferedSpecialCancelCommand_{};
    float specialCancelBufferTimer_ = 0.0f;
    bool latestSpecialHeld_ = false;
    bool latestSpecialReleased_ = false;

    // ===== 必殺技（横）：スライド/ロックオン =====
    bool sideSpecialHitBounceUsed_ = false;
    bool nextSideSpecialLockOn_ = false;
    bool sideSpecialLockOnActive_ = false;
    Vector3 sideSpecialLockOnTarget_{};
    Vector3 sideSpecialLockOnDirection_{ 1.0f, 0.0f, 0.0f };

    // ===== 必殺技（溜め・カウンター等個別） =====
    float iSpecialChargeSec_ = 0.0f;
    bool iCounterSuccess_ = false;
    PlayerISpecialVariant iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    int iSpecialPulseIndex_ = 0;
    PlayerIAttackState iAttackState_ = PlayerIAttackState::None;
    float iAttackStateTime_ = 0.0f;
    bool iAttackHitActive_ = false;

    // ===== 必殺技エディタ用汎用パラメータ =====
    std::array<SpecialMoveTuning, static_cast<size_t>(SpecialMoveIndex::Count)> specialMoveTunings_;
    std::array<std::array<std::vector<SpecialEffectKeyframe>, 4>, 4> specialEffectKeyframes_{};
    std::array<std::array<bool, 4>, 4> specialFreezeBossDuringAttack_{};
    std::array<std::array<std::vector<SpecialHitboxTiming>, 4>, 4> specialHitboxTimings_{};
    std::array<std::array<std::vector<SpecialVisualZKeyframe>, 4>, 4> specialVisualZKeyframes_{};
    Vector3 specialAttackStartPosition_{};
    float specialVisualZOffset_ = 0.0f;
    size_t nextSpecialEffectKey_ = 0;
    PlayerAttackType specialEffectAttackType_ = PlayerAttackType::None;
    int specialEffectLevel_ = -1;
    float specialEffectLastElapsedSec_ = -1.0f;
    uint32_t specialHitConfirmSerial_ = 0;
    uint32_t specialWaypointConsumedHitSerial_ = 0;
    int specialWaypointPassedPositionIndex_ = -1;
    int specialWaypointActiveGatePositionIndex_ = -1;

    // ===== 上必殺技ジグザグ移動先固定ターゲット =====
    Vector3 upSpecialTarget_{};
    bool upSpecialTargetFixed_ = false;
    Vector3 upSpecialStartPos_{};
    std::vector<TrailLine> upSpecialTrailLines_;

    // ===== 必殺技（通常Lv1・牙突）エディタ用パラメータ =====
    float neutralLv1ThrustSpeed_ = 16.0f;
    float neutralLv1ThrustSec_ = 0.16f;

    // ===== ガード・しゃがみ・急降下 =====
    bool guarding_ = false;
    bool crouching_ = false;
    bool fastFalling_ = false;
    float fastFallSpeed_ = 28.0f;

    // ===== 吹っ飛び（Launch）パラメータ =====
    bool launched_ = false;
    float launchedTimer_ = 0.0f;
    float launchInitialSpeed_ = 0.0f;
    float launchActionSpeedRatio_ = 0.0f;
    bool launchControlUnlocked_ = false;
    float launchXZDrag_ = 0.18f;   // 吹っ飛びXZ速度の減衰率（pow(drag, dt)で毎フレーム乗算）
    float launchedTotalTime_ = 0.0f;
    float launchXZDragHigh_ = 0.95f; // 高速時のXZドラッグ (1.0に近いほど減衰しにくい)
    float launchXZDragLow_ = 0.15f;  // 低速時のXZドラッグ
    float launchDragThreshold_ = 0.20f; // 切り替えの閾値 (0.0〜1.0)
    bool launchDragUseTime_ = true;  // 残り時間割合で判定するか (falseなら残り速度割合)
    float launchBounceRestitution_ = 0.65f; // 跳ね返り反発係数 (0.0〜1.0)
    float launchBounceFriction_ = 0.90f;    // 跳ね返り時の他軸摩擦係数 (0.0〜1.0)
    float launchBounceMinSpeed_ = 4.0f;     // 跳ね返りが発生する最低速度
    LaunchState launchState_ = LaunchState::None;
    bool freeFallSmallBounceUsed_ = false;
    bool launchHasBounced_ = false;
    float launchKeepSpeedThreshold_ = 8.0f;
    float freeFallGroundBounceSpeed_ = 3.5f;
    float freeFallGroundBounceDamping_ = 0.35f;

    // ===== 着地関連 =====
    bool suppressLandingRecoveryUntilAttackEnd_ = false;
    bool landingRecoveryPending_ = false;

    // ===== 被ダメージ・HP =====
    int hp_ = 100;
    float damagePercent_ = 0.0f;
    bool dead_ = false;

    // ===== アニメーション・演出パラメータ =====
    float walkAnimTime_ = 0.0f;
    static constexpr int   kWalkFrameCount_ = 5;
    static constexpr float kWalkFps_ = 10.0f;
    float iAtkAnimTime_ = 0.0f;
    static constexpr float kIAttackFps_ = 12.0f;
    float oAtkAnimTime_ = 0.0f;
    static constexpr int   kOFrameCount_ = 5;
    static constexpr float kOAttackFps_ = 12.0f;
    float hitFlashSec_ = 0.0f;
    Vector4 normalColor_{ 1,1,1,1 };
    Vector4 hitColor_{ 1,0.2f,0.2f,1 };
    LightingParam light_;

    // ===== デバッグ・その他 =====
    bool hasDebugCommand_ = false;
    InputCommandFilter inputCommandFilter_;
    bool externalInputBlocked_ = false;
    bool isGrabbed_ = false;
    PlayerInputCommand debugCommand_{};
    unsigned int attackSerial_ = 0;
    float titleDemoTimer_ = 0.0f;
    bool  titleDemoNextIsI_ = true;

    // ===== スマッシュ入力ウィンドウ =====
    static constexpr int kSmashInputWindowFrames_ = 5;
    int recentHorizontalDir_ = 0;
    int recentHorizontalFrames_ = 0;

    friend class PlayerIAttack;
    friend class PlayerINeutralSpecial;
    friend class PlayerISideSpecial;
    friend class PlayerIUpSpecial;
    friend class PlayerIDownSpecial;
};
