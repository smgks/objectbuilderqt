#include "FbxIO.h"
#include "Flags.h"
#include "TextureLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMatrix4x4>
#include <QRegularExpression>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

// ---------------------------------------------------------------- node tree

struct FProp {
    char type = 0;
    qint64 i = 0;
    double d = 0;
    QByteArray s;
    QVector<double> darr;
    QVector<qint64> iarr;

    double num() const { return (type == 'F' || type == 'D') ? d : double(i); }
    QString str() const { return QString::fromUtf8(s); }
};

struct FNode {
    QByteArray name;
    QVector<FProp> props;
    QVector<FNode> children;

    const FNode *child(const char *n) const
    {
        for (const FNode &c : children)
            if (c.name == n)
                return &c;
        return nullptr;
    }
    QVector<const FNode *> all(const char *n) const
    {
        QVector<const FNode *> out;
        for (const FNode &c : children)
            if (c.name == n)
                out.append(&c);
        return out;
    }
    QVector<double> doubles() const
    {
        if (props.isEmpty())
            return {};
        if (!props[0].darr.isEmpty())
            return props[0].darr;
        if (!props[0].iarr.isEmpty()) {
            QVector<double> v;
            for (qint64 x : props[0].iarr)
                v.append(double(x));
            return v;
        }
        QVector<double> v; // ASCII scalars
        for (const FProp &p : props)
            v.append(p.num());
        return v;
    }
    QVector<qint64> ints() const
    {
        if (props.isEmpty())
            return {};
        if (!props[0].iarr.isEmpty())
            return props[0].iarr;
        if (!props[0].darr.isEmpty()) {
            QVector<qint64> v;
            for (double x : props[0].darr)
                v.append(qint64(std::llround(x)));
            return v;
        }
        QVector<qint64> v;
        for (const FProp &p : props)
            v.append(qint64(std::llround(p.num())));
        return v;
    }
    QString stringChild(const char *n) const
    {
        const FNode *c = child(n);
        return c && !c->props.isEmpty() ? c->props[0].str() : QString();
    }
};

// ---------------------------------------------------------------- binary parser

class BinaryParser {
public:
    BinaryParser(const QByteArray &d) : data(d), p(reinterpret_cast<const quint8 *>(d.constData())), n(d.size()) {}

    bool parse(FNode &root, QString *err)
    {
        version = qFromLittleEndian<quint32>(p + 23);
        pos = 27;
        for (;;) {
            FNode node;
            bool isNull = false;
            if (!readNode(node, isNull)) {
                if (err)
                    *err = QStringLiteral("Corrupt FBX at offset %1").arg(pos);
                return false;
            }
            if (isNull || pos >= n)
                break;
            root.children.append(std::move(node));
        }
        return true;
    }

private:
    const QByteArray &data;
    const quint8 *p;
    qsizetype n;
    qsizetype pos = 0;
    quint32 version = 0;

    bool wide() const { return version >= 7500; }
    bool have(qsizetype k) const { return pos + k <= n; }
    quint64 word()
    {
        if (wide()) {
            const quint64 v = qFromLittleEndian<quint64>(p + pos);
            pos += 8;
            return v;
        }
        const quint32 v = qFromLittleEndian<quint32>(p + pos);
        pos += 4;
        return v;
    }

    bool readNode(FNode &node, bool &isNull)
    {
        const int hdr = wide() ? 25 : 13;
        if (!have(hdr))
            return false;
        const quint64 end = word(), numProps = word();
        word(); // property list length
        const int nameLen = p[pos++];
        if (end == 0) {
            isNull = true;
            return true;
        }
        if (!have(nameLen) || end > quint64(n))
            return false;
        node.name = QByteArray(reinterpret_cast<const char *>(p + pos), nameLen);
        pos += nameLen;
        for (quint64 i = 0; i < numProps; ++i) {
            FProp pr;
            if (!readProp(pr))
                return false;
            node.props.append(std::move(pr));
        }
        while (qsizetype(end) - pos > hdr) {
            FNode c;
            bool cnull = false;
            if (!readNode(c, cnull))
                return false;
            if (cnull)
                break;
            node.children.append(std::move(c));
        }
        pos = qsizetype(end);
        return true;
    }

