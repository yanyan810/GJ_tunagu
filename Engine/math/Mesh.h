// Mesh.h
#pragma once
#include <vector>
#include <string>
#include <cstdint>

#include "Matrix4x4.h"

struct Vertex
{
    Vector3 position;
    Vector3 normal;
    Vector2 uv;
    Vector3 tangent;
    // 必要なら VertexColor とか足してもOK
};

struct Mesh
{
    std::vector<Vertex>   vertices;   // 頂点配列
    std::vector<uint32_t> indices;    // インデックス配列
    std::wstring          diffuseMap; // テクスチャファイルパス
};
