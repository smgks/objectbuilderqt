#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// PAA/PAC texture decoding (DXT1-5, ARGB4444/1555/8888, AI88) and the
// engine's procedural texture syntax "#(argb,8,8,3)color(r,g,b,a,TAG)".
namespace Paa {

// Decodes the largest mipmap whose width and height are <= maxSize
// (the smallest available one if none fits). Returns a null image on failure.
QImage decode(const QByteArray &data, int maxSize = 1024, QString *error = nullptr);

// "#(argb,8,8,3)color(0.5,0.5,0.5,1,co)" -> 8x8 image. Null if not procedural.
QImage procedural(const QString &spec);

} // namespace Paa
