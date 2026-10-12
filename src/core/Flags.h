#pragma once

#include <QtGlobal>

// Face flag bits as used by the Face Properties dialog of the original
// (CFaceProp, dialog 178). Verified from the dialog -> flags conversion at
// 0x51cf80 (REA evidence ev_a8361e78...).
namespace FaceFlags {
constexpr quint32 LightingMask = 0x003000A7;
constexpr quint32 LightNormal = 0x00000000;
constexpr quint32 LightBothSides = 0x00000020;
constexpr quint32 LightPosition = 0x00000080;
constexpr quint32 LightFlat = 0x00100000;
constexpr quint32 LightReversed = 0x00200000;

constexpr quint32 ZBiasMask = 0x00000300;
constexpr quint32 ZBiasLow = 0x00000100;
constexpr quint32 ZBiasMiddle = 0x00000200;
constexpr quint32 ZBiasHigh = 0x00000300;

constexpr quint32 NoShadow = 0x00000010;
constexpr quint32 NoTextureMerge = 0x01000000;

constexpr quint32 UserMask = 0xFE000000;
constexpr int UserShift = 25;
} // namespace FaceFlags

// Point flag bits shown in the Point Properties dialog (dialog 175).
// NOTE: the dialog layout was recovered from the binary, but these bit values
// come from the publicly documented MLOD format and were not traced in the
// dialog code. See docs/RE_NOTES.md.
namespace PointFlags {
constexpr quint32 SurfaceMask = 0x0000000F;
constexpr quint32 OnSurface = 0x00000001;
constexpr quint32 UnderSurface = 0x00000002;
constexpr quint32 AboveSurface = 0x00000004;
constexpr quint32 KeepHeight = 0x00000008;

constexpr quint32 LightMask = 0x000000F0;
constexpr quint32 LightShining = 0x00000010;
constexpr quint32 LightInShadow = 0x00000020;
constexpr quint32 LightFull = 0x00000040;
constexpr quint32 LightHalf = 0x00000080;

constexpr quint32 DecalMask = 0x00000300;
constexpr quint32 Decal = 0x00000100;

constexpr quint32 FogMask = 0x00003000;
constexpr quint32 FogNone = 0x00001000;
constexpr quint32 FogSky = 0x00002000;

constexpr quint32 UserMask = 0x00FF0000;
constexpr int UserShift = 16;

constexpr quint32 Hidden = 0x01000000;
} // namespace PointFlags
