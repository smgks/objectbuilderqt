#include "MainWindow.h"

#include "app/CommandIds.h"
#include "app/CommandRegistry.h"
#include "app/Commands.h"
#include "app/Document.h"
#include "ui/Panels.h"
#include "app/GameData.h"
#include "core/Pbo.h"
#include "core/TextureLibrary.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoView>

namespace {
constexpr int kMaxRecent = 8;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_doc = new Document(this);
    m_registry = new CommandRegistry(this);
    m_spec.load();

    QSettings s;
    m_view.grid = s.value(QStringLiteral("View/grid"), true).toBool();
    m_view.solid = s.value(QStringLiteral("View/solid"), false).toBool();
    m_fourViews = s.value(QStringLiteral("CConfig/#Start in four views"), true).toBool();
    m_view.background = s.value(QStringLiteral("CConfig/CBackgroun Color"), QColor(192, 192, 192)).value<QColor>();

    buildViews();
    buildDocks();

    AppContext ctx{this, m_doc, m_registry, &m_view};
    registerFileCommands(ctx);
    registerEditCommands(ctx);
    registerGeometryCommands(ctx);
    registerViewCommands(ctx);
    registerImportCommands(ctx);

    buildMenus();
    buildToolbars();

    m_statusCounts = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusCounts);

    connect(m_doc, &Document::changed, this, [this] {
        updateStatus();
        m_registry->updateActions();
    });
    connect(m_doc, &Document::fileChanged, this, &MainWindow::updateTitle);
    connect(m_doc->undoStack(), &QUndoStack::indexChanged, m_registry, &CommandRegistry::updateActions);

    resize(1400, 900);
    restoreGeometry(s.value(QStringLiteral("MainWindow/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("MainWindow/state")).toByteArray());
    setFourViews(m_fourViews);
    updateTitle();
    updateStatus();
    connect(&GameData::instance(), &GameData::indexed, this, [this](int n) {
        ++m_view.textureGeneration;
        emit m_doc->changed(0);
        showStatus(tr("Game data: %1 archives indexed").arg(n), 6000);
    });
    GameData::instance().startIndexing();
    m_registry->updateActions();
}

MainWindow::~MainWindow()
{
    // The document is the first child and would be destroyed before the views
    // and panels that observe it; its undo stack emits signals while dying.
    // Tear down observers first.
    QObject::disconnect(m_doc->undoStack(), nullptr, nullptr, nullptr);
    QObject::disconnect(m_doc, nullptr, nullptr, nullptr);
    qDeleteAll(m_docks);
    m_docks.clear();
    delete takeCentralWidget();
    m_viewports.clear();
    m_active = nullptr;
}

// ---------------------------------------------------------------- views

void MainWindow::buildViews()
{
    m_viewHost = new QWidget(this);
    m_viewGrid = new QGridLayout(m_viewHost);
    m_viewGrid->setContentsMargins(0, 0, 0, 0);
    m_viewGrid->setSpacing(2);
    // "Four views (F,L,T,3D)"
    const ViewType types[4] = {ViewType::Front, ViewType::Left, ViewType::Top, ViewType::Projected};
    for (int i = 0; i < 4; ++i) {
        auto *v = new Viewport(m_doc, &m_view, types[i], m_viewHost);
        m_viewGrid->addWidget(v, i / 2, i % 2);
        m_viewports.append(v);
        connect(v, &Viewport::activated, this, &MainWindow::setActive);
        connect(v, &Viewport::cursorPosition, this, [this](const QVector3D &p) {
            if (m_posPanel)
                m_posPanel->setCursor3D(p);
        });
        connect(v, &Viewport::contextMenuRequested, this, [this](const QPoint &pos) {
            // Edit menu doubles as the viewport context menu.
            const QJsonArray menu = m_spec.mainMenu();
            for (const QJsonValue &item : menu)
                if (item.toObject().value(QLatin1String("text")).toString() == QLatin1String("&Edit")) {
                    QMenu m;
                    fillMenu(&m, item.toObject().value(QLatin1String("items")).toArray());
                    m.exec(pos);
                }
        });
    }
    setCentralWidget(m_viewHost);
    setActive(m_viewports[3]);
}

void MainWindow::setActive(Viewport *v)
{
    if (m_active == v)
        return;
    if (m_active)
        m_active->setActiveFrame(false);
    m_active = v;
    m_active->setActiveFrame(m_fourViews);
    m_registry->updateActions();
}

void MainWindow::setFourViews(bool on)
{
    m_fourViews = on;
    for (Viewport *v : m_viewports)
        v->setVisible(on || v == m_active);
    if (m_active)
        m_active->setActiveFrame(on);
    m_registry->updateActions();
}

void MainWindow::refreshViews()
{
    for (Viewport *v : m_viewports)
        v->update();
}

QMatrix4x4 MainWindow::creationFrame() const
{
    QVector3D r, u, f;
    m_active->basis(r, u, f);
    // Local X/Y/Z map to the view's right/up/forward, so flat primitives face
    // the camera they were created from (as in the original).
    QMatrix4x4 m(r.x(), u.x(), f.x(), 0, r.y(), u.y(), f.y(), 0, r.z(), u.z(), f.z(), 0, 0, 0, 0, 1);
    QMatrix4x4 t;
    t.translate(m_doc->pin());
    return t * m;
}

QVector3D MainWindow::viewDirection() const
{
    QVector3D r, u, f;
    m_active->basis(r, u, f);
    return f;
}

// ---------------------------------------------------------------- docks

void MainWindow::buildDocks()
{
    const MenuFactory factory = [this](const QString &name, QWidget *parent) { return contextMenu(name, parent); };
    auto addDock = [this](int cmd, const QString &title, QWidget *w, Qt::DockWidgetArea area) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(QStringLiteral("dock_%1").arg(cmd));
        d->setWidget(w);
        addDockWidget(area, d);
        m_docks.insert(cmd, d);
        connect(d, &QDockWidget::visibilityChanged, m_registry, &CommandRegistry::updateActions);
        return d;
    };
    m_lodPanel = new LodPanel(m_doc, factory, this);
    connect(m_lodPanel, &LodPanel::propertiesRequested, this, [this] { m_registry->trigger(Cmd::LodProperties); });
    addDock(Cmd::WinLods, tr("LODs"), m_lodPanel, Qt::LeftDockWidgetArea);

    m_nselPanel = new NamedSelectionsPanel(m_doc, factory, this);
    addDock(Cmd::WinNamedSelections, tr("Named Selections"), m_nselPanel, Qt::RightDockWidgetArea);

    m_npropPanel = new NamedPropertiesPanel(m_doc, factory, this);
    connect(m_npropPanel, &NamedPropertiesPanel::editRequested, this, [this] { m_registry->trigger(Cmd::NpropEdit); });
    addDock(Cmd::WinNamedProps, tr("Named Properties"), m_npropPanel, Qt::RightDockWidgetArea);

    m_massPanel = new MassPanel(m_doc, this);
    addDock(Cmd::WinMass, tr("Mass"), m_massPanel, Qt::RightDockWidgetArea)->hide();

    auto *history = new QUndoView(m_doc->undoStack(), this);
    history->setEmptyLabel(tr("<original>"));
    addDock(Cmd::WinHistory, tr("History"), history, Qt::LeftDockWidgetArea);

    m_posPanel = new PositionPanel(m_doc, this);
    addDock(Cmd::WinPosition, tr("Current position"), m_posPanel, Qt::LeftDockWidgetArea)->hide();
}

QString MainWindow::currentNamedSelection() const
{
    return m_nselPanel->currentName();
}

QString MainWindow::currentNamedProperty() const
{
    return m_npropPanel->currentName();
}

// ---------------------------------------------------------------- menus

QAction *MainWindow::actionFor(int id, const QString &text)
{
    if (QAction *a = m_actions.value(id))
        return a;
    QString label = text.isEmpty() ? m_spec.menuText(id) : text.section(QLatin1Char('\t'), 0, 0).trimmed();
    if (label.isEmpty())
        label = m_spec.toolTip(id).isEmpty() ? m_spec.statusTip(id) : m_spec.toolTip(id);
    auto *a = new QAction(label, this);
    a->setStatusTip(m_spec.statusTip(id));
    if (!m_spec.toolTip(id).isEmpty())
        a->setToolTip(m_spec.toolTip(id));
    const QIcon icon = m_spec.icon(id);
    if (!icon.isNull())
        a->setIcon(icon);
    const QList<QKeySequence> keys = m_spec.shortcuts(id);
    if (!keys.isEmpty()) {
        a->setShortcuts(keys);
        a->setShortcutContext(Qt::WindowShortcut);
    }
    addAction(a); // shortcuts work even if the action is only in a toolbar
    m_actions.insert(id, a);
    m_registry->bind(id, a);
    return a;
}

void MainWindow::fillMenu(QMenu *menu, const QJsonArray &items)
{
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("separator")).toBool()) {
            menu->addSeparator();
        } else if (o.contains(QLatin1String("items"))) {
            QMenu *sub = menu->addMenu(o.value(QLatin1String("text")).toString());
            fillMenu(sub, o.value(QLatin1String("items")).toArray());
        } else {
            const int id = o.value(QLatin1String("id")).toInt();
            if (id == Cmd::FileRecent) { // MFC MRU placeholder
                m_recentMenu = menu->addMenu(tr("Recent Files"));
                rebuildRecentMenu();
                continue;
            }
            menu->addAction(actionFor(id, o.value(QLatin1String("text")).toString()));
        }
    }
}

