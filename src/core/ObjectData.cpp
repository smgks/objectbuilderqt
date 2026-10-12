#include "ObjectData.h"

#include <QHash>

#include <algorithm>
#include <limits>

// ---------------------------------------------------------------- Selection

void Selection::setPoint(int i, quint8 w)
{
    if (i >= points.size())
        points.resize(i + 1);
    points[i] = w;
}

void Selection::setFace(int i, bool on)
{
    if (i >= faces.size())
        faces.resize(i + 1);
    faces[i] = on ? 1 : 0;
}

void Selection::resize(int nPoints, int nFaces)
{
    points.resize(nPoints);
    faces.resize(nFaces);
}

void Selection::clear()
{
    points.fill(0);
    faces.fill(0);
}

bool Selection::isEmpty() const
{
    return pointCount() == 0 && faceCount() == 0;
}

int Selection::pointCount() const
{
    return int(std::count_if(points.begin(), points.end(), [](quint8 w) { return w != 0; }));
}

int Selection::faceCount() const
{
    return int(std::count_if(faces.begin(), faces.end(), [](quint8 w) { return w != 0; }));
}

// ---------------------------------------------------------------- ObjectData

ObjectData::ObjectData()
{
    uvSets.append(UVSet{});
}

int ObjectData::addPoint(const QVector3D &pos, quint32 flags)
{
    points.append(Point{pos, flags});
    const int n = points.size();
    selection.points.resize(n);
    hidden.points.resize(n);
    locked.points.resize(n);
    for (auto &ns : namedSelections)
        ns.sel.points.resize(n);
    if (!mass.isEmpty())
        mass.resize(n);
    for (auto &a : animations)
        a.points.append(pos);
    return n - 1;
}

int ObjectData::addNormal(const QVector3D &n)
{
    normals.append(n);
    return normals.size() - 1;
}

int ObjectData::addFace(const Face &f, const std::array<QVector2D, 4> &uv)
{
    faces.append(f);
    const int n = faces.size();
    selection.faces.resize(n);
    hidden.faces.resize(n);
    locked.faces.resize(n);
    for (auto &ns : namedSelections)
        ns.sel.faces.resize(n);
    for (auto &set : uvSets)
        set.uv.append(uv);
    return n - 1;
}

void ObjectData::deleteFaces(const QVector<bool> &remove)
{
    QVector<int> keep;
    keep.reserve(faces.size());
    for (int i = 0; i < faces.size(); ++i)
        if (i >= remove.size() || !remove[i])
            keep.append(i);
    if (keep.size() == faces.size())
        return;

    auto compact = [&keep](auto &vec) {
        using V = std::decay_t<decltype(vec)>;
        V out;
        out.reserve(keep.size());
        for (int i : keep)
            out.append(i < vec.size() ? vec[i] : typename V::value_type{});
        vec = out;
    };
    compact(faces);
    compact(selection.faces);
    compact(hidden.faces);
    compact(locked.faces);
    for (auto &ns : namedSelections)
        compact(ns.sel.faces);
    for (auto &set : uvSets)
        compact(set.uv);
}

void ObjectData::deletePoints(const QVector<bool> &remove)
{
    // Faces referencing removed points go too.
    QVector<bool> removeFace(faces.size(), false);
    for (int f = 0; f < faces.size(); ++f)
        for (int k = 0; k < faces[f].n; ++k) {
            const int p = faces[f].v[k].point;
            if (p < remove.size() && remove[p]) {
                removeFace[f] = true;
                break;
            }
        }
    deleteFaces(removeFace);

    QVector<int> remap(points.size(), -1);
    QVector<int> keep;
    for (int i = 0; i < points.size(); ++i)
        if (i >= remove.size() || !remove[i]) {
            remap[i] = keep.size();
            keep.append(i);
        }
    if (keep.size() == points.size())
        return;

    auto compact = [&keep](auto &vec) {
        if (vec.isEmpty())
            return;
        using V = std::decay_t<decltype(vec)>;
        V out;
        out.reserve(keep.size());
        for (int i : keep)
            out.append(i < vec.size() ? vec[i] : typename V::value_type{});
        vec = out;
    };
    compact(points);
    compact(selection.points);
    compact(hidden.points);
    compact(locked.points);
    for (auto &ns : namedSelections)
        compact(ns.sel.points);
    compact(mass);
    for (auto &a : animations)
        compact(a.points);

    for (auto &f : faces)
        for (int k = 0; k < f.n; ++k)
            f.v[k].point = remap[f.v[k].point];

    QVector<QPair<int, int>> edges;
    for (const auto &e : sharpEdges) {
        const int a = e.first < remap.size() ? remap[e.first] : -1;
        const int b = e.second < remap.size() ? remap[e.second] : -1;
        if (a >= 0 && b >= 0)
            edges.append({a, b});
    }
    sharpEdges = edges;
}

void ObjectData::clear()
{
    *this = ObjectData();
}

