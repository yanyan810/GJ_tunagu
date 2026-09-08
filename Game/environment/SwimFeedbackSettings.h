#pragma once

// Exposed in the existing F9 validated catalog under the presentation.swim
// keys. Movement physics and camera transforms never depend on these values.
struct SwimFeedbackSettings {
    bool enabled=true, blurEnabled=true, streaksEnabled=true;
    float startSpeed=8.0f, fullSpeed=24.0f;
    float maxBlur=.018f, streakOpacity=.065f;
    float clearRadius=.42f, response=.25f;
};
