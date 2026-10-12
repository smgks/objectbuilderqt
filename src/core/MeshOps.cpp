#include "MeshOps.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace MeshOps {
namespace {

quint64 edgeKey(int a, int b)
{
    return (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b));
}

QVector<bool> selectedFaceMask(const ObjectData &o)
{
    QVector<bool> m(o.faces.size());
    for (int i = 0; i < o.faces.size(); ++i)
        m[i] = o.selection.face(i);
    return m;
}

void reverseFace(ObjectData &o, int f)
{
    Face &fc = o.faces[f];
    std::reverse(fc.v.begin(), fc.v.begin() + fc.n);
    for (auto &set : o.uvSets)
        std::reverse(set.uv[f].begin(), set.uv[f].begin() + fc.n);
}

// Replace face f by the given corner subset (indices into the old corners).
void reshapeFace(ObjectData &o, int f, const QVector<int> &corners)
{
    Face old = o.faces[f];
    QVector<std::array<QVector2D, 4>> oldUv;
    for (auto &set : o.uvSets)
        oldUv.append(set.uv[f]);
    Face &fc = o.faces[f];
    fc.n = corners.size();
    for (int k = 0; k < corners.size(); ++k)
        fc.v[k] = old.v[corners[k]];
    for (int s = 0; s < o.uvSets.size(); ++s)
        for (int k = 0; k < corners.size(); ++k)
            o.uvSets[s].uv[f][k] = oldUv[s][corners[k]];
}

// Appends a copy of face f restricted to `corners`; returns new index.
int splitOff(ObjectData &o, int f, const QVector<int> &corners)
{
    Face nf = o.faces[f];
    nf.n = corners.size();
    for (int k = 0; k < corners.size(); ++k)
        nf.v[k] = o.faces[f].v[corners[k]];
    const int idx = o.addFace(nf);
    for (auto &set : o.uvSets) {
        std::array<QVector2D, 4> t{};
        for (int k = 0; k < corners.size(); ++k)
            t[k] = set.uv[f][corners[k]];
        set.uv[idx] = t;
    }
    // inherit selections
    o.selection.setFace(idx, o.selection.face(f));
    o.hidden.setFace(idx, o.hidden.face(f));
    o.locked.setFace(idx, o.locked.face(f));
    for (auto &ns : o.namedSelections)
        ns.sel.setFace(idx, ns.sel.face(f));
    return idx;
}

} // namespace

// ---------------------------------------------------------------- deletion

void deleteSelection(ObjectData &o)
{
    QVector<bool> pts(o.points.size());
    for (int i = 0; i < o.points.size(); ++i)
        pts[i] = o.selection.point(i) && !o.locked.point(i);
    o.deleteFaces(selectedFaceMask(o));
    o.deletePoints(pts);
}

void removeSelectedFaces(ObjectData &o)
{
    o.deleteFaces(selectedFaceMask(o));
}

// ---------------------------------------------------------------- faces

void reverseSelectedFaces(ObjectData &o)
{
    for (int f = 0; f < o.faces.size(); ++f)
        if (o.selection.face(f))
            reverseFace(o, f);
}

void uncrossSelectedFaces(ObjectData &o)
{
    for (int f = 0; f < o.faces.size(); ++f) {
        if (!o.selection.face(f) || o.faces[f].n != 4)
            continue;
        const Face &fc = o.faces[f];
        auto p = [&](int k) { return o.points[fc.v[k].point].pos; };
        // A non-crossed quad has both triangle normals pointing the same way.
        const QVector3D n1 = QVector3D::crossProduct(p(2) - p(0), p(1) - p(0));
        const QVector3D n2 = QVector3D::crossProduct(p(3) - p(0), p(2) - p(0));
        if (QVector3D::dotProduct(n1, n2) >= 0)
            continue;
        const QVector3D n3 = QVector3D::crossProduct(p(3) - p(0), p(1) - p(0));
        const QVector3D n4 = QVector3D::crossProduct(p(2) - p(0), p(3) - p(0));
        if (QVector3D::dotProduct(n3, n4) >= 0)
            reshapeFace(o, f, {0, 1, 3, 2});
        else
            reshapeFace(o, f, {0, 2, 1, 3});
    }
}

void triangulateSelected(ObjectData &o, bool alongFirstDiagonal)
{
    const int n = o.faces.size();
    for (int f = 0; f < n; ++f) {
        if (!o.selection.face(f) || o.faces[f].n != 4)
            continue;
        if (alongFirstDiagonal) {
            splitOff(o, f, {0, 2, 3});
            reshapeFace(o, f, {0, 1, 2});
        } else {
            splitOff(o, f, {1, 3, 0});
            reshapeFace(o, f, {1, 2, 3});
        }
    }
}