void ObjectData::normalizeArrays()
{
    const int np = points.size(), nf = faces.size();
    selection.resize(np, nf);
    hidden.resize(np, nf);
    locked.resize(np, nf);
    for (auto &ns : namedSelections)
        ns.sel.resize(np, nf);
    if (!mass.isEmpty())
        mass.resize(np);
    if (uvSets.isEmpty())
        uvSets.append(UVSet{});
    for (auto &set : uvSets)
        set.uv.resize(nf);
    for (auto &a : animations)
        a.points.resize(np);
    if (activeUVSet < 0 || activeUVSet >= uvSets.size())
        activeUVSet = 0;
}

void ObjectData::merge(const ObjectData &other, const QString &asSelection)
{
    normalizeArrays();
    const int p0 = points.size(), n0 = normals.size(), f0 = faces.size();
    if (other.hasMass() && !hasMass())
        mass = QVector<float>(p0, 0.0f);

    selection.clear();
    for (int i = 0; i < other.points.size(); ++i) {
        const int idx = addPoint(other.points[i].pos, other.points[i].flags);
        if (!mass.isEmpty())
            mass[idx] = i < other.mass.size() ? other.mass[i] : 0.0f;
        selection.setPoint(idx, 255);
        hidden.setPoint(idx, other.hidden.weight(i));
        locked.setPoint(idx, other.locked.weight(i));
    }
    normals += other.normals;
    for (int f = 0; f < other.faces.size(); ++f) {
        Face fc = other.faces[f];
        for (int k = 0; k < fc.n; ++k) {
            fc.v[k].point += p0;
            fc.v[k].normal += n0;
        }
        const int idx = addFace(fc);
        for (auto &set : uvSets) {
            const UVSet *src = other.findUVSet(set.id);
            set.uv[idx] = (src ? *src : other.uvSet0()).uv[f];
        }
        selection.setFace(idx, true);
        hidden.setFace(idx, other.hidden.face(f));
        locked.setFace(idx, other.locked.face(f));
    }
    for (const auto &ns : other.namedSelections) {
        NamedSelection *dst = findSelection(ns.name);
        if (!dst) {
            namedSelections.append(NamedSelection{ns.name, Selection{}});
            dst = &namedSelections.last();
        }
        dst->sel.resize(points.size(), faces.size());
        for (int i = 0; i < other.points.size(); ++i)
            if (ns.sel.point(i))
                dst->sel.points[p0 + i] = ns.sel.weight(i);
        for (int f = 0; f < other.faces.size(); ++f)
            if (ns.sel.face(f))
                dst->sel.faces[f0 + f] = 1;
    }
    for (const auto &p : other.namedProperties)
        if (property(p.name).isEmpty())
            namedProperties.append(p);
    for (const auto &e : other.sharpEdges)
        sharpEdges.append({e.first + p0, e.second + p0});
    if (!asSelection.isEmpty()) {
        NamedSelection *dst = findSelection(asSelection);
        if (!dst) {
            namedSelections.append(NamedSelection{asSelection, Selection{}});
            dst = &namedSelections.last();
        }
        dst->sel.resize(points.size(), faces.size());
        for (int i = p0; i < points.size(); ++i)
            dst->sel.points[i] = 255;
        for (int f = f0; f < faces.size(); ++f)
            dst->sel.faces[f] = 1;
    }
    normalizeArrays();
}

UVSet &ObjectData::uvSet0()
{
    if (UVSet *s = findUVSet(0))
        return *s;
    if (uvSets.isEmpty())
        uvSets.append(UVSet{});
    return uvSets.first();
}

const UVSet &ObjectData::uvSet0() const
{
    for (const auto &s : uvSets)
        if (s.id == 0)
            return s;
    return uvSets.first();
}

UVSet *ObjectData::findUVSet(quint32 id)
{
    for (auto &s : uvSets)
        if (s.id == id)
            return &s;
    return nullptr;
}

NamedSelection *ObjectData::findSelection(const QString &name)
{
    for (auto &ns : namedSelections)
        if (ns.name.compare(name, Qt::CaseInsensitive) == 0)
            return &ns;
    return nullptr;
}

const NamedSelection *ObjectData::findSelection(const QString &name) const
{
    return const_cast<ObjectData *>(this)->findSelection(name);
}

QString ObjectData::property(const QString &name) const
{
    for (const auto &p : namedProperties)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return p.value;
    return {};
}

void ObjectData::setProperty(const QString &name, const QString &value)
{
    for (auto &p : namedProperties)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            p.value = value;
            return;
        }
    namedProperties.append({name, value});
}

void ObjectData::removeProperty(const QString &name)
{
    namedProperties.erase(std::remove_if(namedProperties.begin(), namedProperties.end(),
                                         [&](const NamedProperty &p) { return p.name.compare(name, Qt::CaseInsensitive) == 0; }),
                          namedProperties.end());
}

bool ObjectData::faceVisible(int i) const
{
    if (hidden.face(i))
        return false;
    const Face &f = faces[i];
    for (int k = 0; k < f.n; ++k)
        if (hidden.point(f.v[k].point))
            return false;
    return true;
}

float ObjectData::totalMass() const
{
    float m = 0;
    for (float v : mass)
        m += v;
    return m;
}