QMenu *MainWindow::contextMenu(const QString &specName, QWidget *parent)
{
    auto *m = new QMenu(parent);
    fillMenu(m, m_spec.contextMenu(specName));
    m_registry->updateActions();
    return m;
}

void MainWindow::buildMenus()
{
    for (const QJsonValue &v : m_spec.mainMenu()) {
        const QJsonObject o = v.toObject();
        QMenu *menu = menuBar()->addMenu(o.value(QLatin1String("text")).toString());
        fillMenu(menu, o.value(QLatin1String("items")).toArray());
        connect(menu, &QMenu::aboutToShow, m_registry, &CommandRegistry::updateActions);
    }
    addCustomCommands();
}

// Commands with IDs >= FirstCustomId are new features; put them where their
// menuPath says (creating sub-menus as needed).
void MainWindow::addCustomCommands()
{
    for (int id : m_registry->customIds()) {
        const Command *cmd = m_registry->find(id);
        if (!cmd || cmd->menuPath.isEmpty())
            continue;
        QMenu *menu = nullptr;
        const QStringList parts = cmd->menuPath.split(QLatin1Char('/'));
        QList<QAction *> level = menuBar()->actions();
        for (const QString &part : parts) {
            QMenu *found = nullptr;
            for (QAction *a : level)
                if (a->menu() && a->text().remove(QLatin1Char('&')) == QString(part).remove(QLatin1Char('&')))
                    found = a->menu();
            if (!found)
                found = menu ? menu->addMenu(part) : menuBar()->addMenu(part);
            menu = found;
            level = menu->actions();
        }
        QAction *a = actionFor(id, cmd->text);
        if (!cmd->shortcut.isEmpty())
            a->setShortcut(cmd->shortcut);
        menu->addAction(a);
    }
}

