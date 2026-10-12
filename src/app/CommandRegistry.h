#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

#include <functional>

class QAction;

// Command IDs match the original's menu/toolbar resource IDs (resources/
// ui_spec.json), so the generated menus wire up automatically. New features
// that have no original counterpart use IDs >= CommandRegistry::FirstCustomId
// and declare where they appear with Command::menuPath.
//
// To add a feature:
//   reg.add({ID_FACES_REVERSE, [=] { ... }, hasSelectedFaces});
// or for a new command:
//   reg.add({CommandRegistry::FirstCustomId + 1, run, enabled, {}, "&Tools", "My Tool", QKeySequence("Ctrl+Alt+M")});
struct Command {
    int id = 0;
    std::function<void()> run;
    std::function<bool()> enabled;  // empty = always enabled
    std::function<bool()> checked;  // non-empty makes the action checkable
    // Only for custom commands (not present in the original menu):
    QString menuPath;   // e.g. "&Tools" or "&Tools/Sub menu"
    QString text;
    QKeySequence shortcut;
};

class CommandRegistry : public QObject {
    Q_OBJECT
public:
    static constexpr int FirstCustomId = 40000;

    explicit CommandRegistry(QObject *parent = nullptr) : QObject(parent) {}

    void add(const Command &cmd);
    bool has(int id) const { return m_commands.contains(id); }
    const Command *find(int id) const;
    QList<int> customIds() const;

    // Binds an action (created from the UI spec) to the command with that id.
    void bind(int id, QAction *action);
    QAction *action(int id) const { return m_actions.value(id); }

    void trigger(int id);
    // Refresh enabled/checked state of every bound action.
    void updateActions();

private:
    QHash<int, Command> m_commands;
    QHash<int, QAction *> m_actions;
    QList<int> m_customOrder;
};
