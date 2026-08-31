#pragma once

struct Vector2 {
    float x;
    float y;
};

struct Vector3 {
    float x;
    float y;
    float z;

    // ---- 以下を追加 ----
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}


    /*Vector3 Normalize(const Vector3& v) {
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length == 0.0f) return { 0.0f, 0.0f, 0.0f };
        return { v.x / length, v.y / length, v.z / length };
    }*/


    // 加算
    Vector3 operator+(const Vector3& other) const {
        return { x + other.x, y + other.y, z + other.z };
    }

    // 減算
    Vector3 operator-(const Vector3& other) const {
        return { x - other.x, y - other.y, z - other.z };
    }

    // スカラー倍
    Vector3 operator*(float scalar) const {
        return { x * scalar, y * scalar, z * scalar };
    }

    // 複合代入
    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }
    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }
};

struct Transform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};


struct Vector4 {
    float x;
    float y;
    float z;
    float w;
};
