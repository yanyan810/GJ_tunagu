#pragma once
struct Vector3 {
	float x;
	float y;
	float z;
};


struct Vector4 {
	float x; // X座標
	float y; // Y座標
	float z; // Z座標
	float w; // W座標 (用途に応じて異なる意味を持つ)
};

struct Vector2 {
	float x; // X座標
	float y; // Y座標
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};
