#pragma once

#include <QByteArray>
#include <QHash>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

// PBO archive (Bohemia "bank"). Header: list of entries
//   asciiz name ; u32 method ; u32 originalSize ; u32 reserved ; u32 timestamp ; u32 dataSize
// terminated by an empty name. A first entry with method 'Vers' (0x56657273)
// and empty name carries key/value properties (prefix, product, version...).
// Entry data follows the header in order; method 'Cprs' (0x43707273) entries
// are LZSS compressed.
class PboArchive {
public:
    struct Entry {
        QString name;     // as stored, backslash separated
        quint32 method = 0;
        quint32 originalSize = 0;
        quint32 dataSize = 0;
        qint64 offset = 0; // absolute file offset of the data
    };

    bool open(const QString &path, QString *error = nullptr);
    QString path() const { return m_path; }
    QString prefix() const { return m_props.value(QStringLiteral("prefix")); }
    const QHash<QString, QString> &properties() const { return m_props; }
    const QVector<Entry> &entries() const { return m_entries; }
    bool read(const Entry &e, QByteArray &out, QString *error = nullptr) const;

private:
    QString m_path;
    QHash<QString, QString> m_props;
    QVector<Entry> m_entries;
};

// Virtual game file system: PBO archives (addressed by their prefix) plus
// loose folders (e.g. P:\). Paths are case-insensitive, '\' separated,
// without a leading backslash ("dz\weapons\firearms\akm\akm.p3d").
class GameFileSystem {
public:
    static GameFileSystem &instance();

    // Indexes every *.pbo under `folder` (recursively). Only headers are read.
    int addPboFolder(const QString &folder);
    bool addPbo(const QString &file);
    void addLooseFolder(const QString &folder);
    void clear();

    static QString normalize(QString path);
    bool exists(const QString &path) const;
    bool read(const QString &path, QByteArray &out, QString *error = nullptr) const;
    // All indexed files ending with `suffix` (normalized paths).
    QStringList files(const QString &suffix = QString()) const;
    int pboCount() const { return int(m_pbos.size()); }

private:
    struct Location {
        int pbo;
        int entry;
    };
    QVector<std::shared_ptr<PboArchive>> m_pbos;
    QHash<QString, Location> m_index;
    QStringList m_loose;
    mutable QReadWriteLock m_lock;
};
