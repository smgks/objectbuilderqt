// Minimal self-checking tests for the core library (no test framework needed).
#include "core/FbxIO.h"
#include "core/Flags.h"
#include "core/LodObject.h"
#include "core/MeshOps.h"
#include "core/ObjIO.h"
#include "core/Odol.h"
#include "core/P3dIO.h"
#include "core/Paa.h"
#include "core/Primitives.h"
#include "core/TextureLibrary.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtMath>

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

static LodObject sampleModel()
{
    LodObject m;
    ObjectData &o = m.lods[0].data;
    Primitives::Params p;
    p.sizeX = 2;
    p.sizeY = 1;
    p.sizeZ = 3;
    Primitives::box(o, p, QMatrix4x4());
    o.faces[0].texture = QStringLiteral("data\\box_co.paa");
    o.faces[0].material = QStringLiteral("data\\box.rvmat");
    o.faces[1].flags = 0x00100000 | 0x10 | (5u << 25);
    o.points[0].flags = 0x1;
    o.namedProperties.append({QStringLiteral("autocenter"), QStringLiteral("0")});
    NamedSelection ns;
    ns.name = QStringLiteral("door");
    ns.sel.resize(o.points.size(), o.faces.size());
    ns.sel.points[0] = 255;
    ns.sel.points[1] = 128; // weighted
    ns.sel.faces[2] = 1;
    o.namedSelections.append(ns);
    o.sharpEdges.append({0, 1});
    o.hidden.resize(o.points.size(), o.faces.size());
    o.hidden.faces[3] = 1;
    UVSet second;
    second.id = 1;
    second.uv = o.uvSet0().uv;
    second.uv[0][0] = QVector2D(0.25f, 0.75f);
    o.uvSets.append(second);

    ObjectData geo;
    Primitives::Params q;
    Primitives::sphere(geo, q, QMatrix4x4());
    geo.mass = QVector<float>(geo.points.size(), 2.5f);
    m.addLod(LodNames::Geometry, geo);
    m.addLod(LodNames::Memory, ObjectData());
    return m;
}

static void testRoundTrip()
{
    const LodObject a = sampleModel();
    QBuffer buf;
    buf.open(QIODevice::ReadWrite);
    QString err;
    CHECK(P3dIO::save(buf, a, {}, &err));
    const QByteArray first = buf.data();
    CHECK(first.startsWith("MLOD"));

    buf.seek(0);
    LodObject b;
    CHECK(P3dIO::load(buf, b, &err));
    CHECK(b.lods.size() == 3);
    CHECK(qFuzzyCompare(b.lods[1].resolution, LodNames::Geometry));

    const ObjectData &o = b.lods[0].data;
    const ObjectData &src = a.lods[0].data;
    CHECK(o.points.size() == src.points.size());
    CHECK(o.faces.size() == src.faces.size());
    CHECK(o.faces[0].texture == QStringLiteral("data\\box_co.paa"));
    CHECK(o.faces[1].flags == src.faces[1].flags);
    CHECK(o.points[0].flags == 1);
    CHECK(o.property(QStringLiteral("autocenter")) == QStringLiteral("0"));
    const NamedSelection *ns = o.findSelection(QStringLiteral("door"));
    CHECK(ns && ns->sel.weight(0) == 255 && ns->sel.weight(1) == 128 && ns->sel.face(2));
    CHECK(o.sharpEdges.size() == 1);
    CHECK(o.hidden.face(3));
    CHECK(o.uvSets.size() == 2);
    CHECK(o.findUVSet(1) && o.findUVSet(1)->uv[0][0] == QVector2D(0.25f, 0.75f));
    CHECK(b.lods[1].data.mass.size() == b.lods[1].data.points.size());
    CHECK(qFuzzyCompare(b.lods[1].data.totalMass(), 2.5f * b.lods[1].data.points.size()));

    // Saving what we loaded must reproduce the bytes exactly.
    QBuffer buf2;
    buf2.open(QIODevice::WriteOnly);
    CHECK(P3dIO::save(buf2, b, {}, &err));
    CHECK(buf2.data() == first);
}

static void testBoxIsClosedAndOutward()
{
    ObjectData o;
    Primitives::box(o, {}, QMatrix4x4());
    CHECK(o.points.size() == 8);
    CHECK(o.faces.size() == 6);
    CHECK(MeshOps::selectNonClosed(o) == 0);
    for (int f = 0; f < o.faces.size(); ++f)
        CHECK(QVector3D::dotProduct(o.faceNormal(f), o.faceCenter(f)) > 0);
}

