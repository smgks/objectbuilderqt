#pragma once

#include "LodObject.h"

#include <QByteArray>
#include <QStringList>

// Binarized model (ODOL) reader -> editable MLOD data.
// The original Object Builder cannot do this (it loads a placeholder box for
// ODOL files); the format was reconstructed from the engine's loader in DayZ
// Tools' binarize.exe. See docs/RE_NOTES.md.
namespace Odol {

struct Info {
    int version = 0;
    QStringList warnings;
    QStringList bones; // skeleton bone names (empty when the model has no skeleton)
    QStringList sections; // per LOD section: "<resolution> <texture> special=<hex>" (diagnostics)
};

bool isOdol(const QByteArray &data);
bool load(const QByteArray &data, LodObject &out, QString *error = nullptr, Info *info = nullptr);

} // namespace Odol