int squarizeSelected(ObjectData &o)
{
    // Greedily merge pairs of selected triangles sharing an edge into a convex,
    // near-planar quad with the same texture/material/flags.
    QHash<quint64, QVector<int>> edgeFaces;
    for (int f = 0; f < o.faces.size(); ++f) {
        const Face &fc = o.faces[f];
        if (fc.n != 3 || !o.selection.face(f))
            continue;
        for (int k = 0; k < 3; ++k)
            edgeFaces[edgeKey(fc.v[k].point, fc.v[(k + 1) % 3].point)].append(f);
    }
    struct Candidate {
        int a, b, ka, kb;
        float score; // lower = closer to a rectangle
    };
    QVector<Candidate> cands;
    for (auto it = edgeFaces.cbegin(); it != edgeFaces.cend(); ++it) {
        if (it.value().size() != 2)
            continue;
        const int a = it.value()[0], b = it.value()[1];
        const Face &fa = o.faces[a];
        const Face &fb = o.faces[b];
        if (fa.texture != fb.texture || fa.material != fb.material || fa.flags != fb.flags)
            continue;
        if (QVector3D::dotProduct(o.faceNormal(a), o.faceNormal(b)) < 0.985f)
            continue;
        // corners opposite to the shared edge
        const int e0 = int(it.key() >> 32), e1 = int(it.key() & 0xffffffff);
        int ka = 0, kb = 0;
        for (int k = 0; k < 3; ++k) {
            if (fa.v[k].point != e0 && fa.v[k].point != e1)
                ka = k;
            if (fb.v[k].point != e0 && fb.v[k].point != e1)
                kb = k;
        }
        const int qp[4] = {fa.v[ka].point, fa.v[(ka + 1) % 3].point, fb.v[kb].point, fa.v[(ka + 2) % 3].point};
        auto p = [&](int k) { return o.points[qp[k & 3]].pos; };
        // convex and how far from right angles
        float score = 0;
        bool convex = true;
        const QVector3D n0 = QVector3D::crossProduct(p(2) - p(0), p(1) - p(0));
        for (int k = 0; k < 4; ++k) {
            const QVector3D e1v = p(k + 1) - p(k), e2v = p(k + 3) - p(k);
            if (QVector3D::dotProduct(QVector3D::crossProduct(e2v, e1v), n0) <= 0)
                convex = false;
            score += std::abs(QVector3D::dotProduct(e1v.normalized(), e2v.normalized()));
        }
        if (convex)
            cands.append({a, b, ka, kb, score});
    }
    std::sort(cands.begin(), cands.end(), [](const Candidate &x, const Candidate &y) { return x.score < y.score; });

    QVector<bool> used(o.faces.size(), false), remove(o.faces.size(), false);
    int created = 0;
    for (const Candidate &c : cands) {
        const int a = c.a, b = c.b, ka = c.ka, kb = c.kb;
        if (used[a] || used[b])
            continue;
        const Face fa = o.faces[a];
        const Face &fb = o.faces[b];
        // quad: a[ka], a[ka+1], b[kb], a[ka+2]
        Face q = fa;
        q.n = 4;
        q.v[0] = fa.v[ka];
        q.v[1] = fa.v[(ka + 1) % 3];
        q.v[2] = fb.v[kb];
        q.v[3] = fa.v[(ka + 2) % 3];
        std::array<QVector2D, 4> uv{};
        for (auto &set : o.uvSets) {
            uv[0] = set.uv[a][ka];
            uv[1] = set.uv[a][(ka + 1) % 3];
            uv[2] = set.uv[b][kb];
            uv[3] = set.uv[a][(ka + 2) % 3];
            set.uv[a] = uv;
        }
        o.faces[a] = q;
        used[a] = used[b] = true;
        remove[b] = true;
        ++created;
    }
    o.deleteFaces(remove);
    return created;
}

void moveSelectedFaces(ObjectData &o, bool toTop)
{
    QVector<int> order, sel, rest;
    for (int f = 0; f < o.faces.size(); ++f)
        (o.selection.face(f) ? sel : rest).append(f);
    order = toTop ? sel + rest : rest + sel;
    auto permute = [&order](auto &vec) {
        auto copy = vec;
        for (int i = 0; i < order.size(); ++i)
            vec[i] = copy[order[i]];
    };
    o.normalizeArrays();
    permute(o.faces);
    permute(o.selection.faces);
    permute(o.hidden.faces);
    permute(o.locked.faces);
    for (auto &ns : o.namedSelections)
        permute(ns.sel.faces);
    for (auto &set : o.uvSets)
        permute(set.uv);
}

