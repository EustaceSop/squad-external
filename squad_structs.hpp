#pragma once
#include <cstdint>
#include <cmath>

// ============================================================
// Squad ESP - Core Structs (UE5 double precision)
// ============================================================

struct FVector
{
    double x, y, z;

    FVector() : x(0), y(0), z(0) {}
    FVector(double x, double y, double z) : x(x), y(y), z(z) {}
    FVector(double s) : x(s), y(s), z(s) {}

    FVector operator+(const FVector& o) const { return { x + o.x, y + o.y, z + o.z }; }
    FVector operator-(const FVector& o) const { return { x - o.x, y - o.y, z - o.z }; }
    FVector operator*(double s) const { return { x * s, y * s, z * s }; }
    FVector operator/(double s) const { double r = 1.0 / s; return { x * r, y * r, z * r }; }
    FVector& operator+=(const FVector& o) { x += o.x; y += o.y; z += o.z; return *this; }
    FVector& operator-=(const FVector& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    double length() const { return sqrt(x * x + y * y + z * z); }
    double length_2d() const { return sqrt(x * x + y * y); }
    double dot(const FVector& o) const { return x * o.x + y * o.y + z * o.z; }
    bool is_zero() const { return x == 0.0 && y == 0.0 && z == 0.0; }

    // Distance in meters (UE units -> meters, 1 UE unit = 1cm)
    double distance_m(const FVector& o) const {
        return (*this - o).length() * 0.01;
    }
};

struct FVector2D
{
    double x, y;

    FVector2D() : x(0), y(0) {}
    FVector2D(double x, double y) : x(x), y(y) {}

    FVector2D operator+(const FVector2D& o) const { return { x + o.x, y + o.y }; }
    FVector2D operator-(const FVector2D& o) const { return { x - o.x, y - o.y }; }
    bool is_valid() const { return x != 0.0 || y != 0.0; }
};

struct FRotator
{
    double pitch, yaw, roll;

    FRotator() : pitch(0), yaw(0), roll(0) {}
    FRotator(double p, double y, double r) : pitch(p), yaw(y), roll(r) {}
};

struct FQuat
{
    double x, y, z, w;
};

struct FTransform
{
    FQuat   rotation;    // 0x00 (32 bytes)
    FVector translation; // 0x20 (24 bytes)
    char    pad0[8];     // alignment
    FVector scale3d;     // 0x40 (24 bytes)
    char    pad1[8];     // alignment -> total 0x60
};
static_assert(sizeof(FTransform) == 0x60, "FTransform size mismatch");

struct FMatrix
{
    double m[4][4];
};

struct FLinearColor
{
    float r, g, b, a;
    FLinearColor() : r(0), g(0), b(0), a(0) {}
    FLinearColor(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}
};

// TArray layout (matches UE5 internal)
template<typename T>
struct TArray
{
    T*      data;
    int32_t count;
    int32_t max;

    TArray() : data(nullptr), count(0), max(0) {}
    bool is_valid() const { return data != nullptr && count > 0; }
    bool valid_index(int32_t i) const { return i >= 0 && i < count; }
};

// FString = TArray<wchar_t>
struct FString : TArray<wchar_t>
{
    const wchar_t* c_str() const { return data; }
};

struct FWeakObjectPtr
{
    int32_t ObjectIndex;
    int32_t ObjectSerialNumber;
};
static_assert(sizeof(FWeakObjectPtr) == 0x8, "FWeakObjectPtr size mismatch");

template<typename T>
using TWeakObjectPtr = FWeakObjectPtr;

// ============================================================
// Bone math helpers
// ============================================================

// Transform a bone-space position to world-space using ComponentToWorld
inline FVector transform_bone_to_world(const FTransform& comp_to_world, const FTransform& bone)
{
    // Simplified: just use bone translation + component translation
    // Full version would apply rotation, but for position-only this is:
    // world_pos = comp_rotation.rotate(bone_translation * comp_scale) + comp_translation

    const FQuat& q = comp_to_world.rotation;
    const FVector& t = comp_to_world.translation;
    const FVector& s = comp_to_world.scale3d;

    // Scale the bone translation
    FVector scaled = { bone.translation.x * s.x, bone.translation.y * s.y, bone.translation.z * s.z };

    // Rotate by quaternion
    // v' = q * v * q^-1
    double qx = q.x, qy = q.y, qz = q.z, qw = q.w;

    double ix = qw * scaled.x + qy * scaled.z - qz * scaled.y;
    double iy = qw * scaled.y + qz * scaled.x - qx * scaled.z;
    double iz = qw * scaled.z + qx * scaled.y - qy * scaled.x;
    double iw = -qx * scaled.x - qy * scaled.y - qz * scaled.z;

    FVector rotated;
    rotated.x = ix * qw + iw * -qx + iy * -qz - iz * -qy;
    rotated.y = iy * qw + iw * -qy + iz * -qx - ix * -qz;
    rotated.z = iz * qw + iw * -qz + ix * -qy - iy * -qx;

    return { rotated.x + t.x, rotated.y + t.y, rotated.z + t.z };
}