void MainWindow::buildToolbars()
{
    // Map toolbar resources to the Window-menu commands that toggle them.
    const QHash<int, int> toggles = {{128, Cmd::MainToolbar}, {137, Cmd::ViewControlBar}, {145, Cmd::EditToolbar},
                                     {237, Cmd::WinSelOperators}, {423, Cmd::SourceControlBar}};
    for (const UiSpec::Toolbar &tb : m_spec.toolbars()) {
        if (!toggles.contains(tb.resource))
            continue; // 189 belongs to the UV editor, not rebuilt yet
        QToolBar *bar = addToolBar(tb.name);
        bar->setObjectName(QStringLiteral("toolbar_%1").arg(tb.resource));
        bar->setIconSize(QSize(16, 16));
        for (int id : tb.ids) {
            if (id == 0)
                bar->addSeparator();
            else
                bar->addAction(actionFor(id));
        }
        if (tb.resource == 423)
            bar->hide();
        const int toggle = toggles.value(tb.resource);
        m_registry->add({toggle, [bar] { bar->setVisible(!bar->isVisible()); }, {}, [bar] { return bar->isVisible(); }});
        if (QAction *a = m_actions.value(toggle))
            m_registry->bind(toggle, a);
    }
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList files = QSettings().value(QStringLiteral("RecentFiles")).toStringList();
    for (int i = 0; i < files.size(); ++i) {
        QAction *a = m_recentMenu->addAction(QStringLiteral("&%1 %2").arg(i + 1).arg(QDir::toNativeSeparators(files[i])));
        connect(a, &QAction::triggered, this, [this, f = files[i]] {
            if (maybeSave())
                openFile(f);
        });
    }
    m_recentMenu->setEnabled(!files.isEmpty());
}

