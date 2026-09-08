#pragma once
#include "Vector3.h"
#include "SwimFeedbackSettings.h"

class Camera;
class RenderManager;

// Measures actual displacement, not requested speed or camera rotation. The
// CPU state is separate so discontinuities/pause/stop can be tested directly.
struct SwimFeedbackMotion {
    Vector3 previous{}, velocity{};
    float speed=0, amount=0, time=0;
    bool sampled=false;
    void Reset();
    void Update(float dt,const Vector3& position,const SwimFeedbackSettings& settings,bool valid=true);
};

class SwimFeedback final {
public:
    void Initialize(RenderManager* render);
    void Shutdown();
    void Reset();
    void Update(float dt,const Vector3& position,const Camera* camera,float waterLevelY,bool validSnapshot=true);
private:
    RenderManager* render_=nullptr;
    SwimFeedbackMotion motion_{};
};
