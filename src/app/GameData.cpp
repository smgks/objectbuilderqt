#include "GameData.h"

#include "core/Pbo.h"
#include "core/TextureLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QThreadPool>

GameData &GameData::instance()
{
    static GameData g;
    return g;
}

QString GameData::gameFolder()
{
    const QString configured = QSettings().value(QStringLiteral("CConfig/\\DayZ folder")).toString();
    if (!configured.isEmpty())
        return configured;
    const QStringList guesses = {QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/DayZ"),
                                 QStringLiteral("C:/Program Files/Steam/steamapps/common/DayZ"),
                                 QStringLiteral("D:/SteamLibrary/steamapps/common/DayZ")};
    for (const QString &g : guesses)
        if (QFileInfo::exists(g + QStringLiteral("/Addons")))
            return g;
    return {};
}

QString GameData::workDrive()
{
    const QString configured = QSettings().value(QStringLiteral("CConfig/\\Work drive (P:)")).toString();
    if (!configured.isEmpty())
        return configured;
    return QFileInfo::exists(QStringLiteral("P:/")) ? QStringLiteral("P:/") : QString();
}

void GameData::startIndexing()
{
    if (m_running)
        return;
    m_running = true;
    m_indexed = false;
    const QString game = gameFolder();
    const QString work = workDrive();
    TextureLibrary::instance().setSearchRoots(work.isEmpty() ? QStringList() : QStringList{work});
    QThreadPool::globalInstance()->start([this, game, work] {
        GameFileSystem &fs = GameFileSystem::instance();
        fs.clear();
        if (!work.isEmpty())
            fs.addLooseFolder(work);
        int n = 0;
        if (!game.isEmpty()) {
            n += fs.addPboFolder(QDir(game).filePath(QStringLiteral("Addons")));
            n += fs.addPboFolder(QDir(game).filePath(QStringLiteral("dta")));
        }
        QMetaObject::invokeMethod(this, [this, n] {
            m_running = false;
            m_indexed = true;
            m_pbos = n;
            TextureLibrary::instance().clear();
            emit indexed(n);
        });
    });
}
