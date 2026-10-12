#pragma once

#include "ObjectData.h"

#include <QMatrix4x4>

// Primitive generators for the Create menu. Mirrors the original CICreatePrim
// command (0x4fe5a0): geometry is built in a unit local frame, scaled by
// size/2, offset by the Align option and placed with `placement` (pin position
// and the orientation of the view it was created from). The new points and
// faces become the current selection.
namespace Primitives {

enum class Align { CenterToPin, Bottom, Top };

struct Params {
    float sizeX = 1, sizeY = 1, sizeZ = 1;
    int segX = 1, segY = 1, segZ = 1;
    float radius = 0.5f;
    int segRadius = 16;
    bool centerPoint = false; // circle/cylinder caps use a centre vertex (triangle fan)
    Align align = Align::CenterToPin;
};

void plane(ObjectData &o, const Params &p, const QMatrix4x4 &placement);
void box(ObjectData &o, const Params &p, const QMatrix4x4 &placement);
void circle(ObjectData &o, const Params &p, const QMatrix4x4 &placement);
void cylinder(ObjectData &o, const Params &p, const QMatrix4x4 &placement);
void sphere(ObjectData &o, const Params &p, const QMatrix4x4 &placement);

} // namespace Primitives
