#pragma once

#include <dinput.h>
#include <Xinput.h>
#include <Windows.h>
#include "WinApp.h"

class Input {
public:
    enum class GamepadButton {
        A,
        B,
    };

    // 初期化
    void Initialize(WinApp* winApp);

    // 更新処理（毎フレーム呼び出し）
    void Update();

    // Runtime panels consume gameplay input without losing raw F-key toggles.
    void SetGameInputBlocked(bool blocked);
    bool IsGameInputBlocked() const { return gameInputBlocked_; }
    bool IsRawKeyTrigger(BYTE keyCode) const;

    // トリガー（今回押されたが前回押されていない）
    bool IsKeyTrigger(BYTE keyCode) const;

    // 押しっぱなし
    bool IsKeyPressed(BYTE keyCode) const;

    // 離した瞬間
    bool IsKeyReleased(BYTE keyCode) const;

    bool IsGamepadConnected() const { return !gameInputBlocked_ && gamepadConnected_; }
    bool IsGamepadButtonPressed(GamepadButton button) const;
    bool IsGamepadButtonTrigger(GamepadButton button) const;
    bool IsGamepadButtonReleased(GamepadButton button) const;
    float GetLeftStickX() const;
    float GetLeftStickY() const;
    bool IsLeftStickUpTrigger(float threshold = 0.25f) const;

    POINT prevMousePos_{};
    POINT mouseDelta_{};

    void UpdateMouseDelta();
    POINT GetMouseDelta() const { return gameInputBlocked_ ? POINT{} : mouseDelta_; }
    // Position in the 1280x720 UI canvas; false outside the active game window.
    bool GetMenuMousePosition(POINT& position) const;
    int GetMouseDeltaX() const { return gameInputBlocked_ ? 0 : mouseDelta_.x; }
    int GetMouseDeltaY() const { return gameInputBlocked_ ? 0 : mouseDelta_.y; }
    void SetCameraControlEnabled(bool enabled);
    bool IsCameraControlEnabled() const { return cameraControlEnabled_; }

    bool IsMouseLeftPressed() const { return !gameInputBlocked_ && !suppressedMouseLeft_ && mouseLeft_; }
    bool IsMouseLeftTrigger() const { return !gameInputBlocked_ && !suppressedMouseLeft_ && mouseLeft_ && !prevMouseLeft_; }
    bool IsMouseLeftReleased() const { return !gameInputBlocked_ && !suppressedMouseLeft_ && !mouseLeft_ && prevMouseLeft_; }

    bool IsMouseRightPressed() const { return !gameInputBlocked_ && !suppressedMouseRight_ && mouseRight_; }
    bool IsMouseRightTrigger() const { return !gameInputBlocked_ && !suppressedMouseRight_ && mouseRight_ && !prevMouseRight_; }

private:
    IDirectInput8* directInput_ = nullptr;
    IDirectInputDevice8* keyboardDevice_ = nullptr;
    BYTE keys_[256]{};
    BYTE prevKeys_[256]{};
    bool gameInputBlocked_ = false;
    bool cameraControlBeforeBlock_ = false;
    BYTE suppressedKeys_[256]{};
    WORD suppressedGamepadButtons_ = 0;
    bool suppressedStickX_ = false;
    bool suppressedStickY_ = false;
    bool suppressedMouseLeft_ = false;
    bool suppressedMouseRight_ = false;
    bool firstMouseUpdate_ = true;
    bool cameraControlEnabled_ = false;
    bool prevToggleKeyState_ = false; // トグル用
    bool justEnteredCameraMode_ = false;
    XINPUT_STATE gamepadState_{};
    XINPUT_STATE prevGamepadState_{};
    bool gamepadConnected_ = false;

    bool mouseLeft_ = false;
    bool prevMouseLeft_ = false;
    bool mouseRight_ = false;
    bool prevMouseRight_ = false;

    WinApp* winApp_ = nullptr;

};