void MainWindow::addRecentFile(const QString &path)
{
    QSettings s;
    QStringList files = s.value(QStringLiteral("RecentFiles")).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecent)
        files.removeLast();
    s.setValue(QStringLiteral("RecentFiles"), files);
    rebuildRecentMenu();
}

// ---------------------------------------------------------------- files

bool MainWindow::maybeSave()
{
    if (!m_doc->isModified())
        return true;
    const auto r = QMessageBox::warning(this, tr("Object Builder"), tr("The model has been modified.\nDo you want to save your changes?"),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return m_doc->filePath().isEmpty() ? saveAs() : saveFile(m_doc->filePath());
    return true;
}

bool MainWindow::openFile(const QString &path)
{
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_doc->open(path, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Open"), tr("Cannot open %1\n\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    addRecentFile(path);
    afterOpen(QFileInfo(path).fileName(), QFileInfo(path).absolutePath());
    return true;
}

bool MainWindow::openGameFile(const QString &vfsPath)
{
    QByteArray data;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = GameFileSystem::instance().read(vfsPath, data, &err) && m_doc->openData(data, vfsPath, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Open"), tr("Cannot open %1\n\n%2").arg(vfsPath, err));
        return false;
    }
    afterOpen(vfsPath, QString());
    return true;
}

void MainWindow::afterOpen(const QString &name, const QString &folder)
{
    TextureLibrary::instance().setModelFolder(folder);
    for (Viewport *v : m_viewports)
        v->zoomToDrawing();
    QString msg = tr("Loaded %1").arg(name);
    if (m_doc->filePath().isEmpty())
        msg += tr(" (converted to editable MLOD - use Save As)");
    const QStringList warn = m_doc->lastWarnings();
    if (!warn.isEmpty())
        msg += tr(" - %1 warning(s): %2").arg(warn.size()).arg(warn.first());
    showStatus(msg, 10000);
}

bool MainWindow::saveFile(const QString &path)
{
    QString err;
    if (!m_doc->save(path, &err)) {
        QMessageBox::critical(this, tr("Save"), tr("Cannot save %1\n\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    addRecentFile(path);
    showStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::saveAs()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("CConfig/\\Last SaveAs Folder")).toString();
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), m_doc->filePath().isEmpty() ? QDir(dir).filePath(QFileInfo(m_doc->suggestedName()).fileName()) : m_doc->filePath(),
                                                tr("Objektiv2 model (*.p3d)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".p3d");
    s.setValue(QStringLiteral("CConfig/\\Last SaveAs Folder"), QFileInfo(path).absolutePath());
    return saveFile(path);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    s.setValue(QStringLiteral("MainWindow/state"), saveState());
    s.setValue(QStringLiteral("View/grid"), m_view.grid);
    s.setValue(QStringLiteral("View/solid"), m_view.solid);
    e->accept();
}

// ---------------------------------------------------------------- status

void MainWindow::showStatus(const QString &text, int ms)
{
    statusBar()->showMessage(text, ms);
}

QString MainWindow::undoText(int id, const QString &fallback) const
{
    const QString t = m_spec.undoText(id);
    return t.isEmpty() ? fallback : t;
}

void MainWindow::updateTitle()
{
    const QString name = m_doc->filePath().isEmpty() ? tr("Untitled") : QFileInfo(m_doc->filePath()).fileName();
    setWindowTitle(QStringLiteral("%1%2 - Object Builder").arg(name, m_doc->isModified() ? QStringLiteral("*") : QString()));
}

void MainWindow::updateStatus()
{
    const ObjectData &o = m_doc->active();
    m_statusCounts->setText(tr("LOD %1 | Points %2/%3 | Faces %4/%5")
                                .arg(LodNames::name(m_doc->model().activeResolution()))
                                .arg(o.selection.pointCount())
                                .arg(o.points.size())
                                .arg(o.selection.faceCount())
                                .arg(o.faces.size()));
}
