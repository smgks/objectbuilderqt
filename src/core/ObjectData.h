#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include <array>
#include <functional>

// One editable LOD of a model, mirroring the in-memory object of the original
// (ObjectData in Objektiv2). Layout of the persistent parts follows the MLOD
// P3DM + TAGG format; see docs/RE_NOTES.md.
//
// Invariant: every per-point / per-face array (selections, UV sets, mass,
// animation phases) is either empty or sized to points.size() / faces.size().
// Use the structural helpers (addPoint, addFace, deletePoints, deleteFaces)
// rather than resizing `points` / `faces` directly so the invariant holds.

struct Point {
    QVector3D pos;
    quint32 flags = 0;
};

struct FaceVertex {
    int point = 0;
    int normal = 0;
};

struct Face {
    int n = 3; // 3 or 4
    std::array<FaceVertex, 4> v{};
    quint32 flags = 0;
    QString texture;
    QString material;
};

// Per-point weights and per-face flags. Point weight 0 = not selected,
// 255 = fully selected (on disk the original stores the negated byte).
struct Selection {
    QVector<quint8> points;
    QVector<quint8> faces;

    bool point(int i) const { return i < points.size() && points[i] != 0; }
    quint8 weight(int i) const { return i < points.size() ? points[i] : 0; }
    bool face(int i) const { return i < faces.size() && faces[i] != 0; }
    void setPoint(int i, quint8 w);
    void setFace(int i, bool on);
    void resize(int nPoints, int nFaces);
    void clear();
    bool isEmpty() const;
    int pointCount() const;
    int faceCount() const;
};

struct NamedSelection {
    QString name;
    Selection sel;
};

struct NamedProperty {
    QString name;
    QString value;
};

struct UVSet {
    quint32 id = 0;
    QVector<std::array<QVector2D, 4>> uv; // per face, per face-vertex
};

struct AnimationPhase {
    float time = 0.0f;
    QVector<QVector3D> points;
};

// A TAGG entry the editor does not interpret; preserved for round-trip.
struct RawTag {
    QString name;
    QByteArray data;
};

class ObjectData {
public:
    QVector<Point> points;
    QVector<QVector3D> normals;
    QVector<Face> faces;

    QVector<NamedSelection> namedSelections;
    QVector<NamedProperty> namedProperties;
    QVector<QPair<int, int>> sharpEdges;
    QVector<float> mass;          // empty when the LOD has no mass
    QVector<UVSet> uvSets;        // always contains at least the set with id 0
    int activeUVSet = 0;          // index into uvSets
    QVector<AnimationPhase> animations;
    QVector<RawTag> unknownTags;

    Selection selection;
    Selection hidden;
    Selection locked;

    ObjectData();

    // --- structure -------------------------------------------------------
    int addPoint(const QVector3D &pos, quint32 flags = 0);
    int addNormal(const QVector3D &n);
    // UVs are written into every UV set (callers usually edit set 0 later).
    int addFace(const Face &f, const std::array<QVector2D, 4> &uv = {});
    void deletePoints(const QVector<bool> &remove); // also removes faces using them
    void deleteFaces(const QVector<bool> &remove);
    void clear();
    void normalizeArrays(); // re-establish the size invariant
    // Appends another LOD's geometry (File > Merge). Named selections with the
    // same name are united; if `asSelection` is set, the merged part also gets
    // a named selection of that name. The merged part becomes the selection.
    void merge(const ObjectData &other, const QString &asSelection = QString());

    // --- UV --------------------------------------------------------------
    UVSet &uvSet0();
    const UVSet &uvSet0() const;
    UVSet *findUVSet(quint32 id);
    const UVSet *findUVSet(quint32 id) const { return const_cast<ObjectData *>(this)->findUVSet(id); }
    QVector2D uv(int face, int vert) const { return uvSet0().uv[face][vert]; }

    // --- named data ------------------------------------------------------
    NamedSelection *findSelection(const QString &name);
    const NamedSelection *findSelection(const QString &name) const;
    QString property(const QString &name) const;
    void setProperty(const QString &name, const QString &value);
    void removeProperty(const QString &name);

    // --- queries ---------------------------------------------------------
    bool pointVisible(int i) const { return !hidden.point(i); }
    bool faceVisible(int i) const;
    bool hasMass() const { return !mass.isEmpty(); }
    float totalMass() const;
    // Front-side (outward) unit normal of a face.
    QVector3D faceNormal(int f) const;
    // The flat vertex normal P3D files store for a face: it points to the back
    // side (-faceNormal), as the original's Recalculate Normals writes it.
    QVector3D storedNormal(int f) const;
    QVector3D faceCenter(int f) const;
    void bounds(QVector3D &mn, QVector3D &mx, bool selectedOnly = false) const;
    QVector3D selectionCenter() const;

    // --- selection helpers ----------------------------------------------
    void selectFacesFromPoints(); // faces whose points are all selected
    void selectPointsFromFaces(); // points used by selected faces
    void forEachSelectedPoint(const std::function<void(int)> &fn) const;

    // --- geometry ops used by several commands ---------------------------
    void recalculateNormals();
};
