#include "UiSpec.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>

bool UiSpec::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_mainMenu = root.value(QLatin1String("mainMenu")).toArray();
    indexMenu(m_mainMenu);

    const QJsonObject ctx = root.value(QLatin1String("contextMenus")).toObject();
    for (auto it = ctx.begin(); it != ctx.end(); ++it)
        m_contextMenus.insert(it.key(), it.value().toArray());

    for (const QJsonValue &v : root.value(QLatin1String("accelerators")).toArray()) {
        const QJsonObject a = v.toObject();
        const QKeySequence seq(a.value(QLatin1String("key")).toString());
        if (!seq.isEmpty())
            m_shortcuts[a.value(QLatin1String("id")).toInt()].append(seq);
    }
    const QJsonObject tips = root.value(QLatin1String("tips")).toObject();
    for (auto it = tips.begin(); it != tips.end(); ++it)
        m_tips.insert(it.key().toInt(), it.value().toString());
    const QJsonObject undo = root.value(QLatin1String("undoText")).toObject();
    for (auto it = undo.begin(); it != undo.end(); ++it)
        m_undo.insert(it.key().toInt(), it.value().toString());

    for (const QJsonValue &v : root.value(QLatin1String("toolbars")).toArray()) {
        const QJsonObject t = v.toObject();
        Toolbar tb;
        tb.resource = t.value(QLatin1String("resource")).toInt();
        tb.name = t.value(QLatin1String("name")).toString();
        for (const QJsonValue &id : t.value(QLatin1String("ids")).toArray())
            tb.ids.append(id.toInt());
        loadIcons(tb);
        m_toolbars.append(tb);
    }
    return true;
}

void UiSpec::indexMenu(const QJsonArray &items)
{
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        if (o.contains(QLatin1String("items"))) {
            indexMenu(o.value(QLatin1String("items")).toArray());
        } else if (o.contains(QLatin1String("id"))) {
            const int id = o.value(QLatin1String("id")).toInt();
            if (!m_menuText.contains(id))
                m_menuText.insert(id, o.value(QLatin1String("text")).toString().section(QLatin1Char('\t'), 0, 0).trimmed());
        }
    }
}

void UiSpec::loadIcons(const Toolbar &tb)
{
    QImage strip(QStringLiteral(":/toolbars/tb_%1.bmp").arg(tb.resource));
    if (strip.isNull())
        return;
    strip = strip.convertToFormat(QImage::Format_ARGB32);
    const int h = strip.height();
    int buttons = 0;
    for (int id : tb.ids)
        buttons += id != 0;
    if (buttons == 0)
        return;
    const int w = strip.width() / buttons;
    // MFC toolbar bitmaps use light grey (and sometimes magenta) as background.
    for (int y = 0; y < strip.height(); ++y)
        for (int x = 0; x < strip.width(); ++x) {
            const QRgb c = strip.pixel(x, y);
            if (c == qRgb(192, 192, 192) || c == qRgb(255, 0, 255))
                strip.setPixel(x, y, qRgba(0, 0, 0, 0));
        }
    int index = 0;
    for (int id : tb.ids) {
        if (id == 0)
            continue;
        if (!m_icons.contains(id))
            m_icons.insert(id, QIcon(QPixmap::fromImage(strip.copy(index * w, 0, w, h))));
        ++index;
    }
}

QString UiSpec::statusTip(int id) const
{
    return m_tips.value(id).section(QLatin1Char('\n'), 0, 0);
}

QString UiSpec::toolTip(int id) const
{
    const QString t = m_tips.value(id);
    return t.contains(QLatin1Char('\n')) ? t.section(QLatin1Char('\n'), 1) : QString();
}
