// Edit menu, View > Lock/Hide, and the named selection / named property panels.
#include "app/Commands.h"

#include "core/MeshOps.h"
#include "core/P3dIO.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>

namespace {

const QString kClipboardMime = QStringLiteral("application/x-objektiv2-lod");
constexpr int kSel = int(Document::Change::Selection);
constexpr int kNamed = int(Document::Change::Named);

ObjectData extractSelection(const ObjectData &o)
{
    ObjectData part = o;
    QVector<bool> dropFace(part.faces.size());
    for (int f = 0; f < part.faces.size(); ++f)
        dropFace[f] = !part.selection.face(f);
    part.deleteFaces(dropFace);
    QVector<bool> used(part.points.size(), false);
    for (const auto &f : part.faces)
        for (int k = 0; k < f.n; ++k)
            used[f.v[k].point] = true;
    QVector<bool> dropPoint(part.points.size());
    for (int i = 0; i < part.points.size(); ++i)
        dropPoint[i] = !used[i] && !part.selection.point(i);
    part.deletePoints(dropPoint);
    return part;
}

void copyToClipboard(const ObjectData &o)
{
    auto *mime = new QMimeData;
    mime->setData(kClipboardMime, P3dIO::serializeLod(extractSelection(o)));
    QApplication::clipboard()->setMimeData(mime);
}

// "EditProperty" (dialog 132)
bool editProperty(QWidget *parent, QString &name, QString &value)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Edit Named Property"));
    auto *n = new QLineEdit(name, &dlg);
    auto *v = new QLineEdit(value, &dlg);
    n->setMaxLength(63);
    v->setMaxLength(63);
    auto *form = new QFormLayout(&dlg);
    form->addRow(QObject::tr("&Name:"), n);
    form->addRow(QObject::tr("&Value:"), v);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    dlg.resize(380, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted || n->text().trimmed().isEmpty())
        return false;
    name = n->text().trimmed();
    value = v->text();
    return true;
}

// Combine current and stored selections (Edit > Selection operators).
enum class SelOp { Union, Complement, Intersection, ComplementOfIntersection };

void combine(Selection &cur, const Selection &stored, SelOp op)
{
    auto f = [op](bool a, bool b) {
        switch (op) {
        case SelOp::Union: return a || b;
        case SelOp::Complement: return a && !b;
        case SelOp::Intersection: return a && b;
        case SelOp::ComplementOfIntersection: return a != b;
        }
        return a;
    };
    for (int i = 0; i < cur.points.size(); ++i)
        cur.points[i] = f(cur.point(i), stored.point(i)) ? std::max(cur.weight(i), stored.weight(i)) : 0;
    for (int i = 0; i < cur.faces.size(); ++i)
        cur.faces[i] = f(cur.face(i), stored.face(i)) ? 1 : 0;
}

} // namespace