static void testPlaneFacesCreatingCamera()
{
    // A plane created from the front view must face the camera (-Z in model space).
    ObjectData o;
    Primitives::plane(o, {}, QMatrix4x4());
    CHECK(o.faces.size() == 1);
    CHECK(o.faceNormal(0).z() < -0.99f);
}

static void testDeleteAndMerge()
{
    ObjectData o;
    Primitives::Params p;
    p.segX = 2;
    p.segY = 2;
    Primitives::plane(o, p, QMatrix4x4());
    CHECK(o.points.size() == 9 && o.faces.size() == 4);
    o.selection.clear();
    o.selection.setPoint(4, 255); // centre point
    MeshOps::deleteSelection(o);
    CHECK(o.points.size() == 8);
    CHECK(o.faces.size() == 0);

    ObjectData q;
    Primitives::plane(q, {}, QMatrix4x4());
    q.selection.clear();
    q.selection.setPoint(0, 255);
    q.selection.setPoint(1, 255);
    MeshOps::mergeSelectedPoints(q);
    CHECK(q.points.size() == 3);
    CHECK(q.faces.size() == 1 && q.faces[0].n == 3);
}

static void testTriangulateSquarize()
{
    ObjectData o;
    Primitives::Params p;
    p.segX = 3;
    p.segY = 2;
    Primitives::plane(o, p, QMatrix4x4());
    MeshOps::triangulateSelected(o, true);
    CHECK(o.faces.size() == 12);
    for (int f = 0; f < o.faces.size(); ++f)
        CHECK(o.faceNormal(f).z() < -0.99f);
    for (int f = 0; f < o.faces.size(); ++f)
        o.selection.setFace(f, true);
    const int quads = MeshOps::squarizeSelected(o);
    CHECK(quads == 6);
    CHECK(o.faces.size() == 6);
}

static void testLodNames()
{
    CHECK(LodNames::name(1e13f) == QStringLiteral("Geometry"));
    CHECK(LodNames::name(1e15f) == QStringLiteral("Memory"));
    CHECK(LodNames::name(1000.0f) == QStringLiteral("View - Gunner"));
    CHECK(LodNames::name(10000.0f) == QStringLiteral("ShadowVolume 0.000"));
    CHECK(LodNames::name(1.5f) == QStringLiteral("1.500"));
    float r = 0;
    CHECK(LodNames::parse(QStringLiteral("ShadowVolume 10"), r) && qFuzzyCompare(r, 10010.0f));
    CHECK(LodNames::parse(QStringLiteral("Fire Geometry"), r) && qFuzzyCompare(r, 7e15f));
    CHECK(LodNames::parse(QStringLiteral("2.5"), r) && qFuzzyCompare(r, 2.5f));
}

static void testFbx()
{
    for (const char *name : {"test_binary.fbx", "test_ascii.fbx"}) {
        LodObject m;
        m.lods.clear();
        FbxIO::Report rep;
        QString err;
        CHECK(FbxIO::import(QStringLiteral(OB_TEST_DATA_DIR "/") + QLatin1String(name), m, {}, &rep, &err));
        CHECK(rep.meshes == 4 && rep.lods == 3);
        const int geo = m.find(LodNames::Geometry);
        CHECK(geo >= 0);
        if (geo < 0)
            continue;
        const ObjectData &g = m.lods[geo].data;
        CHECK(g.points.size() == 8 && g.faces.size() == 6);
        QVector3D mn, mx;
        g.bounds(mn, mx);
        CHECK(qFuzzyCompare(mx.x(), 0.5f) && qFuzzyCompare(mn.y(), -0.5f)); // cm -> m
        for (int f = 0; f < g.faces.size(); ++f)
            CHECK(QVector3D::dotProduct(g.faceNormal(f), g.faceCenter(f)) > 0); // outward
        const int l1 = m.find(1.0f);
        CHECK(l1 >= 0 && m.lods[l1].data.faces[0].texture == QStringLiteral("data\\box_co.paa"));
        CHECK(m.find(2.0f) >= 0); // LodGroup "LOD1" -> second detail level
    }
}

