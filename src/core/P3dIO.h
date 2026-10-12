#pragma once

#include "LodObject.h"

#include <QString>

class QIODevice;

// MLOD (editable) P3D reader/writer, reconstructed from ObjectBuilder.exe:
//   file reader   0x5f0070   (MLOD / NLOD, ODOL rejected)
//   LOD reader    0x5e6600   (P3DM / SP3X)
//   tag reader    0x5e7110   (TAGG)
//   file writer   0x5efcf0
//   LOD writer    0x5e5960   (P3DM v0x100 or SP3X v0x99)
//   tag writer    0x5e6250
// Byte layout is documented in docs/RE_NOTES.md.
namespace P3dIO {

struct SaveOptions {
    // "SP3X" LODs with 64-byte tag names, as written by File > Export >
    // P3D Old Version. Default is the current P3DM format.
    bool oldFormat = false;
    // Skip selections/properties whose name starts with '.' or '-' and the
    // #Selected#/#Lock#/#Hide# tags (the original's param_2 of 0x5e6250).
    bool stripEditorData = false;
};

bool load(const QString &path, LodObject &out, QString *error = nullptr);
bool load(QIODevice &dev, LodObject &out, QString *error = nullptr);
bool save(const QString &path, const LodObject &obj, const SaveOptions &opt = {}, QString *error = nullptr);
bool save(QIODevice &dev, const LodObject &obj, const SaveOptions &opt = {}, QString *error = nullptr);

// Single-LOD serialisation (P3DM + TAGG, no resolution), used for clipboard
// copy/paste like the original (0x5f7240 / 0x5f7450).
QByteArray serializeLod(const ObjectData &obj);
bool deserializeLod(const QByteArray &data, ObjectData &out, QString *error = nullptr);

} // namespace P3dIO
