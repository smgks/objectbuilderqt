#include "P3dIO.h"

#include <QBuffer>
#include <QFile>
#include <QSaveFile>

#include <cstring>

namespace {

// ---------------------------------------------------------------- reader

class Reader {
public:
    explicit Reader(QIODevice &d) : dev(d) {}

    QIODevice &dev;
    bool failed = false;

    QByteArray bytes(qint64 n)
    {
        if (n < 0) {
            failed = true;
            return {};
        }
        QByteArray b = dev.read(n);
        if (b.size() != n)
            failed = true;
        return b;
    }
    quint32 u32()
    {
        const QByteArray b = bytes(4);
        if (failed)
            return 0;
        quint32 v;
        std::memcpy(&v, b.constData(), 4);
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
    quint8 u8()
    {
        const QByteArray b = bytes(1);
        return failed ? 0 : quint8(b[0]);
    }
    QByteArray asciiz()
    {
        QByteArray s;
        char c;
        while (dev.getChar(&c)) {
            if (c == 0)
                return s;
            s.append(c);
        }
        failed = true;
        return s;
    }
    QByteArray fixed(int n)
    {
        QByteArray b = bytes(n);
        const int z = b.indexOf('\0');
        return z >= 0 ? b.left(z) : b;
    }
    bool skip(qint64 n) { return n == 0 || dev.seek(dev.pos() + n); }
};

QString fromLocal(const QByteArray &b)
{
    return QString::fromLocal8Bit(b);
}

void readSelection(Reader &r, Selection &sel, int np, int nf, quint32 size)
{
    const qint64 start = r.dev.pos();
    const QByteArray pts = r.bytes(qMin<qint64>(np, size));
    const QByteArray fcs = r.bytes(qMin<qint64>(nf, qMax<qint64>(0, qint64(size) - np)));
    sel.resize(np, nf);
    for (int i = 0; i < pts.size(); ++i)
        sel.points[i] = quint8(-qint8(pts[i])); // stored negated (0x5d69c0)
    for (int i = 0; i < fcs.size(); ++i)
        sel.faces[i] = quint8(fcs[i]) ? 1 : 0;
    r.dev.seek(start + size);
}

bool readTags(Reader &r, ObjectData &o, bool oldNames, QString *error)
{
    const qint64 tagStart = r.dev.pos();
    const QByteArray magic = r.bytes(4);
    if (r.failed)
        return false;
    if (magic != "TAGG") {
        // Legacy SS3D tag block (64-byte '$Name$' entries) is not supported;
        // rewind so the resolution float can still be read.
        r.dev.seek(tagStart);
        return true;
    }

    const int np = o.points.size(), nf = o.faces.size();
    bool firstUVSet = true;
    for (;;) {
        QString name;
        if (oldNames) {
            name = fromLocal(r.fixed(64));
        } else {
            r.u8(); // active flag
            name = fromLocal(r.asciiz());
        }
        const quint32 size = r.u32();
        if (r.failed) {
            if (error)
                *error = QStringLiteral("Unexpected end of file in tag block");
            return false;
        }
        const qint64 dataStart = r.dev.pos();

        if (name.compare(QLatin1String("#EndOfFile#"), Qt::CaseInsensitive) == 0) {
            r.skip(size);
            break;
        } else if (name.compare(QLatin1String("#Selected#"), Qt::CaseInsensitive) == 0) {
            readSelection(r, o.selection, np, nf, size);
        } else if (name.compare(QLatin1String("#Lock#"), Qt::CaseInsensitive) == 0) {
            readSelection(r, o.locked, np, nf, size);
        } else if (name.compare(QLatin1String("#Hide#"), Qt::CaseInsensitive) == 0) {
            readSelection(r, o.hidden, np, nf, size);
        } else if (name.compare(QLatin1String("#SharpEdges#"), Qt::CaseInsensitive) == 0) {
            for (quint32 i = 0; i < size / 8; ++i) {
                int a = r.i32(), b = r.i32();
                if (b < a)
                    std::swap(a, b);
                o.sharpEdges.append({a, b});
            }
        } else if (name.compare(QLatin1String("#Mass#"), Qt::CaseInsensitive) == 0) {
            o.mass.resize(np);
            for (int i = 0; i < np && quint32(i * 4) < size; ++i)
                o.mass[i] = r.f32();
        } else if (name.compare(QLatin1String("#Property#"), Qt::CaseInsensitive) == 0) {
            const QString pn = fromLocal(r.fixed(64));
            const QString pv = fromLocal(r.fixed(64));
            o.namedProperties.append({pn, pv});
        } else if (name.compare(QLatin1String("#Animation#"), Qt::CaseInsensitive) == 0) {
            AnimationPhase ph;
            ph.time = r.f32();
            ph.points.resize(np);
            for (int i = 0; i < np; ++i) {
                const float x = r.f32(), y = r.f32(), z = r.f32();
                ph.points[i] = QVector3D(x, y, z);
            }
            o.animations.append(ph);
        } else if (name.compare(QLatin1String("#UVSet#"), Qt::CaseInsensitive) == 0) {
            if (firstUVSet) { // first #UVSet# replaces the face-record UVs
                o.uvSets.clear();
                firstUVSet = false;
            }
            UVSet set;
            set.id = r.u32();
            set.uv.resize(nf);
            for (int f = 0; f < nf; ++f)
                for (int k = 0; k < o.faces[f].n; ++k) {
                    const float u = r.f32(), v = r.f32();
                    set.uv[f][k] = QVector2D(u, v);
                }
            o.uvSets.append(set);
        } else if (name.startsWith(QLatin1Char('#'))) {
            // Unknown internal tag: the original skips it.
        } else if (o.namedSelections.size() < 2048) {
            NamedSelection ns;
            ns.name = name;
            readSelection(r, ns.sel, np, nf, size);
            o.namedSelections.append(ns);
        }
        if (r.failed) {
            if (error)
                *error = QStringLiteral("Corrupt tag '%1'").arg(name);
            return false;
        }
        r.dev.seek(dataStart + size);
    }
    return true;
}

bool readLod(Reader &r, ObjectData &o, QString *error)
{
    o = ObjectData();
    const QByteArray magic = r.bytes(4);
    if (r.failed)
        return false;
    const bool p3dm = magic == "P3DM";
    const bool sp3x = magic == "SP3X";
    if (!p3dm && !sp3x) {
        if (error)
            *error = QStringLiteral("Unsupported LOD format '%1'").arg(QString::fromLatin1(magic));
        return false;
    }
    const quint32 headerSize = r.u32();
    r.u32(); // version (0x100 P3DM, 0x99 SP3X)
    const int np = r.i32(), nn = r.i32(), nf = r.i32();
    r.u32(); // flags
    if (r.failed || np < 0 || nn < 0 || nf < 0) {
        if (error)
            *error = QStringLiteral("Corrupt LOD header");
        return false;
    }
    r.skip(qint64(headerSize) - 28);

    o.points.resize(np);
    for (int i = 0; i < np; ++i) {
        const float x = r.f32(), y = r.f32(), z = r.f32();
        o.points[i].pos = QVector3D(x, y, z);
        o.points[i].flags = r.u32();
    }
    o.normals.resize(nn);
    for (int i = 0; i < nn; ++i) {
        const float x = r.f32(), y = r.f32(), z = r.f32();
        o.normals[i] = QVector3D(x, y, z);
    }
    o.faces.resize(nf);
    o.uvSets[0].uv.resize(nf);
    for (int f = 0; f < nf; ++f) {
        Face &fc = o.faces[f];
        if (sp3x)
            fc.texture = fromLocal(r.fixed(32));
        fc.n = r.i32();
        for (int k = 0; k < 4; ++k) {
            fc.v[k].point = r.i32();
            fc.v[k].normal = r.i32();
            const float u = r.f32(), v = r.f32();
            o.uvSets[0].uv[f][k] = QVector2D(u, v);
        }
        fc.flags = r.u32();
        if (p3dm) {
            // The original lower-cases both paths on load.
            fc.texture = fromLocal(r.asciiz()).toLower();
            fc.material = fromLocal(r.asciiz()).toLower();
        }
        if (r.failed)
            break;
        if (fc.n < 3 || fc.n > 4) {
            if (error)
                *error = QStringLiteral("Face %1 has %2 vertices").arg(f).arg(fc.n);
            return false;
        }
        for (int k = 0; k < fc.n; ++k)
            if (fc.v[k].point < 0 || fc.v[k].point >= np) {
                if (error)
                    *error = QStringLiteral("Face %1 references missing point %2").arg(f).arg(fc.v[k].point);
                return false;
            }
    }
    if (r.failed) {
        if (error)
            *error = QStringLiteral("Unexpected end of file in LOD data");
        return false;
    }
    o.normalizeArrays();
    if (!readTags(r, o, sp3x, error))
        return false;
    o.normalizeArrays();
    return true;
}

// ---------------------------------------------------------------- writer

class Writer {
public:
    explicit Writer(QIODevice &d) : dev(d) {}
    QIODevice &dev;

