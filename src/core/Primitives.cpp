#include "Primitives.h"

#include <QHash>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace Primitives {
namespace {

// Collects generated geometry, then orients every face so its normal points
// away from `inside` (all generated primitives are convex or flat).
struct Builder {
    ObjectData &o;
    QMatrix4x4 m;
    int firstPoint, firstFace;

    Builder(ObjectData &obj, const QMatrix4x4 &mat) : o(obj), m(mat), firstPoint(obj.points.size()), firstFace(obj.faces.size()) {}

    int point(const QVector3D &local) { return o.addPoint(m.map(local)); }

    void face(std::initializer_list<int> idx, std::initializer_list<QVector2D> uv = {})
    {
        Face f;
        f.n = int(idx.size());
        int k = 0;
        for (int i : idx)
            f.v[k++].point = i;
        std::array<QVector2D, 4> t{};
        k = 0;
        for (const QVector2D &v : uv)
            t[k++] = v;
        o.addFace(f, t);
    }

    // Orient faces: outward = away from the local-frame centre for solids,
    // towards local -Z (the creating camera) for flat primitives.
    void orient(bool flat, const QVector3D &insideLocal = QVector3D())
    {
        const QVector3D inside = m.map(insideLocal);
        const QVector3D towardsViewer = (m.map(QVector3D(0, 0, -1)) - m.map(QVector3D())).normalized();
        for (int f = firstFace; f < o.faces.size(); ++f) {
            const QVector3D n = o.faceNormal(f);
            const QVector3D want = flat ? towardsViewer : (o.faceCenter(f) - inside);
            if (QVector3D::dotProduct(n, want) < 0) {
                Face &fc = o.faces[f];
                std::reverse(fc.v.begin(), fc.v.begin() + fc.n);
                for (auto &set : o.uvSets)
                    std::reverse(set.uv[f].begin(), set.uv[f].begin() + fc.n);
            }
        }
    }

    // Selects the new geometry and gives it flat normals (existing faces keep theirs).
    void select()
    {
        o.selection.clear();
        for (int i = firstPoint; i < o.points.size(); ++i)
            o.selection.setPoint(i, 255);
        for (int i = firstFace; i < o.faces.size(); ++i) {
            o.selection.setFace(i, true);
            const int n = o.addNormal(o.storedNormal(i));
            for (int k = 0; k < o.faces[i].n; ++k)
                o.faces[i].v[k].normal = n;
        }
    }
};

QMatrix4x4 localFrame(const Params &p, const QMatrix4x4 &placement, float height)
{
    QMatrix4x4 m = placement;
    if (p.align == Align::Bottom)
        m.translate(0, height * 0.5f, 0);
    else if (p.align == Align::Top)
        m.translate(0, -height * 0.5f, 0);
    return m;
}

} // namespace

void plane(ObjectData &o, const Params &p, const QMatrix4x4 &placement)
{
    QMatrix4x4 m = localFrame(p, placement, p.sizeY);
    m.scale(p.sizeX * 0.5f, p.sizeY * 0.5f, 1.0f);
    Builder b(o, m);
    const int sx = std::max(1, p.segX), sy = std::max(1, p.segY);
    for (int r = 0; r <= sy; ++r)
        for (int c = 0; c <= sx; ++c)
            b.point(QVector3D(2.0f * c / sx - 1.0f, 2.0f * r / sy - 1.0f, 0));
    auto idx = [&](int r, int c) { return b.firstPoint + r * (sx + 1) + c; };
    auto uv = [&](int r, int c) { return QVector2D(float(c) / sx, 1.0f - float(r) / sy); };
    for (int r = 0; r < sy; ++r)
        for (int c = 0; c < sx; ++c)
            b.face({idx(r, c), idx(r, c + 1), idx(r + 1, c + 1), idx(r + 1, c)},
                   {uv(r, c), uv(r, c + 1), uv(r + 1, c + 1), uv(r + 1, c)});
    b.orient(true);
    b.select();
}

void box(ObjectData &o, const Params &p, const QMatrix4x4 &placement)
{
    QMatrix4x4 m = localFrame(p, placement, p.sizeY);
    m.scale(p.sizeX * 0.5f, p.sizeY * 0.5f, p.sizeZ * 0.5f);
    Builder b(o, m);
    const int seg[3] = {std::max(1, p.segX), std::max(1, p.segY), std::max(1, p.segZ)};

    // Six grids; shared edges are welded through a coordinate lookup.
    QHash<QString, int> weld;
    auto pointAt = [&](const QVector3D &v) {
        const QString key = QStringLiteral("%1,%2,%3").arg(std::lround(v.x() * 1e4)).arg(std::lround(v.y() * 1e4)).arg(std::lround(v.z() * 1e4));
        auto it = weld.find(key);
        if (it != weld.end())
            return *it;
        const int i = b.point(v);
        weld.insert(key, i);
        return i;
    };
    for (int axis = 0; axis < 3; ++axis) {
        const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
        for (float side : {-1.0f, 1.0f}) {
            for (int i = 0; i < seg[a1]; ++i)
                for (int j = 0; j < seg[a2]; ++j) {
                    int q[4];
                    QVector2D t[4];
                    const int di[4] = {0, 1, 1, 0}, dj[4] = {0, 0, 1, 1};
                    for (int k = 0; k < 4; ++k) {
                        float c[3];
                        c[axis] = side;
                        c[a1] = 2.0f * (i + di[k]) / seg[a1] - 1.0f;
                        c[a2] = 2.0f * (j + dj[k]) / seg[a2] - 1.0f;
                        q[k] = pointAt(QVector3D(c[0], c[1], c[2]));
                        t[k] = QVector2D((c[a1] + 1) * 0.5f, 1.0f - (c[a2] + 1) * 0.5f);
                    }
                    b.face({q[0], q[1], q[2], q[3]}, {t[0], t[1], t[2], t[3]});
                }
        }
    }
    b.orient(false);
    b.select();
}

