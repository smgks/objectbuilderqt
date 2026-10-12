#include "CommandRegistry.h"

#include <QAction>

void CommandRegistry::add(const Command &cmd)
{
    if (!m_commands.contains(cmd.id) && cmd.id >= FirstCustomId)
        m_customOrder.append(cmd.id);
    m_commands.insert(cmd.id, cmd);
    if (QAction *a = m_actions.value(cmd.id))
        bind(cmd.id, a);
}

const Command *CommandRegistry::find(int id) const
{
    auto it = m_commands.constFind(id);
    return it == m_commands.constEnd() ? nullptr : &it.value();
}

QList<int> CommandRegistry::customIds() const
{
    return m_customOrder;
}

void CommandRegistry::bind(int id, QAction *action)
{
    m_actions.insert(id, action);
    action->disconnect(this);
    const Command *cmd = find(id);
    if (!cmd) {
        action->setEnabled(false);
        if (!action->statusTip().contains(QLatin1String("not implemented")))
            action->setStatusTip(action->statusTip() + tr(" (not implemented yet)"));
        return;
    }
    action->setStatusTip(action->statusTip().remove(tr(" (not implemented yet)")));
    action->setCheckable(bool(cmd->checked));
    connect(action, &QAction::triggered, this, [this, id] { trigger(id); });
}

void CommandRegistry::trigger(int id)
{
    const Command *cmd = find(id);
    if (!cmd || !cmd->run)
        return;
    if (cmd->enabled && !cmd->enabled())
        return;
    cmd->run();
    updateActions();
}

void CommandRegistry::updateActions()
{
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const Command *cmd = find(it.key());
        if (!cmd)
            continue;
        QAction *a = it.value();
        a->setEnabled(!cmd->enabled || cmd->enabled());
        if (cmd->checked) {
            const QSignalBlocker block(a);
            a->setChecked(cmd->checked());
        }
    }
}
