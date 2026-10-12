#include "TextureLibrary.h"

#include "Paa.h"
#include "Pbo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

TextureLibrary &TextureLibrary::instance()
{
    static TextureLibrary lib;
    return lib;
}

namespace {

// Reads the prefix from a "$PBOPREFIX$" file ("a\b\c" or "prefix=a\b\c").
QString readPrefix(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    for (QString line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QLatin1String("prefix="), Qt::CaseInsensitive))
            line = line.mid(7).trimmed();
        if (line.isEmpty() || line.contains(QLatin1Char('=')))
            continue;
        line.replace(QLatin1Char('/'), QLatin1Char('\\'));
        while (line.startsWith(QLatin1Char('\\')))
            line.remove(0, 1);
        while (line.endsWith(QLatin1Char('\\')))
            line.chop(1);
        return line.toLower();
    }
    return {};
}

} // namespace

QString TextureLibrary::modPath(const QString &absoluteFile)
{
    const QFileInfo fi(absoluteFile);
    QDir d = fi.absoluteDir();
    for (int depth = 0; depth < 12; ++depth) {
        for (const QString &name : d.entryList({QStringLiteral("$pboprefix$*"), QStringLiteral("$prefix$*")}, QDir::Files)) {
            const QString prefix = readPrefix(d.filePath(name));
            if (!prefix.isEmpty())
                return (prefix + QLatin1Char('\\') + QDir::toNativeSeparators(d.relativeFilePath(fi.absoluteFilePath()))).toLower();
        }
        if (!d.cdUp())
            break;
    }
    return {};
}

void TextureLibrary::setModelFolder(const QString &folder)
{
    // Folders above the model, nearest first: prefix mappings from
    // $PBOPREFIX$ files, then each folder as a plain root.
    QVector<PrefixRoot> mod;
    if (!folder.isEmpty()) {
        QVector<PrefixRoot> plain;
        QDir d(folder);
        for (int depth = 0; depth < 12; ++depth) {
            for (const QString &name : d.entryList({QStringLiteral("$pboprefix$*"), QStringLiteral("$prefix$*")}, QDir::Files)) {
                const QString prefix = readPrefix(d.filePath(name));
                if (!prefix.isEmpty())
                    mod.append({prefix, d.absolutePath()});
            }
            if (depth > 0)
                plain.append({QString(), d.absolutePath()});
            if (!d.cdUp())
                break;
        }
        mod += plain;
    }
    QMutexLocker lock(&m_mutex);
    if (folder != m_modelFolder) {
        m_modelFolder = folder;
        m_modRoots = mod;
        // Relative lookups may now resolve differently; drop failures.
        for (auto it = m_cache.begin(); it != m_cache.end();)
            it = it->isNull() ? m_cache.erase(it) : std::next(it);
    }
}

void TextureLibrary::setSearchRoots(const QStringList &roots)
{
    QMutexLocker lock(&m_mutex);
    m_roots = roots;
    m_cache.clear();
}

bool TextureLibrary::isCached(const QString &path) const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(path.toLower());
}

void TextureLibrary::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
}

QImage TextureLibrary::image(const QString &texturePath)
{
    const QString key = texturePath.trimmed().toLower();
    if (key.isEmpty())
        return {};
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_cache.constFind(key);
        if (it != m_cache.constEnd())
            return *it;
    }
    const QImage img = load(texturePath.trimmed());
    QMutexLocker lock(&m_mutex);
    m_cache.insert(key, img);
    return img;
}

QImage TextureLibrary::load(const QString &path) const
{
    if (path.startsWith(QLatin1Char('#')))
        return Paa::procedural(path);

    auto decodeFile = [this](const QString &file) -> QImage {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QByteArray data = f.readAll();
        const QString ext = QFileInfo(file).suffix().toLower();
        if (ext == QLatin1String("paa") || ext == QLatin1String("pac"))
            return Paa::decode(data, m_maxSize);
        QImage img;
        img.loadFromData(data);
        return img;
    };

    QString rel = path;
    rel.replace(QLatin1Char('/'), QLatin1Char('\\'));
    // 1. absolute
    if (QFileInfo(path).isAbsolute() && QFileInfo::exists(path)) {
        const QImage img = decodeFile(path);
        if (!img.isNull())
            return img;
        if (rel.size() > 3 && rel[1] == QLatin1Char(':'))
            rel = rel.mid(3); // try the drive-relative part in the game data
    }
    while (rel.startsWith(QLatin1Char('\\')))
        rel.remove(0, 1);
    const QString relSlash = QString(rel).replace(QLatin1Char('\\'), QLatin1Char('/'));

    QString modelFolder;
    QVector<PrefixRoot> roots;
    {
        QMutexLocker lock(&m_mutex);
        modelFolder = m_modelFolder;
        if (!modelFolder.isEmpty())
            roots.append({QString(), modelFolder});
        roots += m_modRoots;
        for (const QString &r : m_roots)
            roots.append({QString(), r});
    }
    // 2./3. loose folders; also try the source image formats binarize accepts
    const QString base = relSlash.left(relSlash.lastIndexOf(QLatin1Char('.')));
    const QString relLower = rel.toLower();
    for (const PrefixRoot &root : roots) {
        QString sub = relSlash, subBase = base;
        if (!root.prefix.isEmpty()) {
            if (!relLower.startsWith(root.prefix + QLatin1Char('\\')))
                continue;
            sub = relSlash.mid(root.prefix.size() + 1);
            subBase = base.mid(root.prefix.size() + 1);
        }
        for (const QString &cand : {sub, subBase + QStringLiteral(".paa"), subBase + QStringLiteral(".tga"), subBase + QStringLiteral(".png")}) {
            const QString f = QDir(root.folder).filePath(cand);
            if (QFileInfo::exists(f)) {
                const QImage img = decodeFile(f);
                if (!img.isNull())
                    return img;
            }
        }
    }
    // the texture may sit next to the model under its file name only
    if (!modelFolder.isEmpty()) {
        const QString f = QDir(modelFolder).filePath(QFileInfo(relSlash).fileName());
        if (QFileInfo::exists(f)) {
            const QImage img = decodeFile(f);
            if (!img.isNull())
                return img;
        }
    }
    // 4. game data
    QByteArray data;
    GameFileSystem &fs = GameFileSystem::instance();
    for (const QString &cand : {rel, QString(base).replace(QLatin1Char('/'), QLatin1Char('\\')) + QStringLiteral(".paa")})
        if (fs.read(cand, data))
            return Paa::decode(data, m_maxSize);
    return {};
}
