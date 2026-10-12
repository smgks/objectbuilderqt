#include "Panels.h"

#include "app/Document.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kAll = int(Document::Change::All);

void attachMenu(QWidget *w, MenuFactory factory, const QString &spec)
{
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(w, &QWidget::customContextMenuRequested, w, [w, factory, spec](const QPoint &pos) {
        if (QMenu *m = factory(spec, w)) {
            m->exec(w->mapToGlobal(pos));
            m->deleteLater();
        }
    });
}
} // namespace

// ---------------------------------------------------------------- LODs

LodPanel::LodPanel(Document *doc, MenuFactory menuFactory, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("LOD"), tr("Points"), tr("Faces")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_tree);
    attachMenu(m_tree, menuFactory, QStringLiteral("lodList"));
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (it)
            m_doc->setActiveLod(m_tree->indexOfTopLevelItem(it));
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &LodPanel::propertiesRequested);
    connect(doc, &Document::changed, this, [this](int c) {
        if (c & (int(Document::Change::Lods) | int(Document::Change::Geometry)))
            refresh();
    });
    refresh();
}

void LodPanel::refresh()
{
    const QSignalBlocker block(m_tree);
    m_tree->clear();
    const LodObject &m = m_doc->model();
    for (int i = 0; i < m.lods.size(); ++i) {
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, LodNames::name(m.lods[i].resolution));
        it->setText(1, QString::number(m.lods[i].data.points.size()));
        it->setText(2, QString::number(m.lods[i].data.faces.size()));
        it->setTextAlignment(1, Qt::AlignRight);
        it->setTextAlignment(2, Qt::AlignRight);
    }
    if (QTreeWidgetItem *cur = m_tree->topLevelItem(m.active)) {
        m_tree->setCurrentItem(cur);
        QFont f = cur->font(0);
        f.setBold(true);
        cur->setFont(0, f);
    }
}

// ---------------------------------------------------------------- named selections

NamedSelectionsPanel::NamedSelectionsPanel(Document *doc, MenuFactory menuFactory, QWidget *parent)
    : QWidget(parent), m_doc(doc)
{
    m_list = new QListWidget(this);
    m_list->setSortingEnabled(true);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_list);
    attachMenu(m_list, menuFactory, QStringLiteral("namedSelections"));
    // Click selects using the named selection (undo text 10004); Shift adds, Ctrl removes.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString name = it->text();
        const auto mods = QApplication::keyboardModifiers();
        m_doc->editActive(tr("Select using \"%1\"").arg(name), [&](ObjectData &o) {
            const NamedSelection *ns = o.findSelection(name);
            if (!ns)
                return;
            if (!(mods & (Qt::ShiftModifier | Qt::ControlModifier)))
                o.selection.clear();
            const bool sub = mods & Qt::ControlModifier;
            for (int i = 0; i < o.points.size(); ++i)
                if (ns->sel.point(i) && o.pointVisible(i))
                    o.selection.setPoint(i, sub ? 0 : ns->sel.weight(i));
            for (int f = 0; f < o.faces.size(); ++f)
                if (ns->sel.face(f) && o.faceVisible(f))
                    o.selection.setFace(f, !sub);
        }, int(Document::Change::Selection));
    });
    connect(doc, &Document::changed, this, [this](int c) {
        if (c & (int(Document::Change::Named) | int(Document::Change::Lods)))
            refresh();
    });
    refresh();
}

void NamedSelectionsPanel::refresh()
{
    const QString cur = currentName();
    const QSignalBlocker block(m_list);
    m_list->clear();
    for (const auto &ns : m_doc->active().namedSelections)
        m_list->addItem(ns.name);
    const auto found = m_list->findItems(cur, Qt::MatchExactly);
    if (!found.isEmpty())
        m_list->setCurrentItem(found.first());
}

QString NamedSelectionsPanel::currentName() const
{
    return m_list->currentItem() ? m_list->currentItem()->text() : QString();
}

// ---------------------------------------------------------------- named properties

NamedPropertiesPanel::NamedPropertiesPanel(Document *doc, MenuFactory menuFactory, QWidget *parent)
    : QWidget(parent), m_doc(doc)
{
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Value")});
    m_tree->setRootIsDecorated(false);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_tree);
    attachMenu(m_tree, menuFactory, QStringLiteral("namedProperties"));
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &NamedPropertiesPanel::editRequested);
    connect(doc, &Document::changed, this, [this](int c) {
        if (c & (int(Document::Change::Named) | int(Document::Change::Lods)))
            refresh();
    });
    refresh();
}