// The original computes a face's normal (0x5d3f00) as the sum over corners of
// (v[i] - v[i-1]) x (v[i+1] - v[i-1]), i.e. (p1 - p0) x (p2 - p0) for a
// triangle, and Recalculate Normals stores that direction. On shipped models
// (binarize copies the MLOD normals) it points into closed meshes: P3D vertex
// normals point to the back side. The front (outward) normal is its negation.
QVector3D ObjectData::storedNormal(int f) const
{
    const Face &fc = faces[f];
    QVector3D n;
    for (int i = 0; i < fc.n; ++i) {
        const QVector3D prev = points[fc.v[(i + fc.n - 1) % fc.n].point].pos;
        const QVector3D cur = points[fc.v[i].point].pos;
        const QVector3D next = points[fc.v[(i + 1) % fc.n].point].pos;
        n += QVector3D::crossProduct(cur - prev, next - prev);
    }
    return n.normalized();
}

QVector3D ObjectData::faceNormal(int f) const
{
    return -storedNormal(f);
}

QVector3D ObjectData::faceCenter(int f) const
{
    const Face &fc = faces[f];
    QVector3D c;
    for (int k = 0; k < fc.n; ++k)
        c += points[fc.v[k].point].pos;
    return c / float(fc.n);
}

void ObjectData::bounds(QVector3D &mn, QVector3D &mx, bool selectedOnly) const
{
    const float inf = std::numeric_limits<float>::max();
    mn = QVector3D(inf, inf, inf);
    mx = -mn;
    bool any = false;
    for (int i = 0; i < points.size(); ++i) {
        if (selectedOnly && !selection.point(i))
            continue;
        const QVector3D &p = points[i].pos;
        mn = QVector3D(std::min(mn.x(), p.x()), std::min(mn.y(), p.y()), std::min(mn.z(), p.z()));
        mx = QVector3D(std::max(mx.x(), p.x()), std::max(mx.y(), p.y()), std::max(mx.z(), p.z()));
        any = true;
    }
    if (!any)
        mn = mx = QVector3D();
}

QVector3D ObjectData::selectionCenter() const
{
    QVector3D mn, mx;
    bounds(mn, mx, true);
    return (mn + mx) * 0.5f;
}

void ObjectData::selectFacesFromPoints()
{
    selection.faces.resize(faces.size());
    for (int f = 0; f < faces.size(); ++f) {
        bool all = true;
        for (int k = 0; k < faces[f].n && all; ++k)
            all = selection.point(faces[f].v[k].point);
        selection.faces[f] = all ? 1 : 0;
    }
}

void ObjectData::selectPointsFromFaces()
{
    selection.points.resize(points.size());
    for (int f = 0; f < faces.size(); ++f)
        if (selection.face(f))
            for (int k = 0; k < faces[f].n; ++k)
                selection.points[faces[f].v[k].point] = 255;
}

void ObjectData::forEachSelectedPoint(const std::function<void(int)> &fn) const
{
    for (int i = 0; i < points.size(); ++i)
        if (selection.point(i))
            fn(i);
}

void ObjectData::recalculateNormals()
{
    // Smooth across faces sharing a point unless the shared edge is sharp or
    // the face is flagged Flat lighting. Normals are stored per face-vertex.
    QHash<quint64, bool> sharp;
    auto key = [](int a, int b) { return (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b)); };
    for (const auto &e : sharpEdges)
        sharp.insert(key(e.first, e.second), true);

    QVector<QVector3D> fn(faces.size());
    for (int f = 0; f < faces.size(); ++f)
        fn[f] = storedNormal(f);

    QVector<QVector<int>> pointFaces(points.size());
    for (int f = 0; f < faces.size(); ++f)
        for (int k = 0; k < faces[f].n; ++k)
            pointFaces[faces[f].v[k].point].append(f);

    auto sharesSmoothEdge = [&](int f, int g, int p) {
        // f and g share point p; smooth if they also share an edge through p
        // that is not marked sharp, or if they only touch at p.
        const Face &a = faces[f];
        const Face &b = faces[g];
        for (int i = 0; i < a.n; ++i) {
            const int pa = a.v[i].point, pb = a.v[(i + 1) % a.n].point;
            if (pa != p && pb != p)
                continue;
            const int other = pa == p ? pb : pa;
            for (int j = 0; j < b.n; ++j)
                if (b.v[j].point == other)
                    return !sharp.contains(key(p, other));
        }
        return true;
    };

    normals.clear();
    for (int f = 0; f < faces.size(); ++f) {
        Face &fc = faces[f];
        const bool flat = (fc.flags & 0x00100000) != 0;
        for (int k = 0; k < fc.n; ++k) {
            QVector3D n = fn[f];
            if (!flat) {
                const int p = fc.v[k].point;
                for (int g : pointFaces[p])
                    if (g != f && !(faces[g].flags & 0x00100000) && sharesSmoothEdge(f, g, p))
                        n += fn[g];
                n.normalize();
            }
            fc.v[k].normal = addNormal(n);
        }
    }
}
