#pragma once

#include "LodObject.h"

#include <QString>
#include <QStringList>

// FBX import (binary 7.x and ASCII), without the Autodesk SDK.
//
// Conventions follow the original FBX.dll plugin (LodGroup, SelectionSet,
// units/axis conversion options):
//   - Units are converted to metres using GlobalSettings.UnitScaleFactor.
//   - The scene's up/front axes are converted to Y-up, then Z is negated
//     (FBX is right-handed, the model space is left-handed).
//   - Each mesh node becomes a named selection (node name).
//   - LOD assignment, in order of precedence:
//       1. children of a LodGroup node -> one LOD each (resolution parsed from
//          the child's name, else 1, 2, 3...)
//       2. top-level nodes whose name is a LOD name ("Geometry", "Memory",
//          "Fire Geometry", "1.000", "LOD 2", "ShadowVolume 0", ...) -> that LOD,
//          including all their descendants
//       3. everything else -> `defaultResolution`
//   - The material's diffuse / base colour texture -> face texture (normal,
//     specular... maps are ignored); a material whose name ends in ".rvmat"
//     -> face material.
//   - Texture files are looked up at the stored absolute path, the relative
//     path (from the FBX folder), and by file name in the FBX folder, its
//     "textures" folder and "<name>.fbm". Textures embedded in the FBX are
//     written to "<fbx folder>/<name>.fbm/" when no file is found (the FBX
//     SDK convention). Found files are stored relative to the work drive when
//     they are on it, otherwise as absolute paths.
//   - Smoothing "ByEdge" data -> sharp edges.
namespace FbxIO {

struct Options {
    float defaultResolution = 1.0f;
    float scale = 1.0f;              // extra scale on top of the unit conversion
    bool convertUnits = true;
    bool convertAxes = true;
    QString texturePrefixStrip;      // e.g. "P:\" removed from texture paths
    QString workDrive;               // e.g. "P:/": textures found below it become relative
    bool extractEmbeddedTextures = true;
};

struct Report {
    int meshes = 0, faces = 0, lods = 0;
    int texturesFound = 0;           // texture files located on disk
    QStringList texturesExtracted;   // embedded textures written out
    QStringList texturesMissing;     // referenced but not found (path kept as written)
    QStringList warnings;
};

bool import(const QString &path, LodObject &into, const Options &opt = {}, Report *report = nullptr, QString *error = nullptr);

} // namespace FbxIO
