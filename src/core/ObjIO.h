#pragma once

#include "ObjectData.h"

#include <QString>

// Wavefront OBJ import/export (File > Import > OBJ File...). OBJ is
// right-handed, the model space is left-handed: Z is negated both ways, which
// keeps the visible winding (front faces stay front faces).
namespace ObjIO {
// Appends the OBJ contents to `o`; OBJ groups/objects become named selections.
bool import(const QString &path, ObjectData &o, QString *error = nullptr);
bool exportLod(const QString &path, const ObjectData &o, QString *error = nullptr);
} // namespace ObjIO
