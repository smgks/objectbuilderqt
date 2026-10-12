#pragma once

#include <QVector3D>
#include <QWidget>

#include <functional>

class Document;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QTreeWidget;
class QDoubleSpinBox;

// Dock panels of the main window. Each is a thin view over Document and
// refreshes on Document::changed. Context menus come from the original menu
// resources and are supplied by MainWindow through `menuFactory`.
using MenuFactory = std::function<QMenu *(const QString &specName, QWidget *parent)>;

class LodPanel : public QWidget {
    Q_OBJECT
public:
    LodPanel(Document *doc, MenuFactory menuFactory, QWidget *parent = nullptr);
    void refresh();
signals:
    void propertiesRequested();

private:
    Document *m_doc;
    QTreeWidget *m_tree;
};

class NamedSelectionsPanel : public QWidget {
    Q_OBJECT
public:
    NamedSelectionsPanel(Document *doc, MenuFactory menuFactory, QWidget *parent = nullptr);
    void refresh();
    QString currentName() const;

private:
    Document *m_doc;
    QListWidget *m_list;
};

class NamedPropertiesPanel : public QWidget {
    Q_OBJECT
public:
    NamedPropertiesPanel(Document *doc, MenuFactory menuFactory, QWidget *parent = nullptr);
    void refresh();
    QString currentName() const;
signals:
    void editRequested();

private:
    Document *m_doc;
    QTreeWidget *m_tree;
};

class MassPanel : public QWidget {
    Q_OBJECT
public:
    explicit MassPanel(Document *doc, QWidget *parent = nullptr);
    void refresh();

private:
    Document *m_doc;
    QLabel *m_total, *m_selected;
    QDoubleSpinBox *m_value;
};

class PositionPanel : public QWidget {
    Q_OBJECT
public:
    explicit PositionPanel(Document *doc, QWidget *parent = nullptr);
    void setCursor3D(const QVector3D &p);
    void refresh();

private:
    Document *m_doc;
    QLabel *m_cursor, *m_selection;
    QDoubleSpinBox *m_pin[3];
};