static void testFbxTextures()
{
    // Blender export with packed images (original paths do not exist): the
    // base colour image is written to <name>.fbm and used, the normal map is
    // not used as a face texture. A second import reuses the extracted file.
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString fbx = dir.filePath(QStringLiteral("test_blender_embedded.fbx"));
    CHECK(QFile::copy(QStringLiteral(OB_TEST_DATA_DIR "/test_blender_embedded.fbx"), fbx));
    for (int pass = 0; pass < 2; ++pass) {
        LodObject m;
        m.lods.clear();
        FbxIO::Report rep;
        QString err;
        CHECK(FbxIO::import(fbx, m, {}, &rep, &err));
        CHECK(rep.texturesFound == 1 && rep.texturesMissing.isEmpty());
        CHECK(rep.texturesExtracted.size() == (pass == 0 ? 1 : 0));
        if (m.lods.isEmpty() || m.lods[0].data.faces.isEmpty())
            continue;
        const ObjectData &o = m.lods[0].data;
        const QString expected = QDir::toNativeSeparators(dir.filePath(QStringLiteral("test_blender_embedded.fbm/cube_co.png")));
        for (const Face &f : o.faces)
            CHECK(f.texture.compare(expected, Qt::CaseInsensitive) == 0);
        const QImage img(expected);
        CHECK(img.width() == 8 && img.pixelColor(0, 0).red() > 250 && img.pixelColor(0, 0).blue() < 5);
    }
}

static void testFbxMaterialsInMod()
{
    // Blender export (tests/data/test_blender_materials.fbx) placed in an
    // unpacked mod: FixMod\$PBOPREFIX$ = "fixmod", FixMod\models\fixture.fbx,
    // texture at ..\data\cube_co.png with a converted cube_co.paa beside it.
    QTemporaryDir dir;
    QDir root(dir.path());
    CHECK(root.mkpath(QStringLiteral("FixMod/models")) && root.mkpath(QStringLiteral("FixMod/data")));
    QFile prefix(root.filePath(QStringLiteral("FixMod/$PBOPREFIX$")));
    CHECK(prefix.open(QIODevice::WriteOnly));
    prefix.write("fixmod\n");
    prefix.close();
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::blue);
    CHECK(img.save(root.filePath(QStringLiteral("FixMod/data/cube_co.png"))));
    QFile paa(root.filePath(QStringLiteral("FixMod/data/cube_co.paa")));
    CHECK(paa.open(QIODevice::WriteOnly));
    paa.write("converted");
    paa.close();
    const QString fbx = root.filePath(QStringLiteral("FixMod/models/fixture.fbx"));
    CHECK(QFile::copy(QStringLiteral(OB_TEST_DATA_DIR "/test_blender_materials.fbx"), fbx));

    LodObject m;
    m.lods.clear();
    FbxIO::Report rep;
    QString err;
    CHECK(FbxIO::import(fbx, m, {}, &rep, &err));
    CHECK(rep.meshes == 3 && rep.texturesMissing.isEmpty());
    if (m.lods.isEmpty())
        return;
    const ObjectData &o = m.lods[0].data;
    int cubeFaces = 0;
    for (int f = 0; f < o.faces.size(); ++f) {
        if (o.faces[f].texture.isEmpty())
            continue;
        ++cubeFaces;
        // game path inside the mod, the .paa preferred over the .png
        CHECK(o.faces[f].texture == QStringLiteral("fixmod\\data\\cube_co.paa"));
        // "cube.rvmat.001" (Blender's duplicate name) is the same material
        CHECK(o.faces[f].material == QStringLiteral("fixmod\\data\\cube.rvmat"));
    }
    CHECK(cubeFaces == 12);
    const NamedSelection *px = o.findSelection(QStringLiteral("proxy:\\fixmod\\proxy\\thing.001"));
    CHECK(px && px->sel.faceCount() == 1 && px->sel.pointCount() == 3);
    if (px)
        for (int f = 0; f < o.faces.size(); ++f)
            if (px->sel.face(f)) {
                CHECK(o.faces[f].texture.isEmpty() && o.faces[f].material.isEmpty());
                for (int k = 0; k < o.faces[f].n; ++k)
                    CHECK(o.points[o.faces[f].v[k].point].flags & PointFlags::Hidden);
            }
}

static void testModTextureLookup()
{
    // Unpacked mods: textures are referenced from the mod root. Case 1: the
    // folder name is the prefix. Case 2: a $PBOPREFIX$ file maps a longer prefix.
    QTemporaryDir dir;
    QDir root(dir.path());
    QImage img(4, 2, QImage::Format_RGB32);
    img.fill(Qt::green);
    CHECK(root.mkpath(QStringLiteral("StalkerX/rifles/ak74/data")) && root.mkpath(QStringLiteral("StalkerX/rifles/ak103")));
    CHECK(img.save(root.filePath(QStringLiteral("StalkerX/rifles/ak74/data/ak_co.png"))));
    CHECK(root.mkpath(QStringLiteral("weapons/data")) && root.mkpath(QStringLiteral("weapons/models/gun")));
    CHECK(img.save(root.filePath(QStringLiteral("weapons/data/gun_co.png"))));
    QFile prefix(root.filePath(QStringLiteral("weapons/$PBOPREFIX$")));
    CHECK(prefix.open(QIODevice::WriteOnly));
    prefix.write("my\\mod\\weapons\n");
    prefix.close();

    TextureLibrary &lib = TextureLibrary::instance();
    lib.setModelFolder(root.filePath(QStringLiteral("StalkerX/rifles/ak103")));
    CHECK(lib.image(QStringLiteral("stalkerx\\rifles\\ak74\\data\\ak_co.png")).width() == 4);
    lib.setModelFolder(root.filePath(QStringLiteral("weapons/models/gun")));
    CHECK(lib.image(QStringLiteral("my\\mod\\weapons\\data\\gun_co.png")).width() == 4);
    CHECK(lib.image(QStringLiteral("other\\mod\\data\\gun_co.png")).isNull());
    lib.setModelFolder(QString());
    lib.clear();
}

