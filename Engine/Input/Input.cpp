#include "Input.h"
#include <cassert>
#include <cstring>

#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

namespace {

WORD ToXInputButton(Input::GamepadButton button) {
    switch (button) {
    case Input::GamepadButton::A:
        return XINPUT_GAMEPAD_A;
    case Input::GamepadButton::B:
        return XINPUT_GAMEPAD_B;
    default:
        return 0;
    }
}

float NormalizeThumbAxis(SHORT value, SHORT deadZone) {
    const int magnitude = value < 0 ? -static_cast<int>(value) : static_cast<int>(value);
    if (magnitude <= deadZone) {
        return 0.0f;
    }

    const float sign = value < 0 ? -1.0f : 1.0f;
    const float range = static_cast<float>(32767 - deadZone);
    const float normalized = static_cast<float>(magnitude - deadZone) / range;
    return sign * (normalized > 1.0f ? 1.0f : normalized);
}

} // namespace



void Input::Initialize  (WinApp* winApp) {
    HRESULT hr;

    //借りてきたWinAppのインスタンスを記録
    this->winApp_ = winApp;

    // DirectInputの初期化
    hr = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput_, nullptr);
    assert(SUCCEEDED(hr));

    // キーボードデバイスの作成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboardDevice_, nullptr);
    assert(SUCCEEDED(hr));

    // データフォーマットを設定（標準のキーボードフォーマット）
    hr = keyboardDevice_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 協調レベルの設定
    hr = keyboardDevice_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(hr));

    // デバイスの取得開始
    keyboardDevice_->Acquire();
}

void Input::UpdateMouseDelta() {
    POINT currentMousePos;
    GetCursorPos(&currentMousePos);
    HWND hwnd = winApp_ ? winApp_->GetHwnd() : GetActiveWindow();
    ScreenToClient(hwnd, &currentMousePos);

    if (firstMouseUpdate_) {
        // 初回は差分をゼロにしておく
        mouseDelta_ = { 0, 0 };
        firstMouseUpdate_ = false;
    } else {
        mouseDelta_.x = currentMousePos.x - prevMousePos_.x;
        mouseDelta_.y = currentMousePos.y - prevMousePos_.y;
    }

    prevMousePos_ = currentMousePos;

    if (cameraControlEnabled_) {
        // ウィンドウの中央座標を取得して固定
        RECT rect;
        GetClientRect(hwnd, &rect);
        POINT center;
        center.x = (rect.right - rect.left) / 2;
        center.y = (rect.bottom - rect.top) / 2;

        // 現在位置を取得
        POINT currentMousePos;
        GetCursorPos(&currentMousePos);
        ScreenToClient(hwnd, &currentMousePos);

        // 差分計算
        mouseDelta_.x = currentMousePos.x - center.x;
        mouseDelta_.y = currentMousePos.y - center.y;

        // マウスを中央に戻す
        ClientToScreen(hwnd, &center);
        SetCursorPos(center.x, center.y);
    } else {
        mouseDelta_ = { 0, 0 };
    }

}


void Input::Update() {
    // 前フレームの状態を保存
    memcpy(prevKeys_, keys_, sizeof(keys_));
    prevGamepadState_ = gamepadState_;
    XINPUT_STATE newGamepadState{};
    gamepadConnected_ = XInputGetState(0, &newGamepadState) == ERROR_SUCCESS;
    gamepadState_ = gamepadConnected_ ? newGamepadState : XINPUT_STATE{};

    HRESULT hr = keyboardDevice_->GetDeviceState(sizeof(keys_), keys_);
    if (FAILED(hr)) {
        // Acquireを毎フレーム実行すると極めて重くなるため、1秒のインターバルを設ける
        static DWORD lastAcquireTime = 0;
        DWORD currentTime = GetTickCount();
        if (currentTime - lastAcquireTime > 1000) {
            keyboardDevice_->Acquire();
            lastAcquireTime = currentTime;
        }
        memset(keys_, 0, sizeof(keys_));
    }

    UpdateMouseDelta();

    // === 修正済み：トグル処理は1回だけ ===
    bool toggleKey = keys_[DIK_F1];
    if (toggleKey && !prevToggleKeyState_) {
        SetCameraControlEnabled(!cameraControlEnabled_);
        justEnteredCameraMode_ = cameraControlEnabled_; // 初回だけtrue

    }
    prevToggleKeyState_ = toggleKey;
}

void Input::SetCameraControlEnabled(bool enabled) {
    if (cameraControlEnabled_ == enabled) {
        return;
    }

    cameraControlEnabled_ = enabled;
    justEnteredCameraMode_ = enabled;
    firstMouseUpdate_ = true;
    mouseDelta_ = { 0, 0 };

    ShowCursor(!cameraControlEnabled_);
}

bool Input::IsKeyTrigger(BYTE keyCode) const {
    // DirectInput は 0x80 が押下
    return (keys_[keyCode] & 0x80) && !(prevKeys_[keyCode] & 0x80);
}

bool Input::IsKeyPressed(BYTE keyCode) const {
    return (keys_[keyCode] & 0x80) != 0;
}

bool Input::IsKeyReleased(BYTE keyCode) const {
    return !(keys_[keyCode] & 0x80) && (prevKeys_[keyCode] & 0x80);
}

bool Input::IsGamepadButtonPressed(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ && (gamepadState_.Gamepad.wButtons & mask) != 0;
}

bool Input::IsGamepadButtonTrigger(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ &&
        (gamepadState_.Gamepad.wButtons & mask) != 0 &&
        (prevGamepadState_.Gamepad.wButtons & mask) == 0;
}

bool Input::IsGamepadButtonReleased(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ &&
        (gamepadState_.Gamepad.wButtons & mask) == 0 &&
        (prevGamepadState_.Gamepad.wButtons & mask) != 0;
}

float Input::GetLeftStickX() const {
    return gamepadConnected_
        ? NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        : 0.0f;
}

float Input::GetLeftStickY() const {
    return gamepadConnected_
        ? NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        : 0.0f;
}

bool Input::IsLeftStickUpTrigger(float threshold) const {
    if (!gamepadConnected_) {
        return false;
    }

    const float currentY =
        NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const float previousY =
        NormalizeThumbAxis(prevGamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    return currentY > threshold && previousY <= threshold;
}

//
//bool Input::IsKeyPressed(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) != 0;
//}
//bool Input::IsKeyReleased(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) == 0;
//}
