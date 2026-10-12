// Developer command-line tool for the core library.
//   obtool pbo-list <file.pbo> [filter]
//   obtool extract <file.pbo> <entry> <out>
//   obtool paa <in.paa> <out.png>
//   obtool odol <in.p3d> [out.p3d]         parse ODOL, optionally save as MLOD
//   obtool odol-scan <folder-or.pbo> [max] parse every .p3d in PBOs, report failures
//   obtool obj <model> <out.obj> [lod]      export one LOD (default: first) to OBJ
//   obtool orient <model>                  face direction report per LOD
//   obtool textures <model>                list textures and whether they resolve
//   obtool sels <model> [lod]              named selections with point/face counts
// <model> is a P3D (MLOD or binarized), OBJ or FBX file.
#include "core/FbxIO.h"
#include "core/ObjIO.h"
#include "core/Odol.h"
#include "core/P3dIO.h"
#include "core/Paa.h"
#include "core/Pbo.h"
#include "core/TextureLibrary.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <cstdio>
#include <functional>

static QByteArray readFile(const QString &p)
{
    QFile f(p);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

static bool loadAny(const QString &path, LodObject &obj, QString *err)
{
    if (path.endsWith(QLatin1String(".obj"), Qt::CaseInsensitive)) {
        obj = LodObject();
        return ObjIO::import(path, obj.lods[0].data, err);
    }
    if (path.endsWith(QLatin1String(".fbx"), Qt::CaseInsensitive))
        return FbxIO::import(path, obj, {}, nullptr, err);
    const QByteArray data = readFile(path);
    if (Odol::isOdol(data))
        return Odol::load(data, obj, err);
    return P3dIO::load(path, obj, err);
}

// Closed parts (every edge used by exactly two faces) are outward when their
// volume measured with the front normals is positive. Stored normals are
// compared with storedNormal(), the direction the original writes.
static void orientReport(const QString &name, const ObjectData &o)
{
    QVector<int> parent(o.points.size());
    for (int i = 0; i < parent.size(); ++i)
        parent[i] = i;
    std::function<int(int)> root = [&](int x) { return parent[x] == x ? x : parent[x] = root(parent[x]); };
    for (const Face &f : o.faces)
        for (int k = 1; k < f.n; ++k)
            parent[root(f.v[k].point)] = root(f.v[0].point);
    QHash<int, double> volume;
    QHash<int, QHash<quint64, int>> edges;
    int normalsAgree = 0, normalsOpposite = 0;
    for (int fi = 0; fi < o.faces.size(); ++fi) {
        const Face &f = o.faces[fi];
        const int r = root(f.v[0].point);
        for (int k = 1; k + 1 < f.n; ++k) {
            const QVector3D a = o.points[f.v[0].point].pos, b = o.points[f.v[k].point].pos, c = o.points[f.v[k + 1].point].pos;
            volume[r] += QVector3D::dotProduct(a, QVector3D::crossProduct(c, b)) / 6.0; // front = (c-a)x(b-a)
        }
        for (int k = 0; k < f.n; ++k) {
            const int p = f.v[k].point, q = f.v[(k + 1) % f.n].point;
            edges[r][(quint64(std::min(p, q)) << 32) | quint32(std::max(p, q))]++;
        }
        QVector3D vn;
        for (int k = 0; k < f.n; ++k)
            if (f.v[k].normal < o.normals.size())
                vn += o.normals[f.v[k].normal];
        const float d = QVector3D::dotProduct(vn, o.storedNormal(fi));
        normalsAgree += d > 1e-6f;
        normalsOpposite += d < -1e-6f;
    }
    int out = 0, in = 0, open = 0;
    for (auto it = volume.cbegin(); it != volume.cend(); ++it) {
        bool closed = true;
        for (int uses : edges[it.key()])
            closed &= uses == 2;
        if (!closed)
            ++open;
        else if (it.value() > 0)
            ++out;
        else if (it.value() < 0)
            ++in;
    }
    QVector3D mn, mx;
    o.bounds(mn, mx);
    std::printf("  %-22s faces %6d | closed parts outward %4d inward %4d (open %4d) | stored normals ok %6d reversed %6d"
                " | bbox (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",
                qPrintable(name), int(o.faces.size()), out, in, open, normalsAgree, normalsOpposite, mn.x(), mn.y(), mn.z(), mx.x(), mx.y(), mx.z());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 3) {
        std::fprintf(stderr, "usage: obtool pbo-list|extract|paa|odol|odol-scan ...\n");
        return 2;
    }
    const QString cmd = a[1];
    if (cmd == QLatin1String("pbo-list")) {
        PboArchive pbo;
        QString err;
        if (!pbo.open(a[2], &err)) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return 1;
        }
        std::printf("prefix=%s entries=%d\n", qPrintable(pbo.prefix()), int(pbo.entries().size()));
        for (const auto &e : pbo.entries())
            if (a.size() < 4 || e.name.contains(a[3], Qt::CaseInsensitive))
                std::printf("%s %u %08x\n", qPrintable(e.name), e.originalSize ? e.originalSize : e.dataSize, e.method);
        return 0;
    }
    if (cmd == QLatin1String("extract") && a.size() >= 5) {
        PboArchive pbo;
        pbo.open(a[2]);
        for (const auto &e : pbo.entries())
            if (e.name.compare(a[3], Qt::CaseInsensitive) == 0) {
                QByteArray data;
                QString err;
                if (!pbo.read(e, data, &err)) {
                    std::fprintf(stderr, "%s\n", qPrintable(err));
                    return 1;
                }
                QFile f(a[4]);
                f.open(QIODevice::WriteOnly);
                f.write(data);
                return 0;
            }
        std::fprintf(stderr, "not found\n");
        return 1;
    }
    if (cmd == QLatin1String("paa") && a.size() >= 4) {
        QString err;
        const QImage img = Paa::decode(readFile(a[2]), 4096, &err);
        if (img.isNull()) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return 1;
        }
        img.save(a[3]);
        std::printf("%dx%d\n", img.width(), img.height());
        return 0;
    }
    if (cmd == QLatin1String("odol")) {
        LodObject obj;
        QString err;
        Odol::Info info;
        if (!Odol::load(readFile(a[2]), obj, &err, &info)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        std::printf("version %d, %d LODs\n", info.version, int(obj.lods.size()));
        if (!info.bones.isEmpty())
            std::printf("skeleton bones: %s\n", qPrintable(info.bones.join(QStringLiteral(", "))));
        for (const Lod &l : obj.lods)
            std::printf("  %-28s mass %8.2f points %6d faces %6d selections %3d props %2d tex0 %s\n",
                        qPrintable(LodNames::name(l.resolution)), double(l.data.totalMass()),
                        int(l.data.points.size()), int(l.data.faces.size()), int(l.data.namedSelections.size()),
                        int(l.data.namedProperties.size()),
                        l.data.faces.isEmpty() ? "" : qPrintable(l.data.faces[0].texture));
        for (const QString &w : info.warnings)
            std::printf("  warning: %s\n", qPrintable(w));
        for (const Lod &l : obj.lods) {
            QHash<QString, int> tex;
            for (const Face &f : l.data.faces)
                tex[f.texture + QStringLiteral(" | ") + f.material]++;
            for (auto it = tex.cbegin(); it != tex.cend(); ++it)
                std::printf("    %-28s %5d x %s\n", qPrintable(LodNames::name(l.resolution)), it.value(), qPrintable(it.key()));
        }
        if (qEnvironmentVariableIsSet("OBTOOL_FACES")) {
            const ObjectData &o = obj.lods[0].data;
            for (int f = 0; f < o.faces.size(); ++f) {
                if (!o.faces[f].texture.isEmpty())
                    continue;
                QStringList sels;
                for (const auto &ns : o.namedSelections)
                    if (ns.sel.face(f))
                        sels << ns.name;
                QStringList pts;
                for (int k = 0; k < o.faces[f].n; ++k) {
                    const Point &p = o.points[o.faces[f].v[k].point];
                    pts << QStringLiteral("(%1,%2,%3 f%4)").arg(p.pos.x(), 0, 'f', 2).arg(p.pos.y(), 0, 'f', 2).arg(p.pos.z(), 0, 'f', 2).arg(p.flags, 0, 16);
                }
                std::printf("face %d %s [%s]\n", f, qPrintable(pts.join(' ')), qPrintable(sels.join(", ")));
            }
        }
        if (a.size() >= 4 && !P3dIO::save(a[3], obj, {}, &err)) {
            std::fprintf(stderr, "save: %s\n", qPrintable(err));
            return 1;
        }
        return 0;
    }
    if (cmd == QLatin1String("fbx")) {
        LodObject obj;
        FbxIO::Report rep;
        QString err;
        if (!FbxIO::import(a[2], obj, {}, &rep, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        std::printf("meshes %d faces %d lods %d textures found %d\n", rep.meshes, rep.faces, rep.lods, rep.texturesFound);
        for (const QString &t : rep.texturesExtracted)
            std::printf("  extracted %s\n", qPrintable(t));
        for (const QString &t : rep.texturesMissing)
            std::printf("  missing %s\n", qPrintable(t));
        for (const Lod &l : obj.lods) {
            QVector3D mn, mx;
            l.data.bounds(mn, mx);
            std::printf("  %-12s points %3d faces %3d sels %s tex '%s' bbox (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) n0 (%.2f,%.2f,%.2f)\n",
                        qPrintable(LodNames::name(l.resolution)), int(l.data.points.size()), int(l.data.faces.size()),
                        qPrintable(l.data.namedSelections.isEmpty() ? QString() : l.data.namedSelections[0].name),
                        qPrintable(l.data.faces.isEmpty() ? QString() : l.data.faces[0].texture), mn.x(), mn.y(), mn.z(), mx.x(), mx.y(),
                        mx.z(), l.data.faces.isEmpty() ? 0.f : l.data.faceNormal(0).x(),
                        l.data.faces.isEmpty() ? 0.f : l.data.faceNormal(0).y(), l.data.faces.isEmpty() ? 0.f : l.data.faceNormal(0).z());
            int outward = 0;
            for (int f = 0; f < l.data.faces.size(); ++f)
                outward += QVector3D::dotProduct(l.data.faceNormal(f), l.data.faceCenter(f) - (mn + mx) * 0.5f) > 0;
            std::printf("               outward faces %d/%d, uv0 face0 %.2f,%.2f\n", outward, int(l.data.faces.size()),
                        l.data.faces.isEmpty() ? 0.f : l.data.uv(0, 0).x(), l.data.faces.isEmpty() ? 0.f : l.data.uv(0, 0).y());
        }
        if (a.size() >= 4)
            P3dIO::save(a[3], obj, {}, &err);
        return 0;
    }
    if (cmd == QLatin1String("obj") && a.size() >= 4) {
        LodObject obj;
        QString err;
        if (!loadAny(a[2], obj, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        const int lod = a.size() >= 5 ? a[4].toInt() : 0;
        if (lod < 0 || lod >= obj.lods.size() || !ObjIO::exportLod(a[3], obj.lods[lod].data, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        std::printf("exported %s (%d faces)\n", qPrintable(LodNames::name(obj.lods[lod].resolution)),
                    int(obj.lods[lod].data.faces.size()));
        return 0;
    }
    if (cmd == QLatin1String("sels")) {
        LodObject obj;
        QString err;
        if (!loadAny(a[2], obj, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        const int lod = a.size() >= 4 ? a[3].toInt() : 0;
        if (lod < 0 || lod >= obj.lods.size())
            return 1;
        const ObjectData &o = obj.lods[lod].data;
        for (const NamedSelection &ns : o.namedSelections) {
            QVector3D c;
            int n = 0;
            for (int i = 0; i < o.points.size(); ++i)
                if (ns.sel.point(i)) {
                    c += o.points[i].pos;
                    ++n;
                }
            if (n)
                c /= float(n);
            std::printf("  %-60s points %5d faces %5d centre (%.2f %.2f %.2f)\n", qPrintable(ns.name), ns.sel.pointCount(),
                        ns.sel.faceCount(), c.x(), c.y(), c.z());
        }
        return 0;
    }
    if (cmd == QLatin1String("textures")) {
        LodObject obj;
        QString err;
        if (!loadAny(a[2], obj, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        TextureLibrary::instance().setModelFolder(QFileInfo(a[2]).absolutePath());
        QStringList textures;
        for (const Lod &l : obj.lods)
            for (const Face &f : l.data.faces)
                if (!f.texture.isEmpty() && !textures.contains(f.texture, Qt::CaseInsensitive))
                    textures << f.texture;
        int missing = 0;
        for (const QString &t : textures) {
            const QImage img = TextureLibrary::instance().image(t);
            missing += img.isNull();
            std::printf("  %-8s %s\n", img.isNull() ? "MISSING" : qPrintable(QStringLiteral("%1x%2").arg(img.width()).arg(img.height())),
                        qPrintable(t));
        }
        std::printf("%d texture(s), %d missing\n", int(textures.size()), missing);
        return missing ? 1 : 0;
    }
    if (cmd == QLatin1String("orient")) {
        LodObject obj;
        QString err;
        if (!loadAny(a[2], obj, &err)) {
            std::fprintf(stderr, "FAIL: %s\n", qPrintable(err));
            return 1;
        }
        for (const Lod &l : obj.lods)
            orientReport(LodNames::name(l.resolution), l.data);
        return 0;
    }
    if (cmd == QLatin1String("odol-scan")) {
        QStringList pbos;
        if (a[2].endsWith(QLatin1String(".pbo"), Qt::CaseInsensitive))
            pbos << a[2];
        else {
            QDirIterator it(a[2], {QStringLiteral("*.pbo")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                pbos << it.next();
        }
        const int max = a.size() >= 4 ? a[3].toInt() : 1 << 30;
        int ok = 0, bad = 0, mlod = 0;
        QElapsedTimer t;
        t.start();
        for (const QString &p : pbos) {
            PboArchive pbo;
            if (!pbo.open(p))
                continue;
            for (const auto &e : pbo.entries()) {
                if (!e.name.endsWith(QLatin1String(".p3d"), Qt::CaseInsensitive))
                    continue;
                if (ok + bad + mlod >= max)
                    break;
                QByteArray data;
                pbo.read(e, data);
                if (!data.startsWith("ODOL")) {
                    ++mlod;
                    continue;
                }
                LodObject obj;
                QString err;
                if (Odol::load(data, obj, &err)) {
                    ++ok;
                } else {
                    ++bad;
                    if (bad <= 40)
                        std::printf("FAIL %s\\%s: %s\n", qPrintable(QFileInfo(p).fileName()), qPrintable(e.name), qPrintable(err));
                }
            }
        }
        std::printf("ok %d, failed %d, non-ODOL %d, %.1fs\n", ok, bad, mlod, t.elapsed() / 1000.0);
        return bad ? 1 : 0;
    }
    std::fprintf(stderr, "bad command\n");
    return 2;
}