void circle(ObjectData &o, const Params &p, const QMatrix4x4 &placement)
{
    QMatrix4x4 m = localFrame(p, placement, 2 * p.radius);
    m.scale(p.radius, p.radius, 1.0f);
    Builder b(o, m);
    const int n = std::max(3, p.segRadius);
    QVector<int> ring;
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * float(M_PI) * i / n;
        ring.append(b.point(QVector3D(std::cos(a), std::sin(a), 0)));
    }
    auto uv = [&](int i) {
        const float a = 2.0f * float(M_PI) * i / n;
        return QVector2D(0.5f + 0.5f * std::cos(a), 0.5f - 0.5f * std::sin(a));
    };
    if (p.centerPoint) {
        const int c = b.point(QVector3D());
        for (int i = 0; i < n; ++i)
            b.face({c, ring[i], ring[(i + 1) % n]}, {QVector2D(0.5f, 0.5f), uv(i), uv((i + 1) % n)});
    } else {
        // strip of quads/triangles across the disc, no centre vertex
        int lo = 0, hi = n - 1;
        while (hi - lo >= 2) {
            if (hi - lo >= 3) {
                b.face({ring[lo], ring[lo + 1], ring[hi - 1], ring[hi]}, {uv(lo), uv(lo + 1), uv(hi - 1), uv(hi)});
                ++lo;
                --hi;
            } else {
                b.face({ring[lo], ring[lo + 1], ring[hi]}, {uv(lo), uv(lo + 1), uv(hi)});
                ++lo;
            }
        }
    }
    b.orient(true);
    b.select();
}

void cylinder(ObjectData &o, const Params &p, const QMatrix4x4 &placement)
{
    // Axis along local Y (up), height = sizeZ as in the original dialog.
    const float h = p.sizeZ;
    QMatrix4x4 m = localFrame(p, placement, h);
    Builder b(o, m);
    const int n = std::max(3, p.segRadius), sz = std::max(1, p.segZ);
    QVector<QVector<int>> rings;
    for (int r = 0; r <= sz; ++r) {
        QVector<int> ring;
        const float y = -h * 0.5f + h * r / sz;
        for (int i = 0; i < n; ++i) {
            const float a = 2.0f * float(M_PI) * i / n;
            ring.append(b.point(QVector3D(p.radius * std::cos(a), y, p.radius * std::sin(a))));
        }
        rings.append(ring);
    }
    for (int r = 0; r < sz; ++r)
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const float u0 = float(i) / n, u1 = float(i + 1) / n, v0 = 1.0f - float(r) / sz, v1 = 1.0f - float(r + 1) / sz;
            b.face({rings[r][i], rings[r][j], rings[r + 1][j], rings[r + 1][i]},
                   {QVector2D(u0, v0), QVector2D(u1, v0), QVector2D(u1, v1), QVector2D(u0, v1)});
        }
    for (int cap : {0, sz}) {
        const QVector<int> &ring = rings[cap];
        if (p.centerPoint) {
            const int c = b.point(QVector3D(0, -h * 0.5f + h * cap / sz, 0));
            for (int i = 0; i < n; ++i)
                b.face({c, ring[i], ring[(i + 1) % n]});
        } else {
            int lo = 0, hi = n - 1;
            while (hi - lo >= 2) {
                if (hi - lo >= 3) {
                    b.face({ring[lo], ring[lo + 1], ring[hi - 1], ring[hi]});
                    ++lo;
                    --hi;
                } else {
                    b.face({ring[lo], ring[lo + 1], ring[hi]});
                    ++lo;
                }
            }
        }
    }
    b.orient(false);
    b.select();
}

void sphere(ObjectData &o, const Params &p, const QMatrix4x4 &placement)
{
    QMatrix4x4 m = localFrame(p, placement, 2 * p.radius);
    Builder b(o, m);
    const int n = std::max(3, p.segRadius), rings = std::max(2, p.segRadius / 2);
    const int top = b.point(QVector3D(0, p.radius, 0));
    QVector<QVector<int>> rows;
    for (int r = 1; r < rings; ++r) {
        const float phi = float(M_PI) * r / rings;
        QVector<int> row;
        for (int i = 0; i < n; ++i) {
            const float a = 2.0f * float(M_PI) * i / n;
            row.append(b.point(QVector3D(p.radius * std::sin(phi) * std::cos(a), p.radius * std::cos(phi),
                                         p.radius * std::sin(phi) * std::sin(a))));
        }
        rows.append(row);
    }
    const int bottom = b.point(QVector3D(0, -p.radius, 0));
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        b.face({top, rows.first()[i], rows.first()[j]});
        for (int r = 0; r + 1 < rows.size(); ++r)
            b.face({rows[r][i], rows[r + 1][i], rows[r + 1][j], rows[r][j]});
        b.face({bottom, rows.last()[j], rows.last()[i]});
    }
    b.orient(false);
    b.select();
}

} // namespace Primitives
