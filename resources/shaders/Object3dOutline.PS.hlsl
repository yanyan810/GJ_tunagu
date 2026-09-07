#include "Object3dOutline.hlsli"

float4 main(OutlineVertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET0
{
    if (gOutlineParam.enable == 0.0f) { discard; }
    if (gOutlineParam.thickness < 0.0f)
    {
        // The opt-in PSO disables fixed culling. A negative node determinant
        // reverses raster winding, but must not turn the whole front into a rim.
        bool logicalFront = input.worldOrientation < 0.0f ? !isFrontFace : isFrontFace;
        if (logicalFront) { discard; }
    }
    return gOutlineParam.color;
}