    void raw(const char *p, qint64 n) { dev.write(p, n); }
    void raw(const QByteArray &b) { dev.write(b); }
    void u32(quint32 v) { raw(reinterpret_cast<const char *>(&v), 4); }
    void i32(qint32 v) { u32(quint32(v)); }
    void f32(float f) { raw(reinterpret_cast<const char *>(&f), 4); }
    void u8(quint8 v) { raw(reinterpret_cast<const char *>(&v), 1); }
    void asciiz(const QString &s)
    {
        raw(s.toLocal8Bit());
        u8(0);
    }
    void fixed(const QString &s, int n)
    {
        QByteArray b = s.toLocal8Bit().left(n);
        b.resize(n, '\0');
        raw(b);
    }
    // Tag header (0x5e5dd0): new style = 1 + asciiz name, old style = char[64].
    void tag(const QString &name, quint32 size, bool oldNames)
    {
        if (oldNames)
            fixed(name, 64);
        else {
            u8(1);
            asciiz(name);
        }
        u32(size);
    }
};

void writeSelection(Writer &w, const Selection &s, int np, int nf)
{
    QByteArray b(np + nf, '\0');
    for (int i = 0; i < np; ++i)
        b[i] = char(-qint8(s.weight(i)));
    for (int i = 0; i < nf; ++i)
        b[np + i] = s.face(i) ? 1 : 0;
    w.raw(b);
}

void writeLod(Writer &w, const ObjectData &src, bool massTag, const P3dIO::SaveOptions &opt)
{
    ObjectData o = src;
    o.normalizeArrays();
    const int np = o.points.size(), nn = o.normals.size(), nf = o.faces.size();
    const UVSet &uv0 = o.uvSet0();

    // header (0x5e5960)
    w.raw(opt.oldFormat ? "SP3X" : "P3DM", 4);
    w.u32(28);
    w.u32(opt.oldFormat ? 0x99 : 0x100);
    w.i32(np);
    w.i32(nn);
    w.i32(nf);
    w.u32(0);
    for (const Point &p : o.points) {
        w.f32(p.pos.x());
        w.f32(p.pos.y());
        w.f32(p.pos.z());
        w.u32(p.flags);
    }
    for (const QVector3D &n : o.normals) {
        w.f32(n.x());
        w.f32(n.y());
        w.f32(n.z());
    }
    for (int f = 0; f < nf; ++f) {
        const Face &fc = o.faces[f];
        if (opt.oldFormat)
            w.fixed(fc.texture, 32);
        w.i32(fc.n);
        for (int k = 0; k < 4; ++k) {
            w.i32(k < fc.n ? fc.v[k].point : 0);
            w.i32(k < fc.n ? fc.v[k].normal : 0);
            const QVector2D t = k < fc.n ? uv0.uv[f][k] : QVector2D();
            w.f32(t.x());
            w.f32(t.y());
        }
        w.u32(fc.flags);
        if (!opt.oldFormat) {
            w.asciiz(fc.texture);
            w.asciiz(fc.material);
        }
    }

    // tags (0x5e6250) in the original order
    const bool old = opt.oldFormat;
    const quint32 selSize = quint32(np + nf);
    w.raw("TAGG", 4);
    if (!o.sharpEdges.isEmpty()) {
        w.tag(QStringLiteral("#SharpEdges#"), quint32(o.sharpEdges.size() * 8), old);
        for (const auto &e : o.sharpEdges) {
            w.i32(e.first);
            w.i32(e.second);
        }
    }
    if (!o.selection.isEmpty() && !opt.stripEditorData) {
        w.tag(QStringLiteral("#Selected#"), selSize, old);
        writeSelection(w, o.selection, np, nf);
    }
    auto skipName = [&opt](const QString &n) {
        if (n.startsWith(QLatin1Char('!')))
            return true;
        return opt.stripEditorData && (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char('-')));
    };
    for (const auto &ns : o.namedSelections) {
        if (ns.sel.isEmpty() || skipName(ns.name))
            continue;
        w.tag(ns.name, selSize, old);
        writeSelection(w, ns.sel, np, nf);
    }
    for (const auto &p : o.namedProperties) {
        if (skipName(p.name))
            continue;
        w.tag(QStringLiteral("#Property#"), 128, old);
        w.fixed(p.name, 64);
        w.fixed(p.value, 64);
    }
    if (!o.locked.isEmpty() && !opt.stripEditorData) {
        w.tag(QStringLiteral("#Lock#"), selSize, old);
        writeSelection(w, o.locked, np, nf);
    }
    if (!o.hidden.isEmpty() && !opt.stripEditorData) {
        w.tag(QStringLiteral("#Hide#"), selSize, old);
        writeSelection(w, o.hidden, np, nf);
    }
    if (massTag) {
        w.tag(QStringLiteral("#Mass#"), quint32(np * 4), old);
        for (int i = 0; i < np; ++i)
            w.f32(i < o.mass.size() ? o.mass[i] : 0.0f);
    }
    for (const auto &a : o.animations) {
        w.tag(QStringLiteral("#Animation#"), quint32(np * 12 + 4), old);
        w.f32(a.time);
        for (const QVector3D &p : a.points) {
            w.f32(p.x());
            w.f32(p.y());
            w.f32(p.z());
        }
    }
    quint32 uvSize = 4;
    for (const Face &fc : o.faces)
        uvSize += quint32(fc.n * 8);
    for (const auto &set : o.uvSets) {
        w.tag(QStringLiteral("#UVSet#"), uvSize, old);
        w.u32(set.id);
        for (int f = 0; f < nf; ++f)
            for (int k = 0; k < o.faces[f].n; ++k) {
                w.f32(set.uv[f][k].x());
                w.f32(set.uv[f][k].y());
            }
    }
    w.tag(QStringLiteral("#EndOfFile#"), 0, old);
}

} // namespace