QVector<int> degeneratedFaces(const ObjectData &o)
{
    QVector<int> out;
    for (int f = 0; f < o.faces.size(); ++f) {
        const Face &fc = o.faces[f];
        QSet<int> unique;
        for (int k = 0; k < fc.n; ++k)
            unique.insert(fc.v[k].point);
        if (unique.size() < fc.n) {
            out.append(f);
            continue;
        }
        const QVector3D p0 = o.points[fc.v[0].point].pos;
        const float area = QVector3D::crossProduct(o.points[fc.v[1].point].pos - p0, o.points[fc.v[2].point].pos - p0).length();
        if (area < 1e-9f)
            out.append(f);
    }
    return out;
}

int repairDegenerated(ObjectData &o)
{
    QVector<bool> remove(o.faces.size(), false);
    int fixed = 0;
    for (int f : degeneratedFaces(o)) {
        const Face &fc = o.faces[f];
        QVector<int> corners;
        for (int k = 0; k < fc.n; ++k) {
            bool dup = false;
            for (int c : corners)
                dup |= fc.v[c].point == fc.v[k].point;
            if (!dup)
                corners.append(k);
        }
        if (corners.size() >= 3 && corners.size() < fc.n) {
            reshapeFace(o, f, corners);
            ++fixed;
        } else {
            remove[f] = true;
            ++fixed;
        }
    }
    o.deleteFaces(remove);
    return fixed;
}

// ---------------------------------------------------------------- points

void transformSelected(ObjectData &o, const QMatrix4x4 &m, bool allAnimations)
{
    for (int i = 0; i < o.points.size(); ++i) {
        if (!o.selection.point(i) || o.locked.point(i))
            continue;
        o.points[i].pos = m.map(o.points[i].pos);
        if (allAnimations)
            for (auto &a : o.animations)
                a.points[i] = m.map(a.points[i]);
    }
}

void mergeSelectedPoints(ObjectData &o)
{
    QVector<int> sel;
    for (int i = 0; i < o.points.size(); ++i)
        if (o.selection.point(i))
            sel.append(i);
    if (sel.size() < 2)
        return;
    QVector3D c;
    for (int i : sel)
        c += o.points[i].pos;
    c /= float(sel.size());
    const int keep = sel.first();
    o.points[keep].pos = c;
    QVector<bool> remove(o.points.size(), false);
    for (int i : sel.mid(1))
        remove[i] = true;
    for (auto &f : o.faces)
        for (int k = 0; k < f.n; ++k)
            if (remove[f.v[k].point])
                f.v[k].point = keep;
    repairDegenerated(o);
    o.deletePoints(remove);
}

int mergeNear(ObjectData &o, float tolerance)
{
    QVector<int> sel;
    for (int i = 0; i < o.points.size(); ++i)
        if (o.selection.point(i))
            sel.append(i);
    QVector<int> target(o.points.size());
    for (int i = 0; i < target.size(); ++i)
        target[i] = i;
    const float t2 = tolerance * tolerance;
    int merged = 0;
    for (int a = 0; a < sel.size(); ++a) {
        const int pa = sel[a];
        if (target[pa] != pa)
            continue;
        for (int b = a + 1; b < sel.size(); ++b) {
            const int pb = sel[b];
            if (target[pb] == pb && (o.points[pa].pos - o.points[pb].pos).lengthSquared() <= t2) {
                target[pb] = pa;
                ++merged;
            }
        }
    }
    if (!merged)
        return 0;
    QVector<bool> remove(o.points.size(), false);
    for (int i = 0; i < target.size(); ++i)
        remove[i] = target[i] != i;
    for (auto &f : o.faces)
        for (int k = 0; k < f.n; ++k)
            f.v[k].point = target[f.v[k].point];
    repairDegenerated(o);
    o.deletePoints(remove);
    return merged;
}

QVector<int> isolatedPoints(const ObjectData &o)
{
    QVector<bool> used(o.points.size(), false);
    for (const auto &f : o.faces)
        for (int k = 0; k < f.n; ++k)
            used[f.v[k].point] = true;
    QVector<int> out;
    for (int i = 0; i < used.size(); ++i)
        if (!used[i])
            out.append(i);
    return out;
}

