#include "Odol.h"

#include "Compression.h"
#include "Flags.h"

#include <QtEndian>
#include <QHash>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

// Reconstructed from the engine's LODShape/Shape SerializeBin in DayZ Tools'
// binarize.exe (functions 0x140937300, 0x14093ad50, 0x140938ca0 and helpers;
// see docs/RE_NOTES.md section 8). Supports ODOL versions 28..55 as the engine
// does; DayZ ships version 54.

namespace {

struct Reader {
    const quint8 *d = nullptr;
    qsizetype n = 0;
    qsizetype p = 0;
    bool fail = false;
    int version = 0;
    QString error;

    bool need(qsizetype k)
    {
        if (fail || p + k > n || k < 0) {
            if (!fail)
                error = QStringLiteral("Unexpected end of data at offset %1").arg(p);
            fail = true;
            return false;
        }
        return true;
    }
    quint8 u8()
    {
        if (!need(1))
            return 0;
        return d[p++];
    }
    quint16 u16()
    {
        if (!need(2))
            return 0;
        const quint16 v = quint16(d[p] | (d[p + 1] << 8));
        p += 2;
        return v;
    }
    quint32 u32()
    {
        if (!need(4))
            return 0;
        quint32 v;
        std::memcpy(&v, d + p, 4);
        p += 4;
        return v;
    }
    qint32 i32() { return qint32(u32()); }
    float f32()
    {
        const quint32 u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    QVector3D vec3()
    {
        const float x = f32(), y = f32(), z = f32();
        return QVector3D(x, y, z);
    }
    void skip(qsizetype k)
    {
        if (need(k))
            p += k;
    }
    QString asciiz()
    {
        // The engine stops at 0 (and 0xff) - serializebin.cpp
        const qsizetype start = p;
        while (p < n && d[p] != 0 && d[p] != 0xff)
            ++p;
        if (p >= n) {
            fail = true;
            error = QStringLiteral("Unterminated string at offset %1").arg(start);
            return {};
        }
        const QString s = QString::fromLocal8Bit(reinterpret_cast<const char *>(d + start), p - start);
        ++p;
        return s;
    }
    qint32 count(const char *what, qint32 limit = 50000000)
    {
        const qint32 c = i32();
        if (!fail && (c < 0 || c > limit)) {
            fail = true;
            error = QStringLiteral("Bad %1 count %2 at offset %3").arg(QLatin1String(what)).arg(c).arg(p - 4);
        }
        return fail ? 0 : c;
    }

    // SerializeBinStream compressed block (0x1409da470): raw below 1024 bytes,
    // otherwise LZO (version >= 44) or LZSS.
    QByteArray block(qsizetype bytes)
    {
        if (fail || bytes < 0)
            return {};
        if (bytes < 1024) {
            if (!need(bytes))
                return {};
            QByteArray out(reinterpret_cast<const char *>(d + p), bytes);
            p += bytes;
            return out;
        }
        QByteArray out;
        qsizetype used = 0;
        const bool ok = version >= 44 ? Compression::lzo1x(reinterpret_cast<const char *>(d + p), n - p, bytes, out, &used)
                                      : Compression::lzss(reinterpret_cast<const char *>(d + p), n - p, bytes, out, &used, true);
        if (!ok) {
            fail = true;
            error = QStringLiteral("Decompression failed at offset %1 (%2 bytes)").arg(p).arg(bytes);
            return {};
        }
        p += used;
        return out;
    }
    // count + compressed block of count*elem bytes
    QByteArray array(int elem, const char *what)
    {
        const qint32 c = count(what);
        return block(qsizetype(c) * elem);
    }
    // Condensed array (0x140933600 / 0x140933af0 / 0x14093c770): count, then a
    // flag byte; non-zero = one default element for all, else a block.
    QByteArray condensed(int elem, const char *what, qint32 *outCount)
    {
        const qint32 c = count(what);
        *outCount = c;
        const quint8 isDefault = u8();
        if (fail)
            return {};
        if (isDefault) {
            if (!need(elem))
                return {};
            QByteArray one(reinterpret_cast<const char *>(d + p), elem);
            p += elem;
            QByteArray all;
            all.reserve(qsizetype(c) * elem);
            for (qint32 i = 0; i < c; ++i)
                all += one;
            return all;
        }
        return block(qsizetype(c) * elem);
    }
};

template <typename T>
T at(const QByteArray &b, qsizetype i)
{
    T v;
    std::memcpy(&v, b.constData() + i * qsizetype(sizeof(T)), sizeof(T));
    return v;
}

QVector3D unpackNormal(quint32 v)
{
    auto s10 = [](quint32 x) { return int(x & 0x3ff) - ((x & 0x200) ? 1024 : 0); };
    const float k = -1.0f / 511.0f; // 0x140c96460
    return QVector3D(s10(v) * k, s10(v >> 10) * k, s10(v >> 20) * k);
}

// ---------------------------------------------------------------- materials

void skipColor(Reader &r)
{
    r.skip(16);
}

// TexMaterial::SerializeBin (0x140907ee0). Returns the texture of the first
// stage (usually the normal/detail map) for information only.
void readMaterial(Reader &r)
{
    const qint32 mv = r.i32();
    if (r.fail)
        return;
    if (mv > 20 || mv == 8) {
        r.fail = true;
        r.error = QStringLiteral("Unsupported embedded material version %1").arg(mv);
        return;
    }
    for (int i = 0; i < 5; ++i)
        skipColor(r); // emissive, ambient, diffuse, forcedDiffuse, specular
    if (mv >= 12)
        skipColor(r);
    if (mv >= 13)
        skipColor(r);
    skipColor(r);
    r.skip(4); // specular power
    if (mv >= 14)
        r.skip(4);
    if (mv >= 15)
        r.skip(4);
    if (mv >= 16)
        skipColor(r);
    if (mv >= 17)
        r.skip(4);
    if (mv >= 18)
        r.skip(24);
    if (mv >= 19)
        r.skip(16);
    if (mv >= 20)
        r.skip(4);
    r.skip(8); // pixel / vertex shader
    r.skip(8); // main light, fog mode
    if (mv == 3)
        r.skip(1);
    if (mv >= 6)
        r.asciiz(); // surface info
    if (mv >= 4) {
        const qint32 c = r.count("render flags", 1000);
        r.skip(qsizetype(c) * 4);
    }
    qint32 stages = 4, texGens = 4;
    if (mv > 6) {
        stages = r.count("material stages", 64);
        texGens = mv >= 8 ? r.count("material texgens", 64) : stages;
    }
    const int first = mv < 2 ? 1 : 0;
    auto stage = [&] {
        if (mv >= 5)
            r.skip(4); // filter
        r.asciiz();     // texture
        if (mv > 7)
            r.skip(4); // texgen index
        if (mv > 10)
            r.skip(1);
    };
    auto texGen = [&] { r.skip(4 + 48); }; // uv source + Matrix4P
    if (mv < 8) {
        for (int i = first; i < stages && !r.fail; ++i) {
            texGen();
            stage();
        }
    } else {
        for (int i = first; i < stages && !r.fail; ++i)
            stage();
        for (int i = first; i < texGens && !r.fail; ++i)
            texGen();
    }
    if (mv > 9)
        stage(); // TI stage
}

// ---------------------------------------------------------------- LOD

struct ModelInfo {
    float mass = 0;
    QVector<float> massArray;
    QStringList bones; // skeleton bone names (ModelInfo skeleton), index = bone index
};


struct OdolLod {
    struct Proxy {
        QString name;
        float m[12];
        qint32 seq = 0, selection = -1, bone = -1, section = -1;
    };
    struct Section {
        qint32 beg = 0, end = 0;
        qint16 texture = -1;
        qint32 material = -1;
        QString materialName;
        quint32 special = 0;
    };
    struct NSel {
        QString name;
        QVector<quint16> faces;
        bool sectional = false;
        QVector<qint32> sections;
        QVector<quint16> vertices;
        QByteArray weights;
    };
    QVector<Proxy> proxies;
    QVector<qint32> subSkeletonToSkeleton; // sub-skeleton index -> skeleton bone index
    QByteArray vertexBoneRefs;             // 12 bytes per vertex: u32 count, 4 x (u8 sub-skeleton index, u8 weight)
    QStringList textures, materials;
    QVector<quint16> pointToVertex, vertexToPoint;
    QVector<QVector<quint16>> faces;
    QVector<Section> sections;
    QVector<NSel> selections;
    QVector<QPair<QString, QString>> properties;
    QVector<QPair<float, QVector<QVector3D>>> frames;
    QVector<QVector2D> uv0;
    QVector<QVector<QVector2D>> uvExtra;
    QVector<QVector3D> positions, normals;
};

QVector<QVector2D> readUVSet(Reader &r)
{
    const float f2 = r.f32(), f3 = r.f32(), f0 = r.f32(), f1 = r.f32();
    qint32 c = 0;
    const QByteArray raw = r.condensed(4, "uv", &c);
    QVector<QVector2D> out(c);
    if (r.fail || raw.size() < qsizetype(c) * 4)
        return out;
    // FUN_1409401c0 / FUN_1409402c0
    const float su = (f0 - f2) / 65534.0f, sv = (f1 - f3) / 65534.0f;
    for (qint32 i = 0; i < c; ++i) {
        const qint16 u = at<qint16>(raw, 2 * i), v = at<qint16>(raw, 2 * i + 1);
        out[i] = QVector2D((u + 32767.0f) * su + f2, (v + 32767.0f) * sv + f3);
    }
    return out;
}

QVector<quint16> toU16(const QByteArray &b)
{
    QVector<quint16> v(b.size() / 2);
    std::memcpy(v.data(), b.constData(), v.size() * 2);
    return v;
}

bool readLod(Reader &r, OdolLod &L)
{
    const int v = r.version;
    // proxies (0x140933c70 -> 0x140938b70)
    for (qint32 i = 0, c = r.count("proxies", 100000); i < c && !r.fail; ++i) {
        OdolLod::Proxy px;
        px.name = r.asciiz();
        for (float &f : px.m)
            f = r.f32();
        px.seq = r.i32();
        px.selection = r.i32();
        px.bone = r.i32();
        if (v > 39)
            px.section = r.i32();
        L.proxies.append(px);
    }
    // sub-skeleton maps
    for (qint32 i = 0, c = r.count("subskeleton"); i < c && !r.fail; ++i)
        L.subSkeletonToSkeleton.append(r.i32()); // stored uncompressed
    for (qint32 i = 0, c = r.count("skeleton map"); i < c && !r.fail; ++i)
        r.skip(qsizetype(r.count("skeleton map entry")) * 4);
    if (v >= 50)
        r.u32(); // vertex count
    if (v >= 51)
        r.f32(); // face area
    r.u32();     // or hints
    r.u32();     // and hints
    r.vec3();    // bmin
    r.vec3();    // bmax
    r.vec3();    // center
    r.f32();     // radius
    for (qint32 i = 0, c = r.count("textures", 100000); i < c && !r.fail; ++i)
        L.textures.append(r.asciiz());
    for (qint32 i = 0, c = r.count("materials", 100000); i < c && !r.fail; ++i) {
        L.materials.append(r.asciiz());
        readMaterial(r);
    }
    L.pointToVertex = toU16(r.array(2, "point to vertex"));
    L.vertexToPoint = toU16(r.array(2, "vertex to point"));
    // faces
    const qint32 nFaces = r.count("faces");
    r.u32(); // allocated face data size
    r.u16();
    L.faces.reserve(nFaces);
    for (qint32 i = 0; i < nFaces && !r.fail; ++i) {
        const int n = r.u8();
        QVector<quint16> f(n);
        for (int k = 0; k < n; ++k)
            f[k] = r.u16();
        L.faces.append(f);
    }
    // sections (0x14093a6b0)
    for (qint32 i = 0, c = r.count("sections", 100000); i < c && !r.fail; ++i) {
        OdolLod::Section s;
        s.beg = r.i32();
        s.end = r.i32();
        r.i32(); // min bone
        r.i32(); // bones count
        r.u32();
        s.texture = qint16(r.u16());
        s.special = r.u32();
        s.material = r.i32();
        if (s.material < 0 || s.material >= L.materials.size()) {
            s.materialName = r.asciiz();
            s.material = -1;
        }
        if (v >= 36)
            r.skip(qsizetype(r.count("section areas", 64)) * 4);
        else
            r.skip(4);
        L.sections.append(s);
    }
    // named selections (0x1408a0dc0)
    for (qint32 i = 0, c = r.count("named selections", 100000); i < c && !r.fail; ++i) {
        OdolLod::NSel s;
        s.name = r.asciiz();
        s.faces = toU16(r.array(2, "selection faces"));
        r.array(1, "selection face weights");
        s.sectional = r.u8() != 0;
        const QByteArray secs = r.array(4, "selection sections");
        for (qsizetype k = 0; k + 4 <= secs.size(); k += 4)
            s.sections.append(at<qint32>(secs, k / 4));
        s.vertices = toU16(r.array(2, "selection vertices"));
        s.weights = r.array(1, "selection weights");
        L.selections.append(s);
    }
    for (qint32 i = 0, c = r.count("properties", 100000); i < c && !r.fail; ++i) {
        const QString a = r.asciiz();
        const QString b = r.asciiz();
        L.properties.append({a, b});
    }
    for (qint32 i = 0, c = r.count("frames", 100000); i < c && !r.fail; ++i) {
        const float t = r.f32();
        QVector<QVector3D> pts(r.count("frame points"));
        for (QVector3D &p : pts)
            p = r.vec3();
        L.frames.append({t, pts});
    }
    r.u32(); // color top
    r.u32(); // color
    r.u32(); // special
    r.u8();  // vertex bone ref is simple
    const qsizetype restStart = r.p; // size counts from its own field (0x140938ca0)
    const quint32 restSize = r.u32();
    // vertex data
    qint32 c = 0;
    if (v >= 50)
        r.condensed(4, "clip flags", &c);
    L.uv0 = readUVSet(r);
    const qint32 nUV = r.count("uv sets", 64);
    for (qint32 i = 1; i < nUV && !r.fail; ++i)
        L.uvExtra.append(readUVSet(r));
    const QByteArray pos = r.array(12, "positions");
    L.positions.resize(pos.size() / 12);
    for (int i = 0; i < L.positions.size(); ++i)
        L.positions[i] = QVector3D(at<float>(pos, 3 * i), at<float>(pos, 3 * i + 1), at<float>(pos, 3 * i + 2));
    const QByteArray nrm = r.condensed(4, "normals", &c);
    L.normals.resize(nrm.size() / 4);
    for (int i = 0; i < L.normals.size(); ++i)
        L.normals[i] = unpackNormal(at<quint32>(nrm, i));
    r.array(8, "st coords");
    L.vertexBoneRefs = r.array(12, "vertex bone refs");
    r.array(32, "neighbour bone refs");
    if (!r.fail && restSize && r.p != restStart + qsizetype(restSize)) {
        // trust the stored size (newer fields we do not know about)
        r.p = restStart + restSize;
    }
    return !r.fail;
}

// ---------------------------------------------------------------- conversion

void convertLod(const OdolLod &L, ObjectData &o, QStringList &warnings, const ModelInfo *mass = nullptr,
                const QStringList *bones = nullptr)
{
    o = ObjectData();
    const int nV = L.positions.size();
    // points: collapse vertices that share an original point
    QVector<int> vToP(nV);
    int nP = 0;
    if (L.vertexToPoint.size() == nV) {
        for (int i = 0; i < nV; ++i) {
            vToP[i] = L.vertexToPoint[i];
            nP = std::max(nP, vToP[i] + 1);
        }
    } else {
        // Binarized LODs usually drop the point map; vertices are split at UV
        // seams and hard edges. Weld identical positions back into shared
        // points like an editable model has them.
        QHash<QByteArray, int> seen;
        for (int i = 0; i < nV; ++i) {
            const QByteArray key(reinterpret_cast<const char *>(&L.positions[i]), sizeof(QVector3D));
            auto it = seen.constFind(key);
            if (it != seen.constEnd()) {
                vToP[i] = *it;
            } else {
                vToP[i] = nP;
                seen.insert(key, nP++);
            }
        }
    }
    o.points.resize(nP);
    QVector<bool> set(nP, false);
    for (int i = 0; i < nV; ++i)
        if (!set[vToP[i]]) {
            o.points[vToP[i]].pos = L.positions[i];
            set[vToP[i]] = true;
        }
    // binarize copies the MLOD vertex normals unchanged (same direction).
    o.normals = L.normals;
    if (o.normals.size() < nV)
        o.normals.resize(nV);

    // face -> section (texture/material). Sections address faces either by
    // index or by offset into the face data (2 + 2n bytes per face).
    QVector<int> faceSection(L.faces.size(), -1);
    if (!L.sections.isEmpty()) {
        const bool byIndex = L.sections.last().end <= L.faces.size();
        QVector<qint32> offsets(L.faces.size() + 1, 0);
        for (int f = 0; f < L.faces.size(); ++f)
            offsets[f + 1] = offsets[f] + (byIndex ? 1 : 2 + 2 * int(L.faces[f].size()));
        for (int s = 0; s < L.sections.size(); ++s) {
            const auto &sec = L.sections[s];
            const int a = int(std::lower_bound(offsets.begin(), offsets.end(), sec.beg) - offsets.begin());
            const int b = int(std::lower_bound(offsets.begin(), offsets.end(), sec.end) - offsets.begin());
            for (int f = a; f < b && f < faceSection.size(); ++f)
                faceSection[f] = s;
        }
    }

    for (int f = 0; f < L.faces.size(); ++f) {
        QVector<quint16> vi = L.faces[f];
        if (vi.size() < 3)
            continue;
        // binarize reverses each face's vertex order (an MLOD box binarized
        // with binarize.exe comes back with every face reversed), so undo it.
        std::reverse(vi.begin(), vi.end());
        QVector<QVector<quint16>> parts;
        if (vi.size() <= 4)
            parts.append(vi);
        else
            for (int k = 1; k + 1 < vi.size(); ++k)
                parts.append({vi[0], vi[k], vi[k + 1]});
        for (const auto &part : parts) {
            Face fc;
            fc.n = part.size();
            std::array<QVector2D, 4> uv{};
            bool ok = true;
            for (int k = 0; k < fc.n; ++k) {
                const int vx = part[k];
                if (vx >= nV) {
                    ok = false;
                    break;
                }
                fc.v[k].point = vToP[vx];
                fc.v[k].normal = vx;
                if (vx < L.uv0.size())
                    uv[k] = L.uv0[vx];
            }
            if (!ok)
                continue;
            const int s = faceSection[f];
            if (s >= 0) {
                const auto &sec = L.sections[s];
                if (sec.texture >= 0 && sec.texture < L.textures.size())
                    fc.texture = L.textures[sec.texture];
                if (sec.material >= 0 && sec.material < L.materials.size())
                    fc.material = L.materials[sec.material];
                else if (!sec.materialName.isEmpty())
                    fc.material = sec.materialName;
                fc.flags = sec.special & 0x00300000; // keep lighting bits only
            }
            const int fi = o.addFace(fc, uv);
            Q_UNUSED(fi)
        }
    }
    // Sharp edges: binarize splits vertices along hard edges, so an edge shared
    // by faces whose vertex normals differ at either end was sharp in MLOD.
    {
        struct EdgeUse {
            QVector3D na, nb; // normals at the lower / higher point index
        };
        QHash<quint64, QVector<EdgeUse>> uses;
        for (const Face &fc : o.faces)
            for (int k = 0; k < fc.n; ++k) {
                const FaceVertex &a = fc.v[k], &b = fc.v[(k + 1) % fc.n];
                if (a.point == b.point)
                    continue;
                const bool swap = a.point > b.point;
                const quint64 key = (quint64(swap ? b.point : a.point) << 32) | quint32(swap ? a.point : b.point);
                const QVector3D na = a.normal < o.normals.size() ? o.normals[a.normal] : QVector3D();
                const QVector3D nb = b.normal < o.normals.size() ? o.normals[b.normal] : QVector3D();
                uses[key].append(swap ? EdgeUse{nb, na} : EdgeUse{na, nb});
            }
        for (auto it = uses.cbegin(); it != uses.cend(); ++it) {
            const auto &u = it.value();
            bool sharp = false;
            for (int i = 1; i < u.size() && !sharp; ++i)
                sharp = (u[i].na - u[0].na).lengthSquared() > 1e-4f || (u[i].nb - u[0].nb).lengthSquared() > 1e-4f;
            if (sharp)
                o.sharpEdges.append({int(it.key() >> 32), int(it.key() & 0xffffffff)});
        }
        std::sort(o.sharpEdges.begin(), o.sharpEdges.end());
    }

    // extra UV sets
    for (int s = 0; s < L.uvExtra.size(); ++s) {
        UVSet set;
        set.id = quint32(s + 1);
        set.uv.resize(o.faces.size());
        o.uvSets.append(set);
    }
    if (!L.uvExtra.isEmpty()) {
        // re-walk faces in the same order to fill extra sets
        int fi = 0;
        for (int f = 0; f < L.faces.size(); ++f) {
            QVector<quint16> vi = L.faces[f];
            if (vi.size() < 3)
                continue;
            std::reverse(vi.begin(), vi.end()); // see the first walk
            QVector<QVector<quint16>> parts;
            if (vi.size() <= 4)
                parts.append(vi);
            else
                for (int k = 1; k + 1 < vi.size(); ++k)
                    parts.append({vi[0], vi[k], vi[k + 1]});
            for (const auto &part : parts) {
                bool ok = true;
                for (quint16 x : part)
                    ok &= x < nV;
                if (!ok)
                    continue;
                for (int s = 0; s < L.uvExtra.size(); ++s)
                    for (int k = 0; k < part.size(); ++k)
                        if (part[k] < L.uvExtra[s].size())
                            o.uvSets[s + 1].uv[fi][k] = L.uvExtra[s][part[k]];
                ++fi;
            }
        }
    }

    // named selections
    QVector<QVector<int>> facesOfSection(L.sections.size());
    for (int f = 0; f < faceSection.size(); ++f)
        if (faceSection[f] >= 0)
            facesOfSection[faceSection[f]].append(f);
    // ODOL face index -> first MLOD face (n-gons were split)
    QVector<int> firstFace(L.faces.size() + 1, 0);
    {
        int acc = 0;
        for (int f = 0; f < L.faces.size(); ++f) {
            firstFace[f] = acc;
            const int n = L.faces[f].size();
            acc += n < 3 ? 0 : (n <= 4 ? 1 : n - 2);
        }
        firstFace[L.faces.size()] = acc;
    }
    for (const auto &s : L.selections) {
        NamedSelection ns;
        ns.name = s.name;
        ns.sel.resize(o.points.size(), o.faces.size());
        for (int i = 0; i < s.vertices.size(); ++i) {
            const int vx = s.vertices[i];
            if (vx >= nV)
                continue;
            quint8 w = 255;
            if (i < s.weights.size() && quint8(s.weights[i]) != 0)
                w = quint8(s.weights[i]);
            ns.sel.points[vToP[vx]] = std::max(ns.sel.points[vToP[vx]], w);
        }
        auto addFace = [&](int f) {
            if (f < 0 || f >= L.faces.size())
                return;
            for (int k = firstFace[f]; k < firstFace[f + 1] && k < o.faces.size(); ++k)
                ns.sel.faces[k] = 1;
        };
        for (quint16 f : s.faces)
            addFace(f);
        if (s.sectional)
            for (qint32 sec : s.sections)
                if (sec >= 0 && sec < facesOfSection.size())
                    for (int f : facesOfSection[sec])
                        addFace(f);
        // Bone selections: binarize drops their vertex lists and keeps the
        // membership in the per-vertex bone table instead.
        const int bone = ns.sel.pointCount() == 0 && bones ? int(bones->indexOf(s.name, 0, Qt::CaseInsensitive)) : -1;
        if (bone >= 0 && L.vertexBoneRefs.size() == qsizetype(nV) * 12) {
            const auto *refs = reinterpret_cast<const quint8 *>(L.vertexBoneRefs.constData());
            for (int vx = 0; vx < nV; ++vx) {
                const quint8 *e = refs + 12 * vx;
                const quint32 n = qFromLittleEndian<quint32>(e);
                for (quint32 k = 0; k < n && k < 4; ++k) {
                    const int sub = e[4 + 2 * k];
                    const quint8 w = e[5 + 2 * k];
                    if (sub < L.subSkeletonToSkeleton.size() && L.subSkeletonToSkeleton[sub] == bone && w)
                        ns.sel.points[vToP[vx]] = std::max(ns.sel.points[vToP[vx]], w);
                }
            }
            // faces whose points all belong to the bone
            for (int f = 0; f < o.faces.size(); ++f) {
                bool all = true;
                for (int k = 0; k < o.faces[f].n && all; ++k)
                    all = ns.sel.points[o.faces[f].v[k].point] != 0;
                if (all)
                    ns.sel.faces[f] = 1;
            }
        }
        // Face-only selections (e.g. hiddenSelections): an editable model
        // selects the faces' points too.
        if (ns.sel.pointCount() == 0)
            for (int f = 0; f < o.faces.size(); ++f)
                if (ns.sel.faces[f])
                    for (int k = 0; k < o.faces[f].n; ++k)
                        ns.sel.points[o.faces[f].v[k].point] = 255;
        o.namedSelections.append(ns);
    }

    for (const auto &p : L.properties)
        o.namedProperties.append({p.first, p.second});

    // keyframe animation phases are stored per vertex
    for (const auto &fr : L.frames) {
        AnimationPhase ph;
        ph.time = fr.first;
        ph.points.resize(o.points.size());
        for (int i = 0; i < fr.second.size() && i < nV; ++i)
            ph.points[vToP[i]] = fr.second[i];
        o.animations.append(ph);
    }

    // proxies: rebuild the MLOD proxy triangle (pin, pin+2*up, pin+dir, as
    // CICreateProxy does) from the stored transform.
    // Binarized LODs normally keep each proxy triangle and its "proxy:"
    // selection; flag those points like CICreateProxy does and only rebuild
    // proxies whose selection is missing.
    QSet<QString> proxySelections;
    for (auto &ns : o.namedSelections) {
        if (!ns.name.startsWith(QLatin1String("proxy:"), Qt::CaseInsensitive) || ns.sel.faceCount() == 0)
            continue;
        proxySelections.insert(ns.name.toLower());
        // ODOL proxy selections often list only the face; MLOD ones also
        // contain its three points.
        for (int f = 0; f < o.faces.size(); ++f)
            if (ns.sel.face(f))
                for (int k = 0; k < o.faces[f].n; ++k) {
                    const int p = o.faces[f].v[k].point;
                    ns.sel.setPoint(p, 255);
                    o.points[p].flags |= PointFlags::Hidden;
                }
    }
    for (const auto &px : L.proxies) {
        QString base = px.name;
        if (base.startsWith(QLatin1Char('\\')))
            base.remove(0, 1);
        if (base.endsWith(QLatin1String(".p3d"), Qt::CaseInsensitive))
            base.chop(4);
        const QString expected = QStringLiteral("proxy:\\%1.%2").arg(base).arg(px.seq, 3, 10, QLatin1Char('0')).toLower();
        if (proxySelections.contains(expected))
            continue;
        const QVector3D up(px.m[3], px.m[4], px.m[5]);
        const QVector3D dir(px.m[6], px.m[7], px.m[8]);
        const QVector3D pos(px.m[9], px.m[10], px.m[11]);
        const int a = o.addPoint(pos, PointFlags::Hidden);
        const int b = o.addPoint(pos + up * 2.0f, PointFlags::Hidden);
        const int c = o.addPoint(pos + dir, PointFlags::Hidden);
        Face f;
        f.n = 3;
        f.v[0].point = a;
        f.v[1].point = b;
        f.v[2].point = c;
        const int fi = o.addFace(f);
        const int nrm = o.addNormal(o.storedNormal(fi));
        for (int k = 0; k < 3; ++k)
            o.faces[fi].v[k].normal = nrm;
        QString name = px.name;
        if (name.startsWith(QLatin1Char('\\')))
            name.remove(0, 1);
        if (name.endsWith(QLatin1String(".p3d"), Qt::CaseInsensitive))
            name.chop(4);
        NamedSelection ns;
        ns.name = QStringLiteral("proxy:\\%1.%2").arg(name).arg(px.seq, 3, 10, QLatin1Char('0'));
        ns.sel.resize(o.points.size(), o.faces.size());
        ns.sel.points[a] = ns.sel.points[b] = ns.sel.points[c] = 255;
        ns.sel.faces[fi] = 1;
        o.namedSelections.append(ns);
    }
    if (mass && (mass->mass > 0 || !mass->massArray.isEmpty())) {
        o.mass = QVector<float>(o.points.size(), 0.0f);
        if (mass->massArray.size() == nV) {
            QVector<bool> done(o.points.size(), false);
            for (int i = 0; i < nV; ++i)
                if (!done[vToP[i]]) {
                    o.mass[vToP[i]] = mass->massArray[i];
                    done[vToP[i]] = true;
                }
        } else if (o.points.size() > 0) {
            for (float &m : o.mass)
                m = mass->mass / o.points.size();
        }
    }
    Q_UNUSED(warnings)
    o.normalizeArrays();
}

// ModelInfo (0x14093ad50) up to the mass data. Returns false when the layout
// cannot be followed (embedded skeletons are not decoded); mass is optional.
bool readModelInfo(Reader &r, ModelInfo &mi)
{
    const int v = r.version;
    r.skip(4 + 4 + 4 + 4 + 4 + 4); // special, bsphere, gsphere, remarks, and/or hints
    r.skip(12);                     // aiming center
    r.skip(4 + 4 + 4);              // color, color type, view density
    r.skip(24);                     // bbox
    if (v >= 52)
        r.skip(24);         // visual bbox
    r.skip(12 + 12 + 12);   // bounding center, geometry center, center of mass
    r.skip(36);             // inverse inertia
    r.skip(4);              // autocenter, lock autocenter, can occlude, can be occluded
    if (v > 52)
        r.skip(1 + 4);
    if (v >= 42)
        r.skip(16);         // ht/af/mf
    if (v >= 43)
        r.skip(8);          // mFact, tBody
    if (v > 32)
        r.skip(1);          // force not alpha
    if (v > 36)
        r.skip(4 + 1);      // sb source, prefer shadow volume
    if (v >= 48)
        r.skip(4);          // shadow offset
    r.skip(1);              // animated
    const QString skeleton = r.asciiz();
    if (r.fail)
        return false;
    if (!skeleton.isEmpty()) {
        // SkeletonSource (0x1408d5280): isDiscrete, bones (name, parent), pivots
        if (v >= 23)
            r.skip(1);
        for (qint32 i = 0, c = r.count("bones", 10000); i < c && !r.fail; ++i) {
            mi.bones.append(r.asciiz()); // name
            r.asciiz();                  // parent
        }
        if (v > 40)
            r.asciiz();
    }
    r.skip(1);              // map type
    const QByteArray masses = r.array(4, "mass array");
    mi.massArray.resize(masses.size() / 4);
    for (int i = 0; i < mi.massArray.size(); ++i)
        mi.massArray[i] = at<float>(masses, i);
    mi.mass = r.f32();
    return !r.fail;
}

// Finds the LOD address table that follows ModelInfo/animations:
//   u32 start[n], u32 end[n], u8 permanent[n], 25-byte info per non-permanent LOD
// such that max(end) == file size and the first LOD begins right after it.
bool findLodTable(const Reader &r, int nLods, qsizetype from, QVector<quint32> &starts, QVector<quint32> &ends)
{
    for (qsizetype p = from; p + 9 * nLods <= r.n; ++p) {
        starts.resize(nLods);
        ends.resize(nLods);
        quint32 maxEnd = 0, minStart = 0xffffffff;
        int nonPermanent = 0;
        bool ok = true;
        for (int i = 0; i < nLods && ok; ++i) {
            std::memcpy(&starts[i], r.d + p + 4 * i, 4);
            std::memcpy(&ends[i], r.d + p + 4 * (nLods + i), 4);
            const quint8 perm = r.d[p + 8 * nLods + i];
            ok = perm <= 1 && starts[i] < ends[i] && ends[i] <= quint32(r.n);
            nonPermanent += perm == 0;
            maxEnd = std::max(maxEnd, ends[i]);
            minStart = std::min(minStart, starts[i]);
        }
        if (!ok || maxEnd != quint32(r.n))
            continue;
        const int info = r.version >= 51 ? 25 : (r.version >= 39 ? 17 : 16);
        if (minStart == quint32(p + 9 * nLods + info * nonPermanent))
            return true;
    }
    return false;
}

} // namespace

bool Odol::isOdol(const QByteArray &data)
{
    return data.startsWith("ODOL");
}

bool Odol::load(const QByteArray &data, LodObject &out, QString *error, Info *info)
{
    Reader r;
    r.d = reinterpret_cast<const quint8 *>(data.constData());
    r.n = data.size();
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (!isOdol(data) || r.n < 16)
        return fail(QStringLiteral("Not a binarized (ODOL) model"));
    r.p = 4;
    r.version = r.i32();
    if (info)
        info->version = r.version;
    if (r.version < 28 || r.version > 55)
        return fail(QStringLiteral("Unsupported ODOL version %1 (supported 28..55)").arg(r.version));
    const int nLods = int(r.u32() & 0xff);
    if (nLods <= 0)
        return fail(QStringLiteral("Model has no LODs"));
    QVector<float> res(nLods);
    for (float &f : res)
        f = r.f32();

    ModelInfo mi;
    {
        Reader mr = r;
        if (!readModelInfo(mr, mi))
            mi = ModelInfo();
    }

    QVector<quint32> starts, ends;
    if (r.fail || !findLodTable(r, nLods, r.p, starts, ends))
        return fail(QStringLiteral("Cannot locate the LOD table (unsupported ODOL variant)"));

    QVector<OdolLod> lods(nLods);
    QStringList warnings;
    for (int i = 0; i < nLods; ++i) {
        Reader lr = r;
        lr.p = starts[i];
        lr.n = ends[i];
        if (!readLod(lr, lods[i])) {
            return fail(QStringLiteral("LOD %1 (%2): %3").arg(i).arg(LodNames::name(res[i]), lr.error));
        }
        if (lr.p != qsizetype(ends[i]))
            warnings.append(QStringLiteral("LOD %1: %2 trailing bytes").arg(LodNames::name(res[i])).arg(qsizetype(ends[i]) - lr.p));
    }

    LodObject result;
    result.lods.clear();
    for (int i = 0; i < nLods; ++i) {
        Lod lod;
        lod.resolution = res[i];
        const bool geometry = std::fabs(res[i] - LodNames::Geometry) < 1e10f;
        convertLod(lods[i], lod.data, warnings, geometry ? &mi : nullptr, &mi.bones);
        result.lods.append(lod);
    }
    result.sort();
    result.active = 0;
    out = result;
    if (info) {
        info->warnings = warnings;
        info->bones = mi.bones;
    }
    return true;
}