void registerEditCommands(AppContext &c)
{
    CommandRegistry &r = *c.reg;
    MainWindow *w = c.win;
    Document *d = c.doc;
    ViewSettings *view = c.view;
    auto hasSel = [c] { return c.hasSelectedPoints() || c.hasSelectedFaces(); };
    auto hasPts = [c] { return c.hasPoints(); };

    // --- undo / clipboard ------------------------------------------------
    r.add({Cmd::Undo, [=] { d->undoStack()->undo(); }, [=] { return d->undoStack()->canUndo(); }});
    r.add({Cmd::Redo, [=] { d->undoStack()->redo(); }, [=] { return d->undoStack()->canRedo(); }});
    r.add({Cmd::Copy, [=] { copyToClipboard(d->active()); }, hasSel});
    r.add({Cmd::Cut, [=] {
               copyToClipboard(d->active());
               d->editActive(c.undo(10043, QObject::tr("Cut")), MeshOps::deleteSelection);
           },
           hasSel});
    r.add({Cmd::Paste, [=] {
               const QMimeData *mime = QApplication::clipboard()->mimeData();
               if (!mime || !mime->hasFormat(kClipboardMime))
                   return;
               ObjectData part;
               QString err;
               if (!P3dIO::deserializeLod(mime->data(kClipboardMime), part, &err)) {
                   QMessageBox::warning(w, QObject::tr("Paste"), err);
                   return;
               }
               d->editActive(QObject::tr("Paste"), [&](ObjectData &o) { o.merge(part); });
           },
           [] {
               const QMimeData *m = QApplication::clipboard()->mimeData();
               return m && m->hasFormat(kClipboardMime);
           }});
    r.add({Cmd::Delete, [=] { d->editActive(c.undo(10042, QObject::tr("Delete")), MeshOps::deleteSelection); }, hasSel});

    // --- selection -------------------------------------------------------
    r.add({Cmd::SelectAll, [=] {
               d->editActive(c.undo(10009, QObject::tr("Select All")), [](ObjectData &o) {
                   for (int i = 0; i < o.points.size(); ++i)
                       o.selection.setPoint(i, o.pointVisible(i) && !o.locked.point(i) ? 255 : 0);
                   for (int f = 0; f < o.faces.size(); ++f)
                       o.selection.setFace(f, o.faceVisible(f) && !o.locked.face(f));
               }, kSel);
           },
           hasPts});
    r.add({Cmd::Deselect, [=] { d->editActive(c.undo(10010, QObject::tr("Deselect")), [](ObjectData &o) { o.selection.clear(); }, kSel); }, hasSel});
    r.add({Cmd::DeselectFaces, [=] { d->editActive(QObject::tr("Deselect Faces"), [](ObjectData &o) { o.selection.faces.fill(0); }, kSel); },
           [c] { return c.hasSelectedFaces(); }});
    r.add({Cmd::DeselectVertices, [=] { d->editActive(QObject::tr("Deselect Vertices"), [](ObjectData &o) { o.selection.points.fill(0); }, kSel); },
           [c] { return c.hasSelectedPoints(); }});
    r.add({Cmd::InvertSelection, [=] { d->editActive(c.undo(10011, QObject::tr("Invert Selection")), MeshOps::invertSelection, kSel); }, hasPts});
    r.add({Cmd::SelectFacesFromPoints, [=] { d->editActive(QObject::tr("Select faces from points"), [](ObjectData &o) { o.selectFacesFromPoints(); }, kSel); },
           [c] { return c.hasSelectedPoints(); }});
    r.add({Cmd::SelectPointsFromFaces, [=] { d->editActive(QObject::tr("Select points from faces"), [](ObjectData &o) { o.selectPointsFromFaces(); }, kSel); },
           [c] { return c.hasSelectedFaces(); }});
    r.add({Cmd::GrowSelection, [=] { d->editActive(QObject::tr("Grow selected"), MeshOps::growSelection, kSel); }, hasSel});
    r.add({Cmd::ShrinkSelection, [=] { d->editActive(QObject::tr("Shrink selected"), MeshOps::shrinkSelection, kSel); }, hasSel});
    r.add({Cmd::SelectLocked, [=] { d->editActive(QObject::tr("Select Locked"), [](ObjectData &o) { o.selection = o.locked; }, kSel); }});
    r.add({Cmd::SelectHidden, [=] { d->editActive(QObject::tr("Select Hidden"), [](ObjectData &o) { o.selection = o.hidden; }, kSel); }});
    r.add({Cmd::UnselectTriangles, [=] {
               d->editActive(QObject::tr("Unselect triangles"), [](ObjectData &o) {
                   for (int f = 0; f < o.faces.size(); ++f)
                       if (o.faces[f].n == 3)
                           o.selection.setFace(f, false);
               }, kSel);
           },
           [c] { return c.hasSelectedFaces(); }});

    // --- selection operators (stored selection) ----------------------------
    r.add({Cmd::SelOpCopy, [=] {
               d->storedSelection = d->active().selection;
               w->showStatus(QObject::tr("Selection stored"));
           },
           hasSel});
    auto op = [=](SelOp o, const QString &name) {
        return [=] { d->editActive(name, [=](ObjectData &obj) { combine(obj.selection, d->storedSelection, o); }, kSel); };
    };
    r.add({Cmd::SelOpUnion, op(SelOp::Union, QObject::tr("Selection Union"))});
    r.add({Cmd::SelOpComplement, op(SelOp::Complement, QObject::tr("Selection Complement"))});
    r.add({Cmd::SelOpIntersection, op(SelOp::Intersection, QObject::tr("Selection Intersection"))});
    r.add({Cmd::SelOpComplementOfIntersection, op(SelOp::ComplementOfIntersection, QObject::tr("Selection Complement of Intersection"))});
    r.add({Cmd::SelOpRecall, [=] {
               d->editActive(QObject::tr("Recall Selection"), [=](ObjectData &o) {
                   o.selection = d->storedSelection;
                   o.selection.resize(o.points.size(), o.faces.size());
               }, kSel);
           },
           [=] { return !d->storedSelection.isEmpty(); }});

    // --- pin & edit modes --------------------------------------------------
    r.add({Cmd::CenterPin, [=] { d->setPin(d->active().selectionCenter()); }, [c] { return c.hasSelectedPoints(); }});
    auto mode = [=](EditMode m) {
        return Command{0, [=] {
                           view->mode = m;
                           w->refreshViews();
                       },
                       {}, [=] { return view->mode == m; }};
    };
    for (auto [id, m] : {std::pair{Cmd::ModeVertices, EditMode::Vertices}, std::pair{Cmd::ModeFaces, EditMode::Faces},
                         std::pair{Cmd::ModeObject, EditMode::Object}, std::pair{Cmd::ModePin, EditMode::Pin}}) {
        Command cmd = mode(m);
        cmd.id = id;
        r.add(cmd);
    }

    // --- View > Lock/Hide ------------------------------------------------
    r.add({Cmd::HideSelection, [=] {
               d->editActive(c.undo(10005, QObject::tr("Hide Selection")), [](ObjectData &o) {
                   for (int i = 0; i < o.points.size(); ++i)
                       if (o.selection.point(i))
                           o.hidden.setPoint(i, 255);
                   for (int f = 0; f < o.faces.size(); ++f)
                       if (o.selection.face(f))
                           o.hidden.setFace(f, true);
                   o.selection.clear();
               }, kSel);
           },
           hasSel});
    r.add({Cmd::Unhide, [=] { d->editActive(c.undo(10007, QObject::tr("Unhide Selection")), [](ObjectData &o) { o.hidden.clear(); }, kSel); },
           [=] { return !d->active().hidden.isEmpty(); }});
    r.add({Cmd::LockSelection, [=] {
               d->editActive(c.undo(10006, QObject::tr("Lock Selection")), [](ObjectData &o) {
                   for (int i = 0; i < o.points.size(); ++i)
                       if (o.selection.point(i))
                           o.locked.setPoint(i, 255);
                   for (int f = 0; f < o.faces.size(); ++f)
                       if (o.selection.face(f))
                           o.locked.setFace(f, true);
                   o.selection.clear();
               }, kSel);
           },
           hasSel});
    r.add({Cmd::Unlock, [=] { d->editActive(c.undo(10008, QObject::tr("Unlock Selection")), [](ObjectData &o) { o.locked.clear(); }, kSel); },
           [=] { return !d->active().locked.isEmpty(); }});

    // --- named selections panel (context menu 153) -------------------------
    auto curSel = [w] { return w->currentNamedSelection(); };
    auto hasCurSel = [w] { return !w->currentNamedSelection().isEmpty(); };
    r.add({Cmd::NselNew, [=] {
               bool ok = false;
               const QString name = QInputDialog::getText(w, QObject::tr("New Selection"), QObject::tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
               if (!ok || name.isEmpty())
                   return;
               d->editActive(c.undo(10014, QObject::tr("Save selection")), [&](ObjectData &o) {
                   NamedSelection *ns = o.findSelection(name);
                   if (!ns) {
                       o.namedSelections.append(NamedSelection{name, Selection{}});
                       ns = &o.namedSelections.last();
                   }
                   ns->sel = o.selection;
               }, kNamed);
           }});
    r.add({Cmd::NselRedefine, [=] {
               const QString name = curSel();
               d->editActive(c.undo(10016, QObject::tr("Redefine Named Selection \"%s\"")).replace(QLatin1String("%s"), name), [&](ObjectData &o) {
                   if (NamedSelection *ns = o.findSelection(name))
                       ns->sel = o.selection;
               }, kNamed);
           },
           hasCurSel});
    auto removeNamed = [](ObjectData &o, const QString &name) {
        o.namedSelections.erase(std::remove_if(o.namedSelections.begin(), o.namedSelections.end(),
                                               [&](const NamedSelection &ns) { return ns.name.compare(name, Qt::CaseInsensitive) == 0; }),
                                o.namedSelections.end());
    };
    r.add({Cmd::NselDelete, [=] {
               const QString name = curSel();
               d->editActive(c.undo(10015, QObject::tr("Delete Named Selection \"%s\"")).replace(QLatin1String("%s"), name),
                             [&](ObjectData &o) { removeNamed(o, name); }, kNamed);
           },
           hasCurSel});
    r.add({Cmd::NselDeleteAll, [=] {
               const QString name = curSel();
               d->editAll(QObject::tr("Delete Named Selection \"%1\" (All LODs)").arg(name), [&](LodObject &m) {
                   for (Lod &l : m.lods)
                       removeNamed(l.data, name);
               }, kNamed);
           },
           hasCurSel});
    auto rename = [=](bool all) {
        const QString old = curSel();
        bool ok = false;
        const QString name = QInputDialog::getText(w, QObject::tr("Rename"), QObject::tr("New name:"), QLineEdit::Normal, old, &ok).trimmed();
        if (!ok || name.isEmpty() || name == old)
            return;
        auto doRename = [&](ObjectData &o) {
            if (NamedSelection *ns = o.findSelection(old))
                ns->name = name;
        };
        const QString text = c.undo(10013, QObject::tr("Rename selection \"%s\"")).replace(QLatin1String("%s"), old);
        if (all)
            d->editAll(text, [&](LodObject &m) {
                for (Lod &l : m.lods)
                    doRename(l.data);
            }, kNamed);
        else
            d->editActive(text, doRename, kNamed);
    };
    r.add({Cmd::NselRename, [=] { rename(false); }, hasCurSel});
    r.add({Cmd::NselRenameAll, [=] { rename(true); }, hasCurSel});
    r.add({Cmd::NselDeleteEmpty, [=] {
               d->editActive(QObject::tr("Delete Empty"), [](ObjectData &o) {
                   o.namedSelections.erase(std::remove_if(o.namedSelections.begin(), o.namedSelections.end(),
                                                          [](const NamedSelection &ns) { return ns.sel.isEmpty(); }),
                                           o.namedSelections.end());
               }, kNamed);
           }});

    // --- named properties panel (context menu 133) -------------------------
    r.add({Cmd::NpropNew, [=] {
               QString name, value;
               if (!editProperty(w, name, value))
                   return;
               d->editActive(QObject::tr("New Named Property"), [&](ObjectData &o) { o.setProperty(name, value); }, kNamed);
           }});
    r.add({Cmd::NpropEdit, [=] {
               const QString old = w->currentNamedProperty();
               QString name = old, value = d->active().property(old);
               if (!editProperty(w, name, value))
                   return;
               d->editActive(QObject::tr("Edit Named Property"), [&](ObjectData &o) {
                   if (name.compare(old, Qt::CaseInsensitive) != 0)
                       o.removeProperty(old);
                   o.setProperty(name, value);
               }, kNamed);
           },
           [w] { return !w->currentNamedProperty().isEmpty(); }});
    r.add({Cmd::NpropDelete, [=] {
               const QString name = w->currentNamedProperty();
               d->editActive(QObject::tr("Delete Named Property"), [&](ObjectData &o) { o.removeProperty(name); }, kNamed);
           },
           [w] { return !w->currentNamedProperty().isEmpty(); }});
}
