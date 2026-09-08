// Analytic strokes preserve curved edges at the actual render resolution.
// No pixel font, polygonal ring, external texture, or global bloom dependency.
static const float PI = 3.14159265359;
float segmentDistance(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    return length(p - a - ab * saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-5)));
}
float arcDistance(float2 p, float radius, float fraction) {
    if (fraction >= .9999) return abs(length(p) - radius);
    float sweep = saturate(fraction) * 2 * PI;
    float angle = atan2(p.x, -p.y);
    if (angle < 0) angle += 2 * PI;
    float2 start = float2(0, -radius);
    float2 end = float2(sin(sweep), -cos(sweep)) * radius;
    return angle <= sweep ? abs(length(p) - radius) : min(length(p - start), length(p - end));
}
float4 main(float4 position : SV_POSITION, float2 local : TEXCOORD0,
    nointerpolation float4 color : COLOR0, nointerpolation float4 shape : TEXCOORD1) : SV_TARGET {
    float distance;
    bool emphasized = shape.x >= 1.5;
    bool chevron = (shape.x > .5 && shape.x < 1.5) || shape.x > 2.5;
    if (!chevron) {
        distance = arcDistance(local, shape.y, shape.w);
    } else {
        float2 tip = float2(shape.y * .48, 0);
        distance = min(segmentDistance(local, float2(-shape.y * .40, -shape.y * .72), tip),
            segmentDistance(local, tip, float2(-shape.y * .40, shape.y * .72)));
    }
    // Screen derivatives follow viewport scale without changing with quad rotation.
    float aa = max(.45, max(length(ddx(local)), length(ddy(local))) * .85);
    float edge = distance - shape.z * .5;
    float coverage = 1 - smoothstep(-aa, aa, edge);
    float outside = max(edge, 0);
    float halo = exp(-outside * outside / (emphasized ? 13.0 : 7.0)) * (emphasized ? .38 : .17);
    if (emphasized) halo += exp(-outside * outside / 40.0) * .065;
    // A very narrow dark under-stroke retains contrast on bright sand/sky.
    float underAlpha = (1 - smoothstep(.35, emphasized ? 2.8 : 2.1, edge)) * (emphasized ? .70 : .46) * color.a;
    float lightAlpha = max(coverage, halo) * color.a;
    float alpha = lightAlpha + underAlpha * (1 - lightAlpha);
    float3 tint = lerp(color.rgb, float3(.90, .97, 1), coverage * (emphasized ? .06 : .18));
    // Compose light over its subtle backing, then return straight alpha for
    // the existing blend state. Sand remains readable without a solid panel.
    float3 rgb = (tint * lightAlpha + float3(.015, .03, .045) * underAlpha * (1 - lightAlpha)) / max(alpha, 1e-4);
    return float4(rgb, alpha);
}
