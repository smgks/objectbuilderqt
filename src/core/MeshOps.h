#pragma once

#include "ObjectData.h"

#include <QMatrix4x4>

// Geometry/topology operations behind the Structure, Points, Faces and
// Surfaces menus. All work on the current selection of one LOD and keep the
// ObjectData invariants. They are free functions so commands, scripts and
// tests can share them.
namespace MeshOps {

// --- deletion ------------------------------------------------------------
void deleteSelection(ObjectData &o);      // Edit > Delete: selected points and faces
void removeSelectedFaces(ObjectData &o);  // Faces > Remove: faces only

// --- faces ---------------------------------------------------------------
void reverseSelectedFaces(ObjectData &o);
void uncrossSelectedFaces(ObjectData &o);
void triangulateSelected(ObjectData &o, bool alongFirstDiagonal);
int squarizeSelected(ObjectData &o); // returns quads created
void moveSelectedFaces(ObjectData &o, bool toTop);
QVector<int> degeneratedFaces(const ObjectData &o);
int repairDegenerated(ObjectData &o);

// --- points --------------------------------------------------------------
void transformSelected(ObjectData &o, const QMatrix4x4 &m, bool allAnimations = false);
void mergeSelectedPoints(ObjectData &o);
int mergeNear(ObjectData &o, float tolerance); // returns points removed
QVector<int> isolatedPoints(const ObjectData &o);

// --- creation ------------------------------------------------------------
bool createFaceFromSelection(ObjectData &o, const QVector3D &viewerDir);
void extrudeSelectedFaces(ObjectData &o);

// --- selection -----------------------------------------------------------
void growSelection(ObjectData &o);
void shrinkSelection(ObjectData &o);
void invertSelection(ObjectData &o);

// --- analysis ------------------------------------------------------------
int findComponents(ObjectData &o); // creates "Component01".. named selections
int selectNonClosed(ObjectData &o); // selects points on open edges

// --- surfaces ------------------------------------------------------------
void setSharpEdges(ObjectData &o, bool sharp);

} // namespace MeshOps
