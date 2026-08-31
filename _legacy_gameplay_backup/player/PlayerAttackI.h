#pragma once

#include <cstdint>

struct Vector3;
class Player;

// Special attack runtime states shared by every I-special variant.
enum class PlayerIAttackState : uint8_t {
    None,
    SideSlide_Windup,
    SideSlide_Move,
    SideSlide_Recover,
    UpRise_Windup,
    UpRise_Move,
    UpRise_Recover,
    DownCounter_Active,
    DownCounter_Success,
    DownCounter_Recover,
    NeutralFinish_Windup,
    NeutralFinish_Active,
    NeutralFinish_Recover,
};

enum class PlayerIAttackType : uint8_t {
    None,
    NeutralSpecial,
    SideSpecial,
    UpSpecial,
    DownSpecial,
};

// Fixed when a special starts. Lv0 is normal, Lv1-3 are cancel-chain variants.
enum class PlayerISpecialVariant : uint8_t {
    Lv0,
    Lv1,
    Lv2,
    Lv3,
};

class PlayerINeutralSpecial {
public:
    static void StartNeutralSpecial(Player& player);
    static void UpdateNeutralSpecial(Player& player, float dt);
    static bool GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static int GetNeutralSpecialDamage(const Player& player);

private:
    static void UpdateNeutralSpecialLv0(Player& player, float dt);
    static void UpdateNeutralSpecialLv1(Player& player, float dt);
    static void UpdateNeutralSpecialLv2(Player& player, float dt);
    static void UpdateNeutralSpecialLv3(Player& player, float dt);
    static bool UpdateNeutralSpecialWaypointMovement(Player& player, float dt, uint8_t spIdx);
};

class PlayerISideSpecial {
public:
    static void StartSideSpecial(Player& player);
    static void UpdateSideSpecial(Player& player, float dt);
    static bool GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static int GetSideSpecialDamage(const Player& player);

private:
    static void UpdateSideSpecialLv0(Player& player, float dt);
    static void UpdateSideSpecialLv1(Player& player, float dt);
    static void UpdateSideSpecialLv2(Player& player, float dt);
    static void UpdateSideSpecialLv3(Player& player, float dt);
};

class PlayerIUpSpecial {
public:
    static void StartUpSpecial(Player& player);
    static void UpdateUpSpecial(Player& player, float dt);
    static bool GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static int GetUpSpecialDamage(const Player& player);

private:
    static void UpdateUpSpecialLv0(Player& player, float dt);
    static void UpdateUpSpecialLv1(Player& player, float dt);
    static void UpdateUpSpecialLv2(Player& player, float dt);
    static void UpdateUpSpecialLv3(Player& player, float dt);
    static bool UpdateUpSpecialWaypointMovement(Player& player, float dt, uint8_t spIdx);
};

class PlayerIDownSpecial {
public:
    static void StartDownSpecial(Player& player);
    static void UpdateDownSpecial(Player& player, float dt);
    static bool GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static int GetDownSpecialDamage(const Player& player);

private:
    static void UpdateDownSpecialLv0(Player& player, float dt);
    static void UpdateDownSpecialLv1(Player& player, float dt);
    static void UpdateDownSpecialLv2(Player& player, float dt);
    static void UpdateDownSpecialLv3(Player& player, float dt);
};

class PlayerIAttack {
public:
    static void StartNeutralSpecial(Player& player);
    static void StartSideSpecial(Player& player);
    static void StartUpSpecial(Player& player);
    static void StartDownSpecial(Player& player);

    static void UpdateNeutralSpecial(Player& player, float dt);
    static void UpdateSideSpecial(Player& player, float dt);
    static void UpdateUpSpecial(Player& player, float dt);
    static void UpdateDownSpecial(Player& player, float dt);

    static bool GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);

    static int GetNeutralSpecialDamage(const Player& player);
    static int GetSideSpecialDamage(const Player& player);
    static int GetUpSpecialDamage(const Player& player);
    static int GetDownSpecialDamage(const Player& player);

    static void ChangeState(Player& player, PlayerIAttackState state);
};