// ---------------------------------------------------------------- creation

bool createFaceFromSelection(ObjectData &o, const QVector3D &viewerDir)
{
    QVector<int> sel;
    for (int i = 0; i < o.points.size(); ++i)
        if (o.selection.point(i))
            sel.append(i);
    if (sel.size() != 3 && sel.size() != 4)
        return false;
    if (sel.size() == 4) {
        // order the four points around their centroid in the best-fit plane
        QVector3D c;
        for (int i : sel)
            c += o.points[i].pos;
        c /= 4.0f;
        QVector3D n = QVector3D::crossProduct(o.points[sel[2]].pos - o.points[sel[0]].pos, o.points[sel[1]].pos - o.points[sel[0]].pos);
        if (n.lengthSquared() < 1e-12f)
            n = viewerDir;
        n.normalize();
        QVector3D ax = (o.points[sel[0]].pos - c).normalized();
        const QVector3D ay = QVector3D::crossProduct(n, ax);
        std::sort(sel.begin(), sel.end(), [&](int a, int b) {
            const QVector3D da = o.points[a].pos - c, db = o.points[b].pos - c;
            return std::atan2(QVector3D::dotProduct(da, ay), QVector3D::dotProduct(da, ax))
                   < std::atan2(QVector3D::dotProduct(db, ay), QVector3D::dotProduct(db, ax));
        });
    }
    Face f;
    f.n = sel.size();
    for (int k = 0; k < f.n; ++k)
        f.v[k].point = sel[k];
    const int idx = o.addFace(f);
    if (QVector3D::dotProduct(o.faceNormal(idx), -viewerDir) < 0)
        reverseFace(o, idx);
    const int n = o.addNormal(o.storedNormal(idx));
    for (int k = 0; k < f.n; ++k)
        o.faces[idx].v[k].normal = n;
    o.selection.setFace(idx, true);
    return true;
}

void extrudeSelectedFaces(ObjectData &o)
{
    // Boundary edges of the selected region (used by exactly one selected face,
    // in that face's winding direction).
    QHash<quint64, int> count;
    for (int f = 0; f < o.faces.size(); ++f) {
        if (!o.selection.face(f))
            continue;
        const Face &fc = o.faces[f];
        for (int k = 0; k < fc.n; ++k)
            count[edgeKey(fc.v[k].point, fc.v[(k + 1) % fc.n].point)]++;
    }
    QHash<int, int> dup; // old point -> new point
    auto newPoint = [&](int p) {
        auto it = dup.find(p);
        if (it != dup.end())
            return *it;
        const int n = o.addPoint(o.points[p].pos, o.points[p].flags);
        dup.insert(p, n);
        return n;
    };
    const int nf = o.faces.size();
    QVector<std::array<int, 2>> sides;
    for (int f = 0; f < nf; ++f) {
        if (!o.selection.face(f))
            continue;
        Face &fc = o.faces[f];
        for (int k = 0; k < fc.n; ++k) {
            const int a = fc.v[k].point, b = fc.v[(k + 1) % fc.n].point;
            if (count.value(edgeKey(a, b)) == 1)
                sides.append({a, b});
        }
        for (int k = 0; k < fc.n; ++k)
            fc.v[k].point = newPoint(fc.v[k].point);
    }
    for (const auto &e : sides) {
        // side quad keeps the winding of the cap face it belonged to
        Face s;
        s.n = 4;
        s.v[0].point = e[0];
        s.v[1].point = e[1];
        s.v[2].point = dup.value(e[1]);
        s.v[3].point = dup.value(e[0]);
        const int idx = o.addFace(s);
        const int n = o.addNormal(o.storedNormal(idx));
        for (int k = 0; k < 4; ++k)
            o.faces[idx].v[k].normal = n;
        o.selection.setFace(idx, false);
    }
    // new cap points become the selection
    for (int i = 0; i < o.points.size(); ++i)
        o.selection.setPoint(i, 0);
    for (auto it = dup.cbegin(); it != dup.cend(); ++it)
        o.selection.setPoint(it.value(), 255);
}

// ---------------------------------------------------------------- selection

void growSelection(ObjectData &o)
{
    QVector<bool> add(o.points.size(), false);
    for (int f = 0; f < o.faces.size(); ++f) {
        const Face &fc = o.faces[f];
        bool any = false;
        for (int k = 0; k < fc.n && !any; ++k)
            any = o.selection.point(fc.v[k].point);
        if (any)
            for (int k = 0; k < fc.n; ++k)
                add[fc.v[k].point] = true;
    }
    for (int i = 0; i < add.size(); ++i)
        if (add[i] && o.pointVisible(i) && !o.locked.point(i))
            o.selection.setPoint(i, 255);
    o.selectFacesFromPoints();
}