void NamedPropertiesPanel::refresh()
{
    const QString cur = currentName();
    const QSignalBlocker block(m_tree);
    m_tree->clear();
    for (const auto &p : m_doc->active().namedProperties) {
        auto *it = new QTreeWidgetItem(m_tree, {p.name, p.value});
        if (p.name == cur)
            m_tree->setCurrentItem(it);
    }
}

QString NamedPropertiesPanel::currentName() const
{
    return m_tree->currentItem() ? m_tree->currentItem()->text(0) : QString();
}

// ---------------------------------------------------------------- mass

MassPanel::MassPanel(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    m_total = new QLabel(this);
    m_selected = new QLabel(this);
    m_value = new QDoubleSpinBox(this);
    m_value->setRange(0, 1e9);
    m_value->setDecimals(3);
    auto *set = new QPushButton(tr("Set Total"), this);
    set->setToolTip(tr("Distribute this mass evenly over the selected points"));
    auto *perPoint = new QPushButton(tr("Set Each"), this);
    perPoint->setToolTip(tr("Give every selected point this mass"));
    auto *form = new QFormLayout;
    form->addRow(tr("Total:"), m_total);
    form->addRow(tr("Selected:"), m_selected);
    auto *row = new QHBoxLayout;
    row->addWidget(m_value, 1);
    row->addWidget(set);
    row->addWidget(perPoint);
    auto *l = new QVBoxLayout(this);
    l->addLayout(form);
    l->addLayout(row);
    l->addStretch(1);

    auto apply = [this](bool total) {
        const float v = float(m_value->value());
        m_doc->editActive(tr("Set Mass"), [&](ObjectData &o) {
            const int n = o.selection.pointCount();
            if (n == 0)
                return;
            if (o.mass.isEmpty())
                o.mass = QVector<float>(o.points.size(), 0.0f);
            for (int i = 0; i < o.points.size(); ++i)
                if (o.selection.point(i))
                    o.mass[i] = total ? v / n : v;
        }, int(Document::Change::Named));
    };
    connect(set, &QPushButton::clicked, this, [apply] { apply(true); });
    connect(perPoint, &QPushButton::clicked, this, [apply] { apply(false); });
    connect(doc, &Document::changed, this, &MassPanel::refresh);
    refresh();
}

void MassPanel::refresh()
{
    const ObjectData &o = m_doc->active();
    float sel = 0;
    for (int i = 0; i < o.mass.size(); ++i)
        if (o.selection.point(i))
            sel += o.mass[i];
    m_total->setText(o.hasMass() ? QString::number(o.totalMass(), 'f', 3) : tr("(no mass)"));
    m_selected->setText(QString::number(sel, 'f', 3));
}

// ---------------------------------------------------------------- position

PositionPanel::PositionPanel(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    m_cursor = new QLabel(this);
    m_selection = new QLabel(this);
    m_selection->setWordWrap(true);
    auto *form = new QFormLayout(this);
    form->addRow(tr("Cursor:"), m_cursor);
    auto *pinRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        m_pin[i] = new QDoubleSpinBox(this);
        m_pin[i]->setRange(-1e6, 1e6);
        m_pin[i]->setDecimals(3);
        m_pin[i]->setSingleStep(0.1);
        pinRow->addWidget(m_pin[i]);
        connect(m_pin[i], &QDoubleSpinBox::editingFinished, this, [this] {
            m_doc->setPin(QVector3D(float(m_pin[0]->value()), float(m_pin[1]->value()), float(m_pin[2]->value())));
        });
    }
    form->addRow(tr("Pin:"), pinRow);
    form->addRow(tr("Selection:"), m_selection);
    connect(doc, &Document::pinChanged, this, &PositionPanel::refresh);
    connect(doc, &Document::changed, this, &PositionPanel::refresh);
    refresh();
}

void PositionPanel::setCursor3D(const QVector3D &p)
{
    m_cursor->setText(QStringLiteral("%1, %2, %3").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3).arg(p.z(), 0, 'f', 3));
}

void PositionPanel::refresh()
{
    const QVector3D p = m_doc->pin();
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker b(m_pin[i]);
        m_pin[i]->setValue(p[i]);
    }
    const ObjectData &o = m_doc->active();
    QVector3D mn, mx;
    o.bounds(mn, mx, true);
    const int n = o.selection.pointCount();
    m_selection->setText(n == 0 ? tr("none")
                                : tr("%1 points, %2 faces\nsize %3 x %4 x %5")
                                      .arg(n)
                                      .arg(o.selection.faceCount())
                                      .arg(mx.x() - mn.x(), 0, 'f', 3)
                                      .arg(mx.y() - mn.y(), 0, 'f', 3)
                                      .arg(mx.z() - mn.z(), 0, 'f', 3));
}