    template <typename T>
    QVector<T> array(int elemSize)
    {
        const quint32 count = qFromLittleEndian<quint32>(p + pos);
        const quint32 enc = qFromLittleEndian<quint32>(p + pos + 4);
        const quint32 clen = qFromLittleEndian<quint32>(p + pos + 8);
        pos += 12;
        if (!have(clen))
            return {};
        QByteArray raw;
        if (enc == 1) {
            QByteArray z(4, '\0');
            qToBigEndian<quint32>(count * elemSize, reinterpret_cast<uchar *>(z.data()));
            z.append(reinterpret_cast<const char *>(p + pos), clen);
            raw = qUncompress(z);
        } else {
            raw = QByteArray(reinterpret_cast<const char *>(p + pos), clen);
        }
        pos += clen;
        QVector<T> out(count);
        const auto *r = reinterpret_cast<const quint8 *>(raw.constData());
        if (raw.size() < qsizetype(count) * elemSize)
            return {};
        for (quint32 i = 0; i < count; ++i) {
            const quint8 *e = r + qsizetype(i) * elemSize;
            if constexpr (std::is_same_v<T, double>) {
                if (elemSize == 8) {
                    double v;
                    std::memcpy(&v, e, 8);
                    out[i] = v;
                } else {
                    float v;
                    std::memcpy(&v, e, 4);
                    out[i] = v;
                }
            } else {
                if (elemSize == 8)
                    out[i] = qFromLittleEndian<qint64>(e);
                else if (elemSize == 4)
                    out[i] = qFromLittleEndian<qint32>(e);
                else
                    out[i] = e[0];
            }
        }
        return out;
    }

    bool readProp(FProp &pr)
    {
        if (!have(1))
            return false;
        pr.type = char(p[pos++]);
        switch (pr.type) {
        case 'Y': pr.i = qFromLittleEndian<qint16>(p + pos); pos += 2; break;
        case 'C': pr.i = p[pos]; pos += 1; break;
        case 'I': pr.i = qFromLittleEndian<qint32>(p + pos); pos += 4; break;
        case 'L': pr.i = qFromLittleEndian<qint64>(p + pos); pos += 8; break;
        case 'F': {
            float f;
            std::memcpy(&f, p + pos, 4);
            pr.d = f;
            pos += 4;
            break;
        }
        case 'D': std::memcpy(&pr.d, p + pos, 8); pos += 8; break;
        case 'f': pr.darr = array<double>(4); break;
        case 'd': pr.darr = array<double>(8); break;
        case 'i': pr.iarr = array<qint64>(4); break;
        case 'l': pr.iarr = array<qint64>(8); break;
        case 'b': pr.iarr = array<qint64>(1); break;
        case 'S':
        case 'R': {
            const quint32 len = qFromLittleEndian<quint32>(p + pos);
            pos += 4;
            if (!have(len))
                return false;
            pr.s = QByteArray(reinterpret_cast<const char *>(p + pos), len);
            pos += len;
            break;
        }
        default: return false;
        }
        return pos <= n;
    }
};

// ---------------------------------------------------------------- ASCII parser

class AsciiParser {
public:
    explicit AsciiParser(const QByteArray &d) : s(d) {}

    bool parse(FNode &root, QString *err)
    {
        while (skipWs(), pos < s.size()) {
            FNode node;
            if (!readNode(node)) {
                if (err)
                    *err = QStringLiteral("FBX ASCII syntax error near offset %1").arg(pos);
                return false;
            }
            root.children.append(std::move(node));
        }
        return true;
    }

private:
    const QByteArray &s;
    qsizetype pos = 0;

