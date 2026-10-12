#include "LodObject.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

LodObject::LodObject()
{
    lods.append(Lod{1.0f, ObjectData()});
}

int LodObject::find(float resolution) const
{
    for (int i = 0; i < lods.size(); ++i)
        if (qFuzzyCompare(lods[i].resolution, resolution))
            return i;
    return -1;
}

int LodObject::addLod(float resolution, const ObjectData &data)
{
    if (int i = find(resolution); i >= 0)
        return i;
    const float activeRes = lods.isEmpty() ? resolution : activeResolution();
    lods.append(Lod{resolution, data});
    sort();
    active = find(activeRes);
    return find(resolution);
}

void LodObject::removeLod(int index)
{
    if (lods.size() <= 1 || index < 0 || index >= lods.size())
        return;
    lods.removeAt(index);
    active = std::clamp(active > index ? active - 1 : active, 0, int(lods.size()) - 1);
}

bool LodObject::setResolution(int index, float resolution)
{
    const int other = find(resolution);
    if (other >= 0 && other != index)
        return false;
    const float activeRes = activeResolution();
    const bool wasActive = index == active;
    lods[index].resolution = resolution;
    sort();
    active = wasActive ? find(resolution) : find(activeRes);
    return true;
}

void LodObject::sort()
{
    std::stable_sort(lods.begin(), lods.end(), [](const Lod &a, const Lod &b) { return a.resolution < b.resolution; });
}

// ---------------------------------------------------------------- LodNames

namespace {
struct Special {
    float value;
    const char *name;
};

// Names are the literal strings found in ObjectBuilder.exe .rdata
// (0x6f9b8c..0x6f9e1c); the 1e15-step index assignment follows the switch in
// the name function at 0x620d70.
const Special kSpecial[] = {
    {1e13f, "Geometry"},
    {2e13f, "Geometry Buoyancy"},
    {3e13f, "Geometry Phys Old"},
    {4e13f, "Geometry Phys"},
    {1e15f, "Memory"},
    {2e15f, "LandContact"},
    {3e15f, "RoadWay"},
    {4e15f, "Paths"},
    {5e15f, "Hit-points"},
    {6e15f, "View Geometry"},
    {7e15f, "Fire Geometry"},
    {9e15f, "View - Cargo - Fire Geometry"},
    {1e16f, "View - Commander"},
    {1.1e16f, "View - Commander -Geometry"},
    {1.2e16f, "View - Commander -Fire Geometry"},
    {1.3e16f, "View - Pilot - Geometry"},
    {1.4e16f, "View - Pilot - Fire Geometry"},
    {1.5e16f, "View - Gunner - Geometry"},
    {1.6e16f, "View - Gunner - Fire Geometry"},
    {1.7e16f, "Sub Parts"},
    {1.9e16f, "ShadowVolume - View - Pilot"},
    {2e16f, "ShadowVolume - View - Gunner"},
    {2.1e16f, "Wreck"},
};

// Ranges that carry an index ("%s %.3f").
struct Indexed {
    double from, to, base, scale;
    const char *name;
};
const Indexed kIndexed[] = {
    {1200.0, 1300.0, 1200.0, 1.0, "View - Cargo"},
    {10000.0, 20000.0, 10000.0, 1.0, "ShadowVolume"},
    {20000.0, 30000.0, 20000.0, 1.0, "Edit"},
    {8e15, 9e15, 8e15, 1e15, "View - Cargo - Geometry"},
    {1.8e16, 1.9e16, 1.8e16, 1e15, "ShadowVolume - View - Cargo"},
};

bool near(double a, double b)
{
    return std::fabs(a - b) <= std::fabs(b) * 1e-3;
}
} // namespace

QString LodNames::name(float resolution)
{
    const double r = resolution;
    if (near(r, 1000.0))
        return QStringLiteral("View - Gunner");
    if (near(r, 1100.0))
        return QStringLiteral("View - Pilot");
    for (const auto &s : kSpecial)
        if (near(r, s.value))
            return QString::fromLatin1(s.name);
    for (const auto &ix : kIndexed)
        if (r >= ix.from && r < ix.to)
            return QStringLiteral("%1 %2").arg(QString::fromLatin1(ix.name)).arg((r - ix.base) / ix.scale, 0, 'f', 3);
    if (r > 5e14)
        return QStringLiteral("%1 Spec").arg(r, 0, 'g');
    return QString::number(r, 'f', 3);
}

bool LodNames::parse(const QString &text, float &resolution)
{
    const QString t = text.trimmed();
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (ok) {
        resolution = float(v);
        return true;
    }
    if (t.compare(QLatin1String("View - Gunner"), Qt::CaseInsensitive) == 0) {
        resolution = 1000.0f;
        return true;
    }
    if (t.compare(QLatin1String("View - Pilot"), Qt::CaseInsensitive) == 0) {
        resolution = 1100.0f;
        return true;
    }
    for (const auto &s : kSpecial)
        if (t.compare(QLatin1String(s.name), Qt::CaseInsensitive) == 0) {
            resolution = s.value;
            return true;
        }
    // Longest prefix first so "ShadowVolume - View - Cargo" wins over "ShadowVolume".
    const Indexed *best = nullptr;
    for (const auto &ix : kIndexed)
        if (t.startsWith(QLatin1String(ix.name), Qt::CaseInsensitive) && (!best || qstrlen(ix.name) > qstrlen(best->name)))
            best = &ix;
    if (best) {
        const QString rest = t.mid(int(qstrlen(best->name))).trimmed();
        const double idx = rest.isEmpty() ? 0.0 : rest.toDouble(&ok);
        if (rest.isEmpty() || ok) {
            resolution = float(best->base + idx * best->scale);
            return true;
        }
    }
    return false;
}

QVector<QPair<QString, float>> LodNames::specialLods()
{
    QVector<QPair<QString, float>> out;
    out.append({QStringLiteral("View - Gunner"), 1000.0f});
    out.append({QStringLiteral("View - Pilot"), 1100.0f});
    out.append({QStringLiteral("View - Cargo"), 1200.0f});
    out.append({QStringLiteral("ShadowVolume 0"), 10000.0f});
    out.append({QStringLiteral("Edit 0"), 20000.0f});
    for (const auto &s : kSpecial)
        out.append({QString::fromLatin1(s.name), s.value});
    out.append({QStringLiteral("View - Cargo - Geometry 0"), 8e15f});
    out.append({QStringLiteral("ShadowVolume - View - Cargo 0"), 1.8e16f});
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.second < b.second; });
    return out;
}

bool LodNames::hasMass(float resolution)
{
    const double r = std::fabs(double(resolution));
    return r == 1e13f || r == 4e13f || r == 3e13f;
}
