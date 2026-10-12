#pragma once

#include "ui/UiSpec.h"
#include "ui/Viewport.h"

#include <QHash>
#include <QJsonArray>
#include <QMainWindow>
#include <QMatrix4x4>

class CommandRegistry;
class Document;
class LodPanel;
class MassPanel;
class NamedPropertiesPanel;
class NamedSelectionsPanel;
class PositionPanel;
class QDockWidget;
class QGridLayout;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Document *document() const { return m_doc; }
    CommandRegistry *registry() const { return m_registry; }
    ViewSettings *viewSettings() { return &m_view; }
    const UiSpec &uiSpec() const { return m_spec; }

    // --- views ------------------------------------------------------------
    Viewport *activeViewport() const { return m_active; }
    QList<Viewport *> viewports() const { return m_viewports; }
    bool fourViews() const { return m_fourViews; }
    void setFourViews(bool on);
    void refreshViews();
    // Orientation + pin used to place newly created geometry.
    QMatrix4x4 creationFrame() const;
    QVector3D viewDirection() const;

    // --- files ------------------------------------------------------------
    bool maybeSave();
    bool openFile(const QString &path);
    bool openGameFile(const QString &vfsPath); // path inside the game archives
    bool saveFile(const QString &path);
    bool saveAs();
    void addRecentFile(const QString &path);

    // --- panels -----------------------------------------------------------
    QDockWidget *dockFor(int commandId) const { return m_docks.value(commandId); }
    QString currentNamedSelection() const;
    QString currentNamedProperty() const;

    void showStatus(const QString &text, int ms = 4000);
    QString undoText(int id, const QString &fallback) const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildViews();
    void buildDocks();
    void buildMenus();
    void buildToolbars();
    QAction *actionFor(int id, const QString &text = QString());
    void fillMenu(QMenu *menu, const QJsonArray &items);
    QMenu *contextMenu(const QString &specName, QWidget *parent);
    void rebuildRecentMenu();
    void afterOpen(const QString &name, const QString &folder);
    void updateTitle();
    void updateStatus();
    void setActive(Viewport *v);
    void addCustomCommands();

    Document *m_doc;
    CommandRegistry *m_registry;
    UiSpec m_spec;
    ViewSettings m_view;

    QWidget *m_viewHost = nullptr;
    QGridLayout *m_viewGrid = nullptr;
    QList<Viewport *> m_viewports;
    Viewport *m_active = nullptr;
    bool m_fourViews = true;

    QHash<int, QAction *> m_actions;
    QHash<int, QDockWidget *> m_docks;
    QMenu *m_recentMenu = nullptr;
    LodPanel *m_lodPanel = nullptr;
    NamedSelectionsPanel *m_nselPanel = nullptr;
    NamedPropertiesPanel *m_npropPanel = nullptr;
    MassPanel *m_massPanel = nullptr;
    PositionPanel *m_posPanel = nullptr;
    QLabel *m_statusCounts = nullptr;
};
