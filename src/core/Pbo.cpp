#include "Pbo.h"

#include "Compression.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr quint32 kVers = 0x56657273;
constexpr quint32 kCprs = 0x43707273;
} // namespace

bool PboArchive::open(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_path = path;
    m_entries.clear();
    m_props.clear();

    // Headers are small; read in growing chunks until the terminator is found.
    QByteArray buf;
    qsizetype pos = 0;
    auto ensure = [&](qsizetype n) {
        while (pos + n > buf.size()) {
            const QByteArray more = f.read(std::max<qint64>(1 << 20, n));
            if (more.isEmpty())
                return false;
            buf += more;
        }
        return true;
    };
    auto asciiz = [&](QString &s) {
        for (;;) {
            if (!ensure(1))
                return false;
            const qsizetype z = buf.indexOf('\0', pos);
            if (z >= 0) {
                s = QString::fromLocal8Bit(buf.constData() + pos, z - pos);
                pos = z + 1;
                return true;
            }
            if (!ensure(buf.size() - pos + 1))
                return false;
        }
    };
    auto u32 = [&](quint32 &v) {
        if (!ensure(4))
            return false;
        const auto *p = reinterpret_cast<const quint8 *>(buf.constData() + pos);
        v = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
        pos += 4;
        return true;
    };

    for (;;) {
        Entry e;
        quint32 reserved, ts;
        if (!asciiz(e.name) || !u32(e.method) || !u32(e.originalSize) || !u32(reserved) || !u32(ts) || !u32(e.dataSize)) {
            if (error)
                *error = QStringLiteral("Truncated PBO header");
            return false;
        }
        if (e.name.isEmpty() && e.method == kVers) {
            for (;;) {
                QString k, v;
                if (!asciiz(k))
                    return false;
                if (k.isEmpty())
                    break;
                if (!asciiz(v))
                    return false;
                m_props.insert(k.toLower(), v);
            }
            continue;
        }
        if (e.name.isEmpty())
            break;
        m_entries.append(e);
    }
    qint64 off = pos;
    for (Entry &e : m_entries) {
        e.offset = off;
        off += e.dataSize;
    }
    return true;
}

bool PboArchive::read(const Entry &e, QByteArray &out, QString *error) const
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(e.offset)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray raw = f.read(e.dataSize);
    if (raw.size() != qsizetype(e.dataSize)) {
        if (error)
            *error = QStringLiteral("Truncated PBO entry %1").arg(e.name);
        return false;
    }
    if (e.method == kCprs && e.originalSize != e.dataSize) {
        if (!Compression::lzss(raw.constData(), raw.size(), e.originalSize, out)) {
            if (error)
                *error = QStringLiteral("Corrupt compressed PBO entry %1").arg(e.name);
            return false;
        }
        return true;
    }
    out = raw;
    return true;
}

// ---------------------------------------------------------------- GameFileSystem

GameFileSystem &GameFileSystem::instance()
{
    static GameFileSystem fs;
    return fs;
}

QString GameFileSystem::normalize(QString p)
{
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    while (p.startsWith(QLatin1Char('\\')))
        p.remove(0, 1);
    return p.toLower();
}

bool GameFileSystem::addPbo(const QString &file)
{
    auto pbo = std::make_shared<PboArchive>();
    if (!pbo->open(file))
        return false;
    QWriteLocker lock(&m_lock);
    const int idx = int(m_pbos.size());
    m_pbos.append(pbo);
    QString prefix = normalize(pbo->prefix());
    if (prefix.isEmpty())
        prefix = QFileInfo(file).completeBaseName().toLower();
    if (!prefix.endsWith(QLatin1Char('\\')))
        prefix += QLatin1Char('\\');
    const auto &entries = pbo->entries();
    for (int i = 0; i < entries.size(); ++i)
        m_index.insert(prefix + normalize(entries[i].name), Location{idx, i});
    return true;
}

int GameFileSystem::addPboFolder(const QString &folder)
{
    int n = 0;
    QDirIterator it(folder, {QStringLiteral("*.pbo")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        n += addPbo(it.next());
    return n;
}

void GameFileSystem::addLooseFolder(const QString &folder)
{
    QWriteLocker lock(&m_lock);
    if (!m_loose.contains(folder))
        m_loose.append(folder);
}

void GameFileSystem::clear()
{
    QWriteLocker lock(&m_lock);
    m_pbos.clear();
    m_index.clear();
    m_loose.clear();
}

bool GameFileSystem::exists(const QString &path) const
{
    QReadLocker lock(&m_lock);
    const QString n = normalize(path);
    if (m_index.contains(n))
        return true;
    for (const QString &root : m_loose)
        if (QFileInfo::exists(QDir(root).filePath(QString(n).replace(QLatin1Char('\\'), QLatin1Char('/')))))
            return true;
    return false;
}

bool GameFileSystem::read(const QString &path, QByteArray &out, QString *error) const
{
    QReadLocker lock(&m_lock);
    const QString n = normalize(path);
    // Loose files override packed ones, like a mounted work drive.
    for (const QString &root : m_loose) {
        QFile f(QDir(root).filePath(QString(n).replace(QLatin1Char('\\'), QLatin1Char('/'))));
        if (f.open(QIODevice::ReadOnly)) {
            out = f.readAll();
            return true;
        }
    }
    auto it = m_index.constFind(n);
    if (it == m_index.constEnd()) {
        if (error)
            *error = QStringLiteral("File not found: %1").arg(path);
        return false;
    }
    const PboArchive &pbo = *m_pbos[it->pbo];
    return pbo.read(pbo.entries()[it->entry], out, error);
}

QStringList GameFileSystem::files(const QString &suffix) const
{
    QReadLocker lock(&m_lock);
    QStringList out;
    const QString s = suffix.toLower();
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it)
        if (s.isEmpty() || it.key().endsWith(s))
            out.append(it.key());
    out.sort();
    return out;
}