// ---------------------------------------------------------------- public API

bool P3dIO::load(QIODevice &dev, LodObject &out, QString *error)
{
    Reader r(dev);
    const QByteArray magic = r.bytes(4);
    if (r.failed) {
        if (error)
            *error = QStringLiteral("File is too short");
        return false;
    }
    qint32 nLods = 0;
    if (magic == "MLOD") {
        r.u32(); // version 0x101 (9999 also accepted by the original)
        nLods = r.i32();
    } else if (magic == "NLOD") {
        nLods = r.i32();
    } else if (magic == "ODOL") {
        if (error)
            *error = QStringLiteral("This is a binarized (ODOL) model. Only editable MLOD files can be opened.");
        return false;
    } else {
        if (error)
            *error = QStringLiteral("Not a P3D model (signature '%1')").arg(QString::fromLatin1(magic.toHex()));
        return false;
    }
    if (r.failed || nLods <= 0 || nLods > 100000) {
        if (error)
            *error = QStringLiteral("Invalid LOD count");
        return false;
    }

    LodObject result;
    result.lods.clear();
    for (int i = 0; i < nLods; ++i) {
        Lod lod;
        if (!readLod(r, lod.data, error)) {
            if (error && !error->isEmpty())
                *error = QStringLiteral("LOD %1: %2").arg(i + 1).arg(*error);
            if (result.lods.isEmpty())
                return false;
            break; // keep what was read, like the original
        }
        lod.resolution = r.f32();
        if (r.failed)
            lod.resolution = 0.0f;
        result.lods.append(lod);
    }
    result.sort();
    result.active = 0;
    out = result;
    return true;
}

bool P3dIO::load(const QString &path, LodObject &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return load(f, out, error);
}

bool P3dIO::save(QIODevice &dev, const LodObject &obj, const SaveOptions &opt, QString *error)
{
    Writer w(dev);
    w.raw("MLOD", 4);
    w.u32(0x101);
    w.i32(obj.lods.size());
    for (const Lod &lod : obj.lods) {
        writeLod(w, lod.data, LodNames::hasMass(lod.resolution), opt);
        w.f32(lod.resolution);
    }
    if (!dev.errorString().isEmpty() && dev.errorString() != QLatin1String("Unknown error")) {
        if (error)
            *error = dev.errorString();
        return false;
    }
    return true;
}

bool P3dIO::save(const QString &path, const LodObject &obj, const SaveOptions &opt, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    save(f, obj, opt, error);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QByteArray P3dIO::serializeLod(const ObjectData &obj)
{
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    Writer w(buf);
    writeLod(w, obj, true, SaveOptions{});
    return buf.data();
}

bool P3dIO::deserializeLod(const QByteArray &data, ObjectData &out, QString *error)
{
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    Reader r(buf);
    return readLod(r, out, error);
}