static void testFaceNormalMatchesOriginal()
{
    // 0x5d3f00: the stored normal of a triangle is (p1 - p0) x (p2 - p0); the
    // front side is the opposite one.
    ObjectData o;
    o.addPoint({0, 0, 0});
    o.addPoint({0, 1, 0});
    o.addPoint({1, 0, 0});
    Face f;
    f.n = 3;
    for (int k = 0; k < 3; ++k)
        f.v[k].point = k;
    o.addFace(f);
    CHECK(o.storedNormal(0).z() < -0.99f);
    CHECK(o.faceNormal(0).z() > 0.99f);
    o.recalculateNormals();
    CHECK(o.normals[o.faces[0].v[0].normal].z() < -0.99f);
}

static void testObjWinding()
{
    // An outward P3D face must be counter-clockwise (outward) in the
    // right-handed OBJ file, and come back outward on import.
    ObjectData box;
    Primitives::box(box, {}, QMatrix4x4());
    box.faces[1].texture = QStringLiteral("data\\box_co.paa");
    const QString path = QDir::temp().filePath(QStringLiteral("ob_winding_test.obj"));
    CHECK(ObjIO::exportLod(path, box));
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QVector<QVector3D> v;
    int outward = 0, faces = 0;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QList<QByteArray> t = line.simplified().split(' ');
        if (t.size() >= 4 && t[0] == "v")
            v.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
        if (t.size() >= 4 && t[0] == "f") {
            QVector3D p[3];
            QVector3D c;
            for (int k = 0; k < 3; ++k)
                p[k] = v[t[k + 1].split('/')[0].toInt() - 1];
            for (int k = 1; k < t.size(); ++k)
                c += v[t[k].split('/')[0].toInt() - 1];
            c /= float(t.size() - 1);
            outward += QVector3D::dotProduct(QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]), c) > 0;
            ++faces;
        }
    }
    file.close();
    CHECK(faces == 6 && outward == 6);
    ObjectData back;
    CHECK(ObjIO::import(path, back));
    CHECK(back.faces.size() == 6);
    for (int f = 0; f < back.faces.size(); ++f) {
        CHECK(QVector3D::dotProduct(back.faceNormal(f), back.faceCenter(f)) > 0);
        CHECK(back.faces[f].texture == box.faces[f].texture); // via usemtl
        CHECK((back.uv(f, 0) - box.uv(f, 0)).length() < 1e-5f);
    }
    QFile::remove(path);
}

static void testOdolKeepsWinding()
{
    // binarize_box_src.p3d binarized by DayZ Tools' binarize.exe gives
    // binarize_box_odol.p3d (binarize reverses every face). The converted
    // faces must face the same way as the source faces, with the same normals.
    LodObject src, bin;
    QString err;
    CHECK(P3dIO::load(QStringLiteral(OB_TEST_DATA_DIR "/binarize_box_src.p3d"), src, &err));
    QFile f(QStringLiteral(OB_TEST_DATA_DIR "/binarize_box_odol.p3d"));
    CHECK(f.open(QIODevice::ReadOnly));
    CHECK(Odol::load(f.readAll(), bin, &err));
    const int a = src.find(1.0f), b = bin.find(1.0f);
    CHECK(a >= 0 && b >= 0);
    if (a < 0 || b < 0)
        return;
    const ObjectData &s = src.lods[a].data, &o = bin.lods[b].data;
    QVector3D smn, smx, omn, omx;
    s.bounds(smn, smx);
    o.bounds(omn, omx);
    const QVector3D shift = omn - smn; // binarize re-centres the model
    int matched = 0;
    for (int i = 0; i < o.faces.size(); ++i) {
        for (int j = 0; j < s.faces.size(); ++j) {
            if ((s.faceCenter(j) + shift - o.faceCenter(i)).length() > 1e-3f)
                continue;
            CHECK(QVector3D::dotProduct(s.faceNormal(j), o.faceNormal(i)) > 0.99f);
            QVector3D sn, on;
            for (int k = 0; k < s.faces[j].n; ++k)
                sn += s.normals[s.faces[j].v[k].normal];
            for (int k = 0; k < o.faces[i].n; ++k)
                on += o.normals[o.faces[i].v[k].normal];
            CHECK(QVector3D::dotProduct(sn.normalized(), on.normalized()) > 0.99f);
            ++matched;
        }
    }
    CHECK(matched == s.faces.size() && matched == o.faces.size());
}

