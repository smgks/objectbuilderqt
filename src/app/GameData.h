#pragma once

#include <QObject>
#include <QString>

// Game data setup: where DayZ and the work drive are, and background indexing
// of the game's PBO archives into GameFileSystem.
class GameData : public QObject {
    Q_OBJECT
public:
    static GameData &instance();

    static QString gameFolder();   // CConfig "\DayZ folder" or auto-detected
    static QString workDrive();    // CConfig "\Work drive (P:)" or "P:/" if present
    void startIndexing();          // asynchronous; emits indexed()
    bool isIndexed() const { return m_indexed; }
    int pboCount() const { return m_pbos; }

signals:
    void indexed(int pboCount);

private:
    bool m_indexed = false;
    bool m_running = false;
    int m_pbos = 0;
};
