#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

// Resolves face texture paths ("dz\weapons\...\akm_co.paa", "P:\...",
// "#(argb,8,8,3)color(...)") to decoded images. Lookup order:
//   procedural -> absolute file -> model folder -> the model's mod folder
//   (folders above the model: a "$PBOPREFIX$" there maps its prefix to that
//   folder, and each parent folder is tried as a root, so an unpacked mod
//   such as <x>\StalkerFWWeapons\rifles\ak103\ak103.p3d finds
//   "stalkerfwweapons\rifles\ak74\data\ak101_co.paa") -> work drive / extra
//   roots -> game PBOs (GameFileSystem). Results (including failures) are cached.
class TextureLibrary {
public:
    static TextureLibrary &instance();

    void setModelFolder(const QString &folder);
    void setSearchRoots(const QStringList &roots); // e.g. "P:/"
    void setMaxSize(int px) { m_maxSize = px; }

    // Returns a null image when the texture cannot be found/decoded.
    QImage image(const QString &texturePath);
    bool isCached(const QString &texturePath) const;
    void clear();

private:
    QImage load(const QString &path) const;

    struct PrefixRoot {
        QString prefix; // lower case, backslashes, no trailing '\'; empty = plain root
        QString folder;
    };

    mutable QMutex m_mutex;
    QHash<QString, QImage> m_cache;
    QString m_modelFolder;
    QVector<PrefixRoot> m_modRoots; // derived from the model folder
    QStringList m_roots;
    int m_maxSize = 1024;
};