static void testOdolKeepsBonePartsSeparate()
{
    // binarize_bones_src.p3d (+ binarize_bones_model.cfg) binarized by
    // binarize.exe: two boxes touching face to face, each its own bone. The
    // converter must not weld the touching corners (the bones would drag each
    // other's faces), and must rebuild both bone selections.
    QFile f(QStringLiteral(OB_TEST_DATA_DIR "/binarize_bones_odol.p3d"));
    CHECK(f.open(QIODevice::ReadOnly));
    LodObject m;
    QString err;
    Odol::Info info;
    CHECK(Odol::load(f.readAll(), m, &err, &info));
    CHECK(info.bones == QStringList({QStringLiteral("part_a"), QStringLiteral("part_b")}));
    if (m.lods.isEmpty())
        return;
    const ObjectData &o = m.lods[0].data;
    CHECK(o.points.size() == 16 && o.faces.size() == 12);
    for (const char *name : {"part_a", "part_b"}) {
        const NamedSelection *ns = o.findSelection(QLatin1String(name));
        CHECK(ns && ns->sel.pointCount() == 8 && ns->sel.faceCount() == 6);
        if (!ns)
            continue;
        for (int fi = 0; fi < o.faces.size(); ++fi)
            if (!ns->sel.face(fi))
                for (int k = 0; k < o.faces[fi].n; ++k)
                    CHECK(!ns->sel.point(o.faces[fi].v[k].point)); // no point shared with the other part
    }
}

static void testPaaProcedural()
{
    const QImage img = Paa::procedural(QStringLiteral("#(argb,8,8,3)color(1,0,0,1,co)"));
    CHECK(img.width() == 8 && img.height() == 8);
    CHECK(img.pixelColor(0, 0) == QColor(255, 0, 0));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QByteArray(argv[1]) == "--write-bones-sample") {
        // Source of tests/data/binarize_bones_odol.p3d: two unit boxes that
        // touch face to face, each a bone ("part_a", "part_b").
        LodObject m;
        ObjectData &o = m.lods[0].data;
        for (int part = 0; part < 2; ++part) {
            const int firstPoint = o.points.size(), firstFace = o.faces.size();
            QMatrix4x4 at;
            at.translate(float(part), 0, 0);
            Primitives::box(o, {}, at);
            NamedSelection ns;
            ns.name = part == 0 ? QStringLiteral("part_a") : QStringLiteral("part_b");
            ns.sel.resize(o.points.size(), o.faces.size());
            for (int i = firstPoint; i < o.points.size(); ++i)
                ns.sel.points[i] = 255;
            for (int f = firstFace; f < o.faces.size(); ++f)
                ns.sel.faces[f] = 1;
            o.namedSelections.append(ns);
        }
        for (NamedSelection &ns : o.namedSelections)
            ns.sel.resize(o.points.size(), o.faces.size());
        o.selection.clear();
        for (Face &f : o.faces)
            f.texture = QStringLiteral("#(argb,8,8,3)color(0.5,0.5,0.5,1,co)");
        QString err;
        if (!P3dIO::save(QString::fromLocal8Bit(argv[2]), m, {}, &err)) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return 1;
        }
        return 0;
    }
    if (argc == 3 && QByteArray(argv[1]) == "--write-sample") {
        QString err;
        if (!P3dIO::save(QString::fromLocal8Bit(argv[2]), sampleModel(), {}, &err)) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return 1;
        }
        return 0;
    }
    testRoundTrip();
    testBoxIsClosedAndOutward();
    testPlaneFacesCreatingCamera();
    testDeleteAndMerge();
    testTriangulateSquarize();
    testLodNames();
    testFbx();
    testFbxTextures();
    testModTextureLookup();
    testFbxMaterialsInMod();
    testFaceNormalMatchesOriginal();
    testObjWinding();
    testOdolKeepsWinding();
    testOdolKeepsBonePartsSeparate();
    testPaaProcedural();
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::printf("all core tests passed\n");
    return g_failures ? 1 : 0;
}
