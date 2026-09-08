#pragma once

#include "Vector3.h"
#include <memory>

class Camera;
class DirectXCommon;
class Object3dCommon;

// Two opaque draws, sharing three prepared stone/mineral model pairs. No
// postprocess pass, model creation during flight, or collision decisions.
class ShockwaveRockVisual final {
public:
    ShockwaveRockVisual();
    ~ShockwaveRockVisual();
    void Initialize(Object3dCommon* objects, DirectXCommon* dx, Camera* camera);
    void Update(const Vector3& position,const Vector3& rotation,const Vector3& scale,float dt);
    void Draw();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
