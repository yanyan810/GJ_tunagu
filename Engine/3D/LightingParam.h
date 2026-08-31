#pragma once
#include "MathStruct.h"

struct LightingParam {
    int lightingMode = 2;

    Vector3 dir = { 0.3f, -1.0f, 0.2f };
    float dirIntensity = 1.5f;
    Vector4 dirColor = { 1,1,1,1 };

    Vector3 pointPos = { 0,15,10 };
    float pointIntensity = 2.0f;
    Vector4 pointColor = { 1,1,1,1 };
    float pointRadius = 80.0f;
    float pointDecay = 1.5f;

    Vector3 spotPos = { 0,20,-10 };
    Vector3 spotDir = { 0,-1,0.3f };
    float spotIntensity = 0.0f; // まずOFF推奨
    float spotDistance = 100.0f;
    float spotDecay = 2.0f;
    float spotAngleDeg = 35.0f;
    float spotFalloffStartDeg = 25.0f;
    Vector3 spotColor = { 1,1,1 };
};
