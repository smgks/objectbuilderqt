#include "ObjIO.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

bool ObjIO::import(const QString &path, ObjectData &o, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const int base = o.points.size();
    QVector<QVector2D> uvs;
    QString group, material;
    QHash<QString, int> groupIndex;
    int line = 0;
    QTextStream in(&f);
    while (!in.atEnd()) {
        ++line;
        const QString l = in.readLine().trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#')))
            continue;
        const QStringList t = l.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString &k = t[0];
        if (k == QLatin1String("v") && t.size() >= 4) {
            o.addPoint(QVector3D(t[1].toFloat(), t[2].toFloat(), -t[3].toFloat()));
        } else if (k == QLatin1String("vt") && t.size() >= 3) {
            uvs.append(QVector2D(t[1].toFloat(), 1.0f - t[2].toFloat()));
        } else if ((k == QLatin1String("g") || k == QLatin1String("o")) && t.size() >= 2) {
            group = t.mid(1).join(QLatin1Char(' '));
        } else if (k == QLatin1String("usemtl") && t.size() >= 2) {
            // Export writes the face texture as the material name (Blender
            // keeps it). Only names that look like a texture are taken back.
            const QString name = t.mid(1).join(QLatin1Char(' '));
            static const QRegularExpression textureLike(QStringLiteral("^#\\(|\\.(paa|pac|tga|png|jpg|jpeg|bmp|dds)$"),
                                                        QRegularExpression::CaseInsensitiveOption);
            material = textureLike.match(name).hasMatch() ? name : QString();
        } else if (k == QLatin1String("f") && t.size() >= 4) {
            QVector<int> pi, ti;
            for (int i = 1; i < t.size(); ++i) {
                const QStringList parts = t[i].split(QLatin1Char('/'));
                int p = parts[0].toInt();
                p = p < 0 ? o.points.size() + p : base + p - 1;
                int ut = parts.size() > 1 && !parts[1].isEmpty() ? parts[1].toInt() : 0;
                ut = ut < 0 ? uvs.size() + ut : ut - 1;
                if (p < base || p >= o.points.size()) {
                    if (error)
                        *error = QStringLiteral("Line %1: vertex index out of range").arg(line);
                    return false;
                }
                pi.append(p);
                ti.append(ut);
            }
            // fan-triangulate polygons with more than four corners
            QVector<QVector<int>> polys;
            if (pi.size() <= 4) {
                QVector<int> q{0, 1, 2, 3};
                q.resize(pi.size());
                polys.append(q);
            } else
                for (int i = 1; i + 1 < pi.size(); ++i)
                    polys.append({0, i, i + 1});
            for (const auto &poly : polys) {
                Face fc;
                fc.n = poly.size();
                fc.texture = material;
                std::array<QVector2D, 4> uv{};
                for (int c = 0; c < poly.size(); ++c) {
                    fc.v[c].point = pi[poly[c]];
                    const int tix = ti[poly[c]];
                    if (tix >= 0 && tix < uvs.size())
                        uv[c] = uvs[tix];
                }
                const int idx = o.addFace(fc, uv);
                const int n = o.addNormal(o.storedNormal(idx));
                for (int c = 0; c < fc.n; ++c)
                    o.faces[idx].v[c].normal = n;
                if (!group.isEmpty()) {
                    if (!groupIndex.contains(group)) {
                        NamedSelection *ex = o.findSelection(group);
                        if (!ex) {
                            o.namedSelections.append(NamedSelection{group, Selection{}});
                            ex = &o.namedSelections.last();
                        }
                        groupIndex.insert(group, int(ex - o.namedSelections.data()));
                    }
                    Selection &s = o.namedSelections[groupIndex.value(group)].sel;
                    s.setFace(idx, true);
                    for (int c = 0; c < fc.n; ++c)
                        s.setPoint(fc.v[c].point, 255);
                }
            }
        }
    }
    o.normalizeArrays();
    return true;
}

bool ObjIO::exportLod(const QString &path, const ObjectData &o, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << "# Exported by Object Builder (Qt)\n";
    for (const Point &p : o.points)
        out << "v " << p.pos.x() << ' ' << p.pos.y() << ' ' << -p.pos.z() << '\n';
    const UVSet &uv = o.uvSet0();
    for (int fi = 0; fi < o.faces.size(); ++fi)
        for (int k = 0; k < o.faces[fi].n; ++k)
            out << "vt " << uv.uv[fi][k].x() << ' ' << 1.0f - uv.uv[fi][k].y() << '\n';
    int t = 1;
    QString lastTex;
    for (int fi = 0; fi < o.faces.size(); ++fi) {
        const Face &fc = o.faces[fi];
        if (fc.texture != lastTex) {
            out << "usemtl " << (fc.texture.isEmpty() ? QStringLiteral("none") : fc.texture) << '\n';
            lastTex = fc.texture;
        }
        out << 'f';
        for (int k = 0; k < fc.n; ++k)
            out << ' ' << fc.v[k].point + 1 << '/' << t++;
        out << '\n';
    }
    return true;
}