    void skipWs()
    {
        while (pos < s.size()) {
            const char c = s[pos];
            if (c == ';') {
                while (pos < s.size() && s[pos] != '\n')
                    ++pos;
            } else if (isspace(uchar(c))) {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool readNode(FNode &node)
    {
        skipWs();
        const qsizetype start = pos;
        while (pos < s.size() && (isalnum(uchar(s[pos])) || s[pos] == '_' || s[pos] == '|'))
            ++pos;
        if (pos == start || pos >= s.size() || s[pos] != ':')
            return false;
        node.name = s.mid(start, pos - start);
        ++pos;
        // values until '{' or end of line
        for (;;) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r'))
                ++pos;
            if (pos >= s.size() || s[pos] == '\n' || s[pos] == '}')
                break;
            if (s[pos] == '{') {
                ++pos;
                for (;;) {
                    skipWs();
                    if (pos >= s.size())
                        return false;
                    if (s[pos] == '}') {
                        ++pos;
                        break;
                    }
                    FNode c;
                    if (!readNode(c))
                        return false;
                    if (c.name == "a" && node.props.size() == 1 && node.props[0].type == '*') {
                        // "Name: *N { a: 1,2,3 }" -> fold array into the parent
                        node.props[0] = FProp{};
                        node.props[0].type = 'd';
                        for (const FProp &p : c.props)
                            node.props[0].darr.append(p.num());
                        continue;
                    }
                    node.children.append(std::move(c));
                }
                break;
            }
            if (s[pos] == ',') {
                ++pos;
                continue;
            }
            FProp pr;
            if (s[pos] == '"') {
                const qsizetype e = s.indexOf('"', pos + 1);
                if (e < 0)
                    return false;
                pr.type = 'S';
                pr.s = s.mid(pos + 1, e - pos - 1);
                // FBX ASCII uses "Name::Class"; binary uses "Name\x00\x01Class"
                const int cc = pr.s.indexOf("::");
                if (cc >= 0)
                    pr.s = pr.s.mid(cc + 2) + QByteArray("\x00\x01", 2) + pr.s.left(cc);
                pos = e + 1;
            } else if (s[pos] == '*') {
                ++pos;
                while (pos < s.size() && isdigit(uchar(s[pos])))
                    ++pos;
                pr.type = '*';
            } else {
                const qsizetype st = pos;
                while (pos < s.size() && s[pos] != ',' && s[pos] != '\n' && s[pos] != '{' && s[pos] != '}' && s[pos] != '\r')
                    ++pos;
                const QByteArray tok = s.mid(st, pos - st).trimmed();
                bool ok = false;
                if (tok.contains('.') || tok.contains('e') || tok.contains('E')) {
                    pr.type = 'D';
                    pr.d = tok.toDouble(&ok);
                } else {
                    pr.type = 'L';
                    pr.i = tok.toLongLong(&ok);
                }
                if (!ok) {
                    pr.type = 'S';
                    pr.s = tok;
                }
            }
            node.props.append(std::move(pr));
        }
        return true;
    }
};

// ---------------------------------------------------------------- scene helpers

// Properties70 lookup: P: "name", "type", "label", "flags", values...
QVector<double> prop70(const FNode &obj, const char *name, const QVector<double> &def = {})
{
    const FNode *p70 = obj.child("Properties70");
    if (!p70)
        return def;
    for (const FNode &p : p70->children)
        if (!p.props.isEmpty() && p.props[0].s == name) {
            QVector<double> v;
            for (int i = 4; i < p.props.size(); ++i)
                v.append(p.props[i].num());
            return v;
        }
    return def;
}

QString prop70s(const FNode &obj, const char *name)
{
    const FNode *p70 = obj.child("Properties70");
    if (!p70)
        return {};
    for (const FNode &p : p70->children)
        if (p.props.size() > 4 && p.props[0].s == name)
            return p.props[4].str();
    return {};
}

QString objectName(const FNode &obj)
{
    if (obj.props.size() < 2)
        return {};
    QByteArray s = obj.props[1].s;
    const int sep = s.indexOf(QByteArray("\x00\x01", 2));
    if (sep >= 0)
        s = s.left(sep);
    return QString::fromUtf8(s);
}

QString objectClass(const FNode &obj)
{
    return obj.props.size() > 2 ? obj.props[2].str() : QString();
}

qint64 objectId(const FNode &obj)
{
    return obj.props.isEmpty() ? 0 : obj.props[0].i;
}

QMatrix4x4 eulerXYZ(const QVector<double> &r, int order)
{
    QMatrix4x4 m;
    const float x = float(r.value(0)), y = float(r.value(1)), z = float(r.value(2));
    // FBX rotation orders: 0 XYZ, 1 XZY, 2 YZX, 3 YXZ, 4 ZXY, 5 ZYX (applied left to right)
    const QVector3D ax(1, 0, 0), ay(0, 1, 0), az(0, 0, 1);
    struct Step {
        float a;
        QVector3D axis;
    };
    Step steps[3];
    switch (order) {
    case 1: steps[0] = {x, ax}; steps[1] = {z, az}; steps[2] = {y, ay}; break;
    case 2: steps[0] = {y, ay}; steps[1] = {z, az}; steps[2] = {x, ax}; break;
    case 3: steps[0] = {y, ay}; steps[1] = {x, ax}; steps[2] = {z, az}; break;
    case 4: steps[0] = {z, az}; steps[1] = {x, ax}; steps[2] = {y, ay}; break;
    case 5: steps[0] = {z, az}; steps[1] = {y, ay}; steps[2] = {x, ax}; break;
    default: steps[0] = {x, ax}; steps[1] = {y, ay}; steps[2] = {z, az}; break;
    }
    for (int i = 2; i >= 0; --i)
        m.rotate(steps[i].a, steps[i].axis);
    return m;
}

QMatrix4x4 localTransform(const FNode &model)
{
    const QVector<double> t = prop70(model, "Lcl Translation", {0, 0, 0});
    const QVector<double> r = prop70(model, "Lcl Rotation", {0, 0, 0});
    const QVector<double> s = prop70(model, "Lcl Scaling", {1, 1, 1});
    const QVector<double> pre = prop70(model, "PreRotation", {0, 0, 0});
    const QVector<double> post = prop70(model, "PostRotation", {0, 0, 0});
    const QVector<double> rp = prop70(model, "RotationPivot", {0, 0, 0});
    const QVector<double> sp = prop70(model, "ScalingPivot", {0, 0, 0});
    const QVector<double> ro = prop70(model, "RotationOffset", {0, 0, 0});
    const QVector<double> so = prop70(model, "ScalingOffset", {0, 0, 0});
    const int order = int(prop70(model, "RotationOrder", {0}).value(0));
    auto v3 = [](const QVector<double> &v) { return QVector3D(float(v.value(0)), float(v.value(1)), float(v.value(2))); };
    QMatrix4x4 m;
    m.translate(v3(t) + v3(ro) + v3(rp));
    m *= eulerXYZ(pre, 0);
    m *= eulerXYZ(r, order);
    m *= eulerXYZ(post, 0).inverted();
    m.translate(-v3(rp) + v3(so) + v3(sp));
    m.scale(v3(s));
    m.translate(-v3(sp));
    return m;
}

QMatrix4x4 geometricTransform(const FNode &model)
{
    auto v3 = [](const QVector<double> &v) { return QVector3D(float(v.value(0)), float(v.value(1)), float(v.value(2))); };
    QMatrix4x4 m;
    m.translate(v3(prop70(model, "GeometricTranslation", {0, 0, 0})));
    m *= eulerXYZ(prop70(model, "GeometricRotation", {0, 0, 0}), 0);
    m.scale(v3(prop70(model, "GeometricScaling", {1, 1, 1})));
    return m;
}

// Layer element value for polygon-vertex `pv` of polygon `poly` at control point `cp`.
template <typename Fn>
bool layerIndex(const FNode &layer, int pv, int poly, int cp, Fn &&fn)
{
    const QString mapping = layer.stringChild("MappingInformationType");
    const QString ref = layer.stringChild("ReferenceInformationType");
    int idx;
    if (mapping == QLatin1String("ByPolygonVertex"))
        idx = pv;
    else if (mapping == QLatin1String("ByPolygon"))
        idx = poly;
    else if (mapping == QLatin1String("AllSame"))
        idx = 0;
    else
        idx = cp; // ByVertice / ByVertex / ByControlPoint
    return fn(idx, ref == QLatin1String("IndexToDirect") || ref == QLatin1String("Index"));
}

bool parseLodName(QString name, float &res)
{
    name = name.trimmed();
    if (LodNames::parse(name, res))
        return true;
    // "LOD1", "LOD_1", "lod-2.5", "res_1" ...
    static const char *prefixes[] = {"lod", "res", "resolution"};
    for (const char *p : prefixes)
        if (name.startsWith(QLatin1String(p), Qt::CaseInsensitive)) {
            QString rest = name.mid(int(qstrlen(p)));
            while (!rest.isEmpty() && (rest[0] == QLatin1Char('_') || rest[0] == QLatin1Char('-') || rest[0] == QLatin1Char(' ')))
                rest.remove(0, 1);
            bool ok = false;
            const double v = rest.toDouble(&ok);
            if (ok) {
                res = float(v);
                return true;
            }
            return LodNames::parse(rest, res);
        }
    // Blender-style duplicates: "Geometry.001"
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        bool numeric = true;
        for (int i = dot + 1; i < name.size(); ++i)
            numeric &= name[i].isDigit();
        if (numeric && name.size() - dot == 4)
            return LodNames::parse(name.left(dot), res);
    }
    return false;
}

QString cleanTexturePath(QString path, const FbxIO::Options &opt)
{
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    QString drive = QDir::toNativeSeparators(opt.workDrive);
    if (!drive.isEmpty() && !drive.endsWith(QLatin1Char('\\')))
        drive += QLatin1Char('\\');
    if (!drive.isEmpty() && path.startsWith(drive, Qt::CaseInsensitive))
        path = path.mid(drive.size());
    else if (!opt.texturePrefixStrip.isEmpty() && path.startsWith(opt.texturePrefixStrip, Qt::CaseInsensitive))
        path = path.mid(opt.texturePrefixStrip.size());
    else if (path.size() > 2 && path[1] == QLatin1Char(':') && path[0].toUpper() == QLatin1Char('P'))
        path = path.mid(3); // P:\ work drive
    return path;
}

// How suitable a material slot is as the face texture: diffuse / base colour
// first, unnamed slots next, other maps (normal, specular...) never.
int textureSlotScore(const QByteArray &slot)
{
    const QByteArray s = slot.toLower();
    if (s.contains("diffuse") || s.contains("basecolor") || s.contains("base_color") || s.contains("albedo"))
        return 2;
    static const char *other[] = {"normal", "bump", "specular", "rough", "metal", "emissive", "opacity", "transparen",
                                  "reflect", "shininess", "ambient", "displace", "gloss", "height", "vector"};
    for (const char *o : other)
        if (s.contains(o))
            return 0;
    return 1;
}

// Embedded image bytes of a Video node: raw in binary files, base64 in ASCII.
QByteArray embeddedContent(const FNode &video)
{
    const FNode *c = video.child("Content");
    if (!c)
        return {};
    QByteArray text;
    for (const FProp &p : c->props) {
        if (p.type == 'R')
            return p.s;
        text += p.s;
    }
    return QByteArray::fromBase64(text);
}

} // namespace