void shrinkSelection(ObjectData &o)
{
    QVector<bool> drop(o.points.size(), false);
    for (int f = 0; f < o.faces.size(); ++f) {
        const Face &fc = o.faces[f];
        bool all = true;
        for (int k = 0; k < fc.n && all; ++k)
            all = o.selection.point(fc.v[k].point);
        if (!all)
            for (int k = 0; k < fc.n; ++k)
                drop[fc.v[k].point] = true;
    }
    for (int i = 0; i < drop.size(); ++i)
        if (drop[i])
            o.selection.setPoint(i, 0);
    o.selectFacesFromPoints();
}

void invertSelection(ObjectData &o)
{
    for (int i = 0; i < o.points.size(); ++i)
        if (o.pointVisible(i) && !o.locked.point(i))
            o.selection.setPoint(i, o.selection.point(i) ? 0 : 255);
    for (int f = 0; f < o.faces.size(); ++f)
        if (o.faceVisible(f) && !o.locked.face(f))
            o.selection.setFace(f, !o.selection.face(f));
}

// ---------------------------------------------------------------- analysis

int findComponents(ObjectData &o)
{
    QVector<int> parent(o.points.size());
    for (int i = 0; i < parent.size(); ++i)
        parent[i] = i;
    std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
    for (const auto &f : o.faces)
        for (int k = 1; k < f.n; ++k)
            parent[find(f.v[k].point)] = find(f.v[0].point);

    QHash<int, int> compIndex;
    for (const auto &f : o.faces) {
        const int root = find(f.v[0].point);
        if (!compIndex.contains(root))
            compIndex.insert(root, compIndex.size());
    }
    // remove previous ComponentXX selections, like the original does
    o.namedSelections.erase(std::remove_if(o.namedSelections.begin(), o.namedSelections.end(),
                                           [](const NamedSelection &ns) {
                                               return ns.name.startsWith(QLatin1String("Component"), Qt::CaseInsensitive);
                                           }),
                            o.namedSelections.end());
    QVector<NamedSelection> comps(compIndex.size());
    for (int c = 0; c < comps.size(); ++c) {
        comps[c].name = QStringLiteral("Component%1").arg(c + 1, 2, 10, QLatin1Char('0'));
        comps[c].sel.resize(o.points.size(), o.faces.size());
    }
    for (int f = 0; f < o.faces.size(); ++f) {
        const int c = compIndex.value(find(o.faces[f].v[0].point));
        comps[c].sel.faces[f] = 1;
        for (int k = 0; k < o.faces[f].n; ++k)
            comps[c].sel.points[o.faces[f].v[k].point] = 255;
    }
    o.namedSelections += comps;
    return comps.size();
}

int selectNonClosed(ObjectData &o)
{
    QHash<quint64, int> count;
    for (const auto &f : o.faces)
        for (int k = 0; k < f.n; ++k)
            count[edgeKey(f.v[k].point, f.v[(k + 1) % f.n].point)]++;
    o.selection.clear();
    int open = 0;
    for (auto it = count.cbegin(); it != count.cend(); ++it)
        if (it.value() == 1) {
            o.selection.setPoint(int(it.key() >> 32), 255);
            o.selection.setPoint(int(it.key() & 0xffffffff), 255);
            ++open;
        }
    return open;
}

// ---------------------------------------------------------------- surfaces

void setSharpEdges(ObjectData &o, bool sharp)
{
    QSet<quint64> edges;
    for (const auto &e : o.sharpEdges)
        edges.insert(edgeKey(e.first, e.second));
    for (const auto &f : o.faces)
        for (int k = 0; k < f.n; ++k) {
            const int a = f.v[k].point, b = f.v[(k + 1) % f.n].point;
            if (!o.selection.point(a) || !o.selection.point(b))
                continue;
            if (sharp)
                edges.insert(edgeKey(a, b));
            else
                edges.remove(edgeKey(a, b));
        }
    o.sharpEdges.clear();
    for (quint64 k : edges)
        o.sharpEdges.append({int(k >> 32), int(k & 0xffffffff)});
    std::sort(o.sharpEdges.begin(), o.sharpEdges.end());
}

} // namespace MeshOps
