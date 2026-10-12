#pragma once

#include "ObjectData.h"

#include <QString>
#include <QVector>

struct Lod {
    float resolution = 0.0f;
    ObjectData data;
};

// The whole model: a list of LODs kept sorted by resolution, plus the index of
// the LOD being edited (LodData in the original).
class LodObject {
public:
    QVector<Lod> lods;
    int active = 0;

    LodObject();

    ObjectData &activeData() { return lods[active].data; }
    const ObjectData &activeData() const { return lods[active].data; }
    float activeResolution() const { return lods[active].resolution; }

    int find(float resolution) const;
    int addLod(float resolution, const ObjectData &data = ObjectData()); // returns index
    void removeLod(int index);
    bool setResolution(int index, float resolution); // false if taken
    void sort();
};

namespace LodNames {
// Special resolutions (see docs/RE_NOTES.md, name function at 0x620d70).
constexpr float ViewGunner = 1000.0f;
constexpr float ViewPilot = 1100.0f;
constexpr float ViewCargo = 1200.0f;
constexpr float ShadowVolume = 10000.0f;
constexpr float Edit = 20000.0f;
constexpr float Geometry = 1e13f;
constexpr float GeometryBuoyancy = 2e13f;
constexpr float GeometryPhysOld = 3e13f;
constexpr float GeometryPhys = 4e13f;
constexpr float Memory = 1e15f;
constexpr float LandContact = 2e15f;
constexpr float Roadway = 3e15f;
constexpr float Paths = 4e15f;
constexpr float HitPoints = 5e15f;
constexpr float ViewGeometry = 6e15f;
constexpr float FireGeometry = 7e15f;

QString name(float resolution);
// Parses either a number or one of the names produced by name().
bool parse(const QString &text, float &resolution);
// All named special LODs, for pickers.
QVector<QPair<QString, float>> specialLods();
// LODs whose mass is saved (#Mass# tag), per the MLOD writer at 0x5efcf0.
bool hasMass(float resolution);
} // namespace LodNames