// ---------------------------------------------------------------- import

bool FbxIO::import(const QString &path, LodObject &into, const Options &opt, Report *report, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    FNode root;
    if (data.startsWith("Kaydara FBX Binary")) {
        if (!BinaryParser(data).parse(root, error))
            return false;
    } else if (!AsciiParser(data).parse(root, error)) {
        return false;
    }
    Report rep;

    // --- global settings: units and axes
    float unit = 1.0f;
    QMatrix4x4 axes;
    if (const FNode *gs = root.child("GlobalSettings")) {
        if (opt.convertUnits)
            unit = float(prop70(*gs, "UnitScaleFactor", {1.0}).value(0)) / 100.0f; // FBX unit is cm
        if (opt.convertAxes) {
            const int up = int(prop70(*gs, "UpAxis", {1}).value(0)), upS = int(prop70(*gs, "UpAxisSign", {1}).value(0));
            const int fr = int(prop70(*gs, "FrontAxis", {2}).value(0)), frS = int(prop70(*gs, "FrontAxisSign", {1}).value(0));
            const int co = int(prop70(*gs, "CoordAxis", {0}).value(0)), coS = int(prop70(*gs, "CoordAxisSign", {1}).value(0));
            auto row = [](int axis, int sign) {
                QVector4D r;
                r[std::clamp(axis, 0, 2)] = float(sign >= 0 ? 1 : -1);
                return r;
            };
            axes.setRow(0, row(co, coS));
            axes.setRow(1, row(up, upS));
            axes.setRow(2, row(fr, frS));
        }
    }
    QMatrix4x4 toModel;
    toModel.scale(1, 1, -1); // right-handed Y-up -> left-handed model space
    toModel.scale(unit * opt.scale);
    toModel *= axes;

    // --- objects and connections
    const FNode *objects = root.child("Objects");
    const FNode *connections = root.child("Connections");
    if (!objects) {
        if (error)
            *error = QStringLiteral("FBX file has no Objects section");
        return false;
    }
    QHash<qint64, const FNode *> byId;
    for (const FNode &o : objects->children)
        byId.insert(objectId(o), &o);
    QHash<qint64, qint64> parentOf;              // model -> parent model
    QMultiHash<qint64, qint64> childrenOf;       // any -> children (connection order)
    QHash<qint64, QVector<qint64>> orderedChildren;
    QHash<QPair<qint64, qint64>, QByteArray> slotOf; // (child, parent) -> property of an "OP" connection
    if (connections)
        for (const FNode &c : connections->children) {
            if (c.props.size() < 3)
                continue;
            const qint64 child = c.props[1].i, parent = c.props[2].i;
            orderedChildren[parent].append(child);
            if (c.props.size() > 3)
                slotOf.insert({child, parent}, c.props[3].s);
            const FNode *cn = byId.value(child), *pn = byId.value(parent);
            if (cn && pn && cn->name == "Model" && pn->name == "Model")
                parentOf.insert(child, parent);
        }

    auto globalTransform = [&](qint64 id) {
        QMatrix4x4 m;
        QVector<qint64> chain;
        for (qint64 cur = id; byId.contains(cur) && byId.value(cur)->name == "Model"; cur = parentOf.value(cur, 0)) {
            chain.prepend(cur);
            if (!parentOf.contains(cur))
                break;
        }
        for (qint64 c : chain)
            m *= localTransform(*byId.value(c));
        return m;
    };

    // --- LOD assignment
    QHash<qint64, float> lodOfModel; // model id -> resolution
    // 1. LodGroup
    for (const FNode &o : objects->children) {
        if (o.name != "NodeAttribute" || objectClass(o) != QLatin1String("LodGroup"))
            continue;
        // The attribute is connected to its model; the model's children are the levels.
        for (auto it = orderedChildren.cbegin(); it != orderedChildren.cend(); ++it) {
            if (!it.value().contains(objectId(o)))
                continue;
            const qint64 groupModel = it.key();
            int level = 0;
            for (qint64 c : orderedChildren.value(groupModel)) {
                const FNode *cn = byId.value(c);
                if (!cn || cn->name != "Model")
                    continue;
                // Explicit LOD names win; generic "LOD0", "LOD1"... (DCC tool
                // convention) are detail levels in order: 1, 2, 3...
                ++level;
                float res;
                static const QRegularExpression generic(QStringLiteral("^lod[ _-]?\\d+$"), QRegularExpression::CaseInsensitiveOption);
                if (generic.match(objectName(*cn)).hasMatch() || !parseLodName(objectName(*cn), res))
                    res = float(level);
                lodOfModel.insert(c, res);
            }
        }
    }
    // 2. top-level (or any) model named like a LOD
    for (const FNode &o : objects->children) {
        if (o.name != "Model" || lodOfModel.contains(objectId(o)))
            continue;
        float res;
        if (parseLodName(objectName(o), res))
            lodOfModel.insert(objectId(o), res);
    }
    auto lodFor = [&](qint64 model) {
        for (qint64 cur = model; cur; cur = parentOf.value(cur, 0)) {
            if (lodOfModel.contains(cur))
                return lodOfModel.value(cur);
            if (!parentOf.contains(cur))
                break;
        }
        return opt.defaultResolution;
    };

    // --- texture files: locate (or extract) the file behind a Texture node
    const QFileInfo fbxInfo(path);
    const QDir fbxDir = fbxInfo.absoluteDir();
    const QString fbmDir = fbxInfo.completeBaseName() + QStringLiteral(".fbm");
    QHash<qint64, QString> textureCache;
    auto textureFile = [&](qint64 texId) -> QString {
        if (textureCache.contains(texId))
            return textureCache.value(texId);
        const FNode *t = byId.value(texId);
        const FNode *video = nullptr;
        for (qint64 c : orderedChildren.value(texId))
            if (byId.value(c) && byId.value(c)->name == "Video")
                video = byId.value(c);
        // names as written: absolute first, then relative
        QStringList absolute, relative;
        for (const FNode *n : {t, video}) {
            if (!n)
                continue;
            for (const char *key : {"FileName", "Filename"})
                if (!n->stringChild(key).isEmpty())
                    absolute << n->stringChild(key);
            if (!prop70s(*n, "Path").isEmpty())
                absolute << prop70s(*n, "Path");
            if (!n->stringChild("RelativeFilename").isEmpty())
                relative << n->stringChild("RelativeFilename");
        }
        QString found;
        auto tryFile = [&](const QString &p) {
            if (found.isEmpty() && !p.isEmpty() && QFileInfo(p).isFile())
                found = QFileInfo(p).absoluteFilePath();
        };
        for (const QString &n : absolute + relative) {
            const QString p = QDir::fromNativeSeparators(n);
            tryFile(QDir::isAbsolutePath(p) ? p : fbxDir.filePath(p));
        }
        const QStringList names = relative + absolute;
        const QString base = names.isEmpty() ? QString() : QFileInfo(QDir::fromNativeSeparators(names.first())).fileName();
        if (!base.isEmpty()) {
            tryFile(fbxDir.filePath(base));
            tryFile(fbxDir.filePath(QStringLiteral("textures/") + base));
            tryFile(fbxDir.filePath(fbmDir + QLatin1Char('/') + base));
        }
        if (found.isEmpty() && video && opt.extractEmbeddedTextures && !base.isEmpty()) {
            const QByteArray bytes = embeddedContent(*video);
            if (!bytes.isEmpty() && fbxDir.mkpath(fbmDir)) {
                QFile out(fbxDir.filePath(fbmDir + QLatin1Char('/') + base));
                if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size()) {
                    found = QFileInfo(out.fileName()).absoluteFilePath();
                    rep.texturesExtracted << QDir::toNativeSeparators(found);
                }
            }
        }
        // A converted .paa next to the source image is what the game loads.
        if (!found.isEmpty() && QFileInfo(found).suffix().compare(QLatin1String("paa"), Qt::CaseInsensitive) != 0) {
            const QFileInfo src(found);
            const QString paa = src.absoluteDir().filePath(src.completeBaseName() + QStringLiteral(".paa"));
            if (QFileInfo(paa).isFile())
                found = paa;
        }
        QString stored;
        if (!found.isEmpty()) {
            // inside an unpacked mod: the game path ("<prefix>\..."), else
            // relative to the work drive, else absolute
            stored = TextureLibrary::modPath(found);
            if (stored.isEmpty())
                stored = cleanTexturePath(QDir::toNativeSeparators(found), opt);
            ++rep.texturesFound;
        } else if (!names.isEmpty()) {
            // Not on disk: keep the path as written (it may be a P:\ or game
            // data path that the texture library resolves later).
            stored = cleanTexturePath(relative.isEmpty() ? absolute.first() : relative.first(), opt);
            rep.texturesMissing << stored;
        }
        textureCache.insert(texId, stored);
        return stored;
    };

    // --- materials per model (connection order) and their textures
    auto materialInfo = [&](qint64 matId, QString &texture, QString &rvmat) {
        const FNode *m = byId.value(matId);
        QString name = m ? objectName(*m) : QString();
        // Blender keeps material names unique by appending ".001", ".002"...:
        // two materials sharing one .rvmat arrive as "x.rvmat" and "x.rvmat.001".
        static const QRegularExpression blenderSuffix(QStringLiteral("\\.(rvmat|paa|tga|png)\\.\\d{3}$"),
                                                      QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch suffix = blenderSuffix.match(name);
        if (suffix.hasMatch())
            name.chop(4);
        if (name.endsWith(QLatin1String(".rvmat"), Qt::CaseInsensitive))
            rvmat = cleanTexturePath(name, opt);
        qint64 best = 0;
        int bestScore = 0;
        for (qint64 c : orderedChildren.value(matId)) {
            const FNode *t = byId.value(c);
            if (!t || t->name != "Texture")
                continue;
            const int score = textureSlotScore(slotOf.value({c, matId}));
            if (score > bestScore) {
                best = c;
                bestScore = score;
            }
        }
        if (best)
            texture = textureFile(best);
        // Material name "a.paa" is also accepted as texture.
        if (texture.isEmpty() && (name.endsWith(QLatin1String(".paa"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(".tga"), Qt::CaseInsensitive)))
            texture = cleanTexturePath(name, opt);
    };

    QHash<float, ObjectData> perLod;
    QVector<float> lodOrder;

    for (const FNode &geo : objects->children) {
        if (geo.name != "Geometry" || objectClass(geo) != QLatin1String("Mesh"))
            continue;
        // models using this geometry
        QVector<qint64> models;
        for (auto it = orderedChildren.cbegin(); it != orderedChildren.cend(); ++it)
            if (it.value().contains(objectId(geo)) && byId.value(it.key()) && byId.value(it.key())->name == "Model")
                models.append(it.key());
        if (models.isEmpty())
            continue;
        const FNode *vtx = geo.child("Vertices");
        const FNode *pvi = geo.child("PolygonVertexIndex");
        if (!vtx || !pvi)
            continue;
        const QVector<double> verts = vtx->doubles();
        const QVector<qint64> idx = pvi->ints();
        const QVector<const FNode *> uvLayers = geo.all("LayerElementUV");
        const FNode *matLayer = geo.child("LayerElementMaterial");
        const FNode *smooth = geo.child("LayerElementSmoothing");
        const FNode *edgesNode = geo.child("Edges");

        for (qint64 modelId : models) {
            const FNode &model = *byId.value(modelId);
            const float res = lodFor(modelId);
            if (!perLod.contains(res)) {
                perLod.insert(res, ObjectData());
                lodOrder.append(res);
            }
            ObjectData &o = perLod[res];
            while (o.uvSets.size() < std::max<qsizetype>(1, uvLayers.size())) {
                UVSet s;
                s.id = quint32(o.uvSets.size());
                s.uv.resize(o.faces.size());
                o.uvSets.append(s);
            }
            const QMatrix4x4 m = toModel * globalTransform(modelId) * geometricTransform(model);
            // Winding flips when the total transform mirrors space. toModel
            // contains one mirror (Z negation) that is compensated by the
            // model space being left-handed, so test against that.
            auto det3 = [](const QMatrix4x4 &a) {
                return a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0))
                       + a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
            };
            const bool mirrored = det3(m) > 0;

            // materials of this model in connection order
            QVector<QPair<QString, QString>> mats;
            for (qint64 c : orderedChildren.value(modelId)) {
                const FNode *mn = byId.value(c);
                if (mn && mn->name == "Material") {
                    QString tex, rv;
                    materialInfo(c, tex, rv);
                    mats.append({tex, rv});
                }
            }

            const int base = o.points.size();
            const int faceBase = o.faces.size();
            for (int i = 0; i + 2 < verts.size(); i += 3)
                o.addPoint(m.map(QVector3D(float(verts[i]), float(verts[i + 1]), float(verts[i + 2]))));

            // polygons
            int poly = 0;
            QVector<int> pvStartOfPoly;
            for (int start = 0; start < idx.size(); ++poly) {
                int end = start;
                while (end < idx.size() && idx[end] >= 0)
                    ++end;
                if (end >= idx.size())
                    break;
                const int count = end - start + 1;
                pvStartOfPoly.append(start);
                QVector<int> cps(count);
                for (int k = 0; k < count; ++k) {
                    const qint64 v = idx[start + k];
                    cps[k] = int(v < 0 ? ~v : v);
                }
                // UVs per layer
                QVector<QVector<QVector2D>> uv(uvLayers.size(), QVector<QVector2D>(count));
                for (int L = 0; L < uvLayers.size(); ++L) {
                    const FNode *uvn = uvLayers[L]->child("UV");
                    const FNode *uvi = uvLayers[L]->child("UVIndex");
                    const QVector<double> uvv = uvn ? uvn->doubles() : QVector<double>();
                    const QVector<qint64> uvix = uvi ? uvi->ints() : QVector<qint64>();
                    for (int k = 0; k < count; ++k)
                        layerIndex(*uvLayers[L], start + k, poly, cps[k], [&](int i, bool indexed) {
                            if (indexed)
                                i = i < uvix.size() ? int(uvix[i]) : -1;
                            if (i >= 0 && 2 * i + 1 < uvv.size())
                                uv[L][k] = QVector2D(float(uvv[2 * i]), 1.0f - float(uvv[2 * i + 1]));
                            return true;
                        });
                }
                int matIdx = 0;
                if (matLayer) {
                    const QVector<qint64> mi = matLayer->child("Materials") ? matLayer->child("Materials")->ints() : QVector<qint64>();
                    layerIndex(*matLayer, start, poly, cps[0], [&](int i, bool) {
                        matIdx = i < mi.size() ? int(mi[i]) : 0;
                        return true;
                    });
                }
                Face fc;
                if (matIdx >= 0 && matIdx < mats.size()) {
                    fc.texture = mats[matIdx].first;
                    fc.material = mats[matIdx].second;
                }
                // split into triangles/quads (fan)
                QVector<QVector<int>> parts;
                if (count == 3 || count == 4) {
                    QVector<int> q;
                    for (int k = 0; k < count; ++k)
                        q.append(k);
                    parts.append(q);
                } else {
                    for (int k = 1; k + 1 < count; ++k)
                        parts.append({0, k, k + 1});
                }
                for (QVector<int> part : parts) {
                    if (mirrored)
                        std::reverse(part.begin(), part.end());
                    Face f2 = fc;
                    f2.n = part.size();
                    std::array<QVector2D, 4> t0{};
                    for (int k = 0; k < part.size(); ++k) {
                        f2.v[k].point = base + cps[part[k]];
                        if (!uv.isEmpty())
                            t0[k] = uv[0][part[k]];
                    }
                    const int fi = o.addFace(f2, t0);
                    for (int L = 1; L < uvLayers.size() && L < o.uvSets.size(); ++L)
                        for (int k = 0; k < part.size(); ++k)
                            o.uvSets[L].uv[fi][k] = uv[L][part[k]];
                    const int nrm = o.addNormal(o.storedNormal(fi));
                    for (int k = 0; k < f2.n; ++k)
                        o.faces[fi].v[k].normal = nrm;
                    ++rep.faces;
                }
                start = end + 1;
            }

            // sharp edges from per-edge smoothing
            if (smooth && edgesNode && smooth->stringChild("MappingInformationType") == QLatin1String("ByEdge")) {
                const QVector<qint64> sm = smooth->child("Smoothing") ? smooth->child("Smoothing")->ints() : QVector<qint64>();
                const QVector<qint64> edges = edgesNode->ints();
                for (int e = 0; e < edges.size() && e < sm.size(); ++e) {
                    if (sm[e] != 0)
                        continue;
                    const int pv = int(edges[e]);
                    if (pv < 0 || pv >= idx.size())
                        continue;
                    // next polygon-vertex in the same polygon
                    int next = pv + 1;
                    if (idx[pv] < 0) { // last vertex of polygon: wrap to its first
                        next = pv;
                        while (next > 0 && idx[next - 1] >= 0)
                            --next;
                    }
                    const qint64 a = idx[pv] < 0 ? ~idx[pv] : idx[pv];
                    const qint64 b = idx[next] < 0 ? ~idx[next] : idx[next];
                    o.sharpEdges.append({base + int(std::min(a, b)), base + int(std::max(a, b))});
                }
            }

            // named selection = model name
            const QString selName = objectName(model);
            if (!selName.isEmpty()) {
                NamedSelection *ns = o.findSelection(selName);
                if (!ns) {
                    o.namedSelections.append(NamedSelection{selName, Selection{}});
                    ns = &o.namedSelections.last();
                }
                ns->sel.resize(o.points.size(), o.faces.size());
                for (int i = base; i < o.points.size(); ++i)
                    ns->sel.points[i] = 255;
                for (int i = faceBase; i < o.faces.size(); ++i)
                    ns->sel.faces[i] = 1;
                // An object named "proxy:<path>.<n>" is a proxy triangle: no
                // texture or material, points flagged like CICreateProxy does.
                if (selName.startsWith(QLatin1String("proxy:"), Qt::CaseInsensitive)) {
                    for (int i = faceBase; i < o.faces.size(); ++i) {
                        o.faces[i].texture.clear();
                        o.faces[i].material.clear();
                    }
                    for (int i = base; i < o.points.size(); ++i)
                        o.points[i].flags |= PointFlags::Hidden;
                }
            }
            ++rep.meshes;
        }
    }

    if (perLod.isEmpty()) {
        if (error)
            *error = QStringLiteral("No meshes found in the FBX file");
        return false;
    }
    for (float res : lodOrder) {
        ObjectData &o = perLod[res];
        o.normalizeArrays();
        o.recalculateNormals();
        const int i = into.find(res);
        if (i >= 0)
            into.lods[i].data.merge(o);
        else
            into.addLod(res, o);
    }
    rep.lods = int(lodOrder.size());
    if (report)
        *report = rep;
    return true;
}
