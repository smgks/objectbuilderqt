// Create, Structure, Points, Faces and Surfaces menus, and the LOD panel.
#include "app/Commands.h"

#include "core/Flags.h"
#include "core/MeshOps.h"
#include "core/Primitives.h"
#include "ui/Dialogs.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>

namespace {

constexpr int kGeo = int(Document::Change::Geometry);

void createPrimitive(AppContext &c, PrimitiveDialog::Kind kind)
{
    PrimitiveDialog dlg(kind, c.win);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Primitives::Params p = dlg.params();
    const QMatrix4x4 frame = c.win->creationFrame();
    c.doc->editActive(c.undo(10022, QObject::tr("Create Primitive")), [&](ObjectData &o) {
        switch (kind) {
        case PrimitiveDialog::Kind::Plane: Primitives::plane(o, p, frame); break;
        case PrimitiveDialog::Kind::Box: Primitives::box(o, p, frame); break;
        case PrimitiveDialog::Kind::Circle: Primitives::circle(o, p, frame); break;
        case PrimitiveDialog::Kind::Cylinder: Primitives::cylinder(o, p, frame); break;
        case PrimitiveDialog::Kind::Sphere: Primitives::sphere(o, p, frame); break;
        }
    });
}

void transform(AppContext &c, TransformDialog::Kind kind)
{
    TransformDialog dlg(kind, c.win);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QVector3D v = dlg.value();
    const QVector3D center = dlg.relativeToPin() ? c.doc->pin() : c.doc->active().selectionCenter();
    QMatrix4x4 m;
    if (kind == TransformDialog::Kind::Move) {
        m.translate(v);
    } else {
        m.translate(center);
        if (kind == TransformDialog::Kind::Rotate) {
            m.rotate(v.x(), 1, 0, 0);
            m.rotate(v.y(), 0, 1, 0);
            m.rotate(v.z(), 0, 0, 1);
        } else {
            m.scale(v);
        }
        m.translate(-center);
    }
    const bool anims = dlg.allAnimations();
    c.doc->editActive(c.undo(10024, QObject::tr("Transform Points")), [&](ObjectData &o) { MeshOps::transformSelected(o, m, anims); });
}

// Proxy triangle exactly as CICreateProxy builds it (0x5fa110): pin,
// pin + (0,2,0), pin + (0,0,1); points flagged as hidden-special.
void createProxy(AppContext &c)
{
    bool ok = false;
    QString name = QInputDialog::getText(c.win, QObject::tr("Proxy Name"), QObject::tr("Enter name of proxy"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.startsWith(QLatin1String("proxy:"), Qt::CaseInsensitive))
        name = name.mid(6);
    const QVector3D pin = c.doc->pin();
    c.doc->editActive(QObject::tr("Create Proxy"), [&](ObjectData &o) {
        const int a = o.addPoint(pin, PointFlags::Hidden);
        const int b = o.addPoint(pin + QVector3D(0, 2, 0), PointFlags::Hidden);
        const int cc = o.addPoint(pin + QVector3D(0, 0, 1), PointFlags::Hidden);
        Face f;
        f.n = 3;
        f.v[0].point = a;
        f.v[1].point = b;
        f.v[2].point = cc;
        const int fi = o.addFace(f);
        // proxy:<name>.NNN, first free index
        QString selName;
        for (int i = 1; i < 1000; ++i) {
            selName = QStringLiteral("proxy:%1.%2").arg(name).arg(i, 3, 10, QLatin1Char('0'));
            if (!o.findSelection(selName))
                break;
        }
        NamedSelection ns{selName, Selection{}};
        ns.sel.resize(o.points.size(), o.faces.size());
        ns.sel.points[a] = ns.sel.points[b] = ns.sel.points[cc] = 255;
        ns.sel.faces[fi] = 1;
        o.namedSelections.append(ns);
        o.selection = ns.sel;
    });
}

} // namespace

void registerGeometryCommands(AppContext &c)
{
    CommandRegistry &r = *c.reg;
    MainWindow *w = c.win;
    Document *d = c.doc;
    auto selPts = [c] { return c.hasSelectedPoints(); };
    auto selFaces = [c] { return c.hasSelectedFaces(); };
    auto hasPts = [c] { return c.hasPoints(); };

    // --- Create -------------------------------------------------------------
    r.add({Cmd::CreatePoint, [=] {
               const QVector3D pin = d->pin();
               d->editActive(c.undo(10017, QObject::tr("New Point")), [&](ObjectData &o) {
                   o.selection.clear();
                   o.selection.setPoint(o.addPoint(pin), 255);
               });
           }});
    r.add({Cmd::CreatePlane, [c]() mutable { createPrimitive(c, PrimitiveDialog::Kind::Plane); }});
    r.add({Cmd::CreateBox, [c]() mutable { createPrimitive(c, PrimitiveDialog::Kind::Box); }});
    r.add({Cmd::CreateCircle, [c]() mutable { createPrimitive(c, PrimitiveDialog::Kind::Circle); }});
    r.add({Cmd::CreateCylinder, [c]() mutable { createPrimitive(c, PrimitiveDialog::Kind::Cylinder); }});
    r.add({Cmd::CreateSphere, [c]() mutable { createPrimitive(c, PrimitiveDialog::Kind::Sphere); }});
    r.add({Cmd::CreateFace, [=] {
               const QVector3D dir = w->viewDirection();
               bool ok = false;
               d->editActive(QObject::tr("New Face"), [&](ObjectData &o) { ok = MeshOps::createFaceFromSelection(o, dir); });
               if (!ok) {
                   d->undoStack()->undo();
                   w->showStatus(QObject::tr("Select 3 or 4 points to create a face"));
               }
           },
           [c] {
               const int n = c.doc->active().selection.pointCount();
               return n == 3 || n == 4;
           }});
    r.add({Cmd::CreateProxy, [c]() mutable { createProxy(c); }});
    r.add({Cmd::Extrude, [=] { d->editActive(QObject::tr("Extrude"), MeshOps::extrudeSelectedFaces); }, selFaces});

    // --- Structure ----------------------------------------------------------
    r.add({Cmd::CheckFaces, [=] {
               const QVector<int> bad = MeshOps::degeneratedFaces(d->active());
               d->editActive(c.undo(10030, QObject::tr("Check Faces")), [&](ObjectData &o) {
                   o.selection.clear();
                   for (int f : bad) {
                       o.selection.setFace(f, true);
                       for (int k = 0; k < o.faces[f].n; ++k)
                           o.selection.setPoint(o.faces[f].v[k].point, 255);
                   }
               }, int(Document::Change::Selection));
               w->showStatus(bad.isEmpty() ? QObject::tr("No bad faces found") : QObject::tr("%1 bad face(s) selected").arg(bad.size()));
           },
           hasPts});
    r.add({Cmd::CenterAll, [=] {
               d->editActive(QObject::tr("Center All"), [](ObjectData &o) {
                   QVector3D mn, mx;
                   o.bounds(mn, mx);
                   const QVector3D c0 = (mn + mx) * 0.5f;
                   for (auto &p : o.points)
                       p.pos -= c0;
                   for (auto &a : o.animations)
                       for (auto &p : a.points)
                           p -= c0;
               });
           },
           hasPts});
    r.add({Cmd::FindComponents, [=] {
               int n = 0;
               d->editActive(c.undo(10034, QObject::tr("Find Components")), [&](ObjectData &o) { n = MeshOps::findComponents(o); },
                             int(Document::Change::Named));
               w->showStatus(QObject::tr("%1 component(s) found").arg(n));
           },
           hasPts});
    r.add({Cmd::FindNonClosed, [=] {
               int n = 0;
               d->editActive(c.undo(10039, QObject::tr("Find non-closed")), [&](ObjectData &o) {
                   n = MeshOps::selectNonClosed(o);
                   o.selectFacesFromPoints();
               }, int(Document::Change::Selection));
               w->showStatus(n ? QObject::tr("%1 open edge(s) found").arg(n) : QObject::tr("Topology is closed"));
           },
           hasPts});
    r.add({Cmd::TriangulateBack, [=] { d->editActive(c.undo(10032, QObject::tr("Triangulate")), [](ObjectData &o) { MeshOps::triangulateSelected(o, true); }); },
           selFaces});
    r.add({Cmd::TriangulateFwd, [=] { d->editActive(c.undo(10032, QObject::tr("Triangulate")), [](ObjectData &o) { MeshOps::triangulateSelected(o, false); }); },
           selFaces});
    r.add({Cmd::Squarize, [=] {
               int n = 0;
               d->editActive(c.undo(10033, QObject::tr("Squarize")), [&](ObjectData &o) { n = MeshOps::squarizeSelected(o); });
               w->showStatus(QObject::tr("%1 quad(s) created").arg(n));
           },
           selFaces});

    // --- Points -------------------------------------------------------------
    r.add({Cmd::SelectIsolated, [=] {
               d->editActive(c.undo(10031, QObject::tr("Find Isolated Points")), [](ObjectData &o) {
                   o.selection.clear();
                   for (int i : MeshOps::isolatedPoints(o))
                       o.selection.setPoint(i, 255);
               }, int(Document::Change::Selection));
           },
           hasPts});
    r.add({Cmd::TransformMove, [c]() mutable { transform(c, TransformDialog::Kind::Move); }, selPts});
    r.add({Cmd::TransformRotate, [c]() mutable { transform(c, TransformDialog::Kind::Rotate); }, selPts});
    r.add({Cmd::TransformScale, [c]() mutable { transform(c, TransformDialog::Kind::Scale); }, selPts});
    r.add({Cmd::MergePoints, [=] { d->editActive(c.undo(10047, QObject::tr("Merge Selected")), MeshOps::mergeSelectedPoints); },
           [c] { return c.doc->active().selection.pointCount() >= 2; }});
    r.add({Cmd::MergeNear, [=] {
               bool ok = false;
               const double tol = QInputDialog::getDouble(w, QObject::tr("Merge near..."), QObject::tr("Distance:"), 0.001, 0, 1000, 4, &ok);
               if (!ok)
                   return;
               int n = 0;
               d->editActive(QObject::tr("Merge near"), [&](ObjectData &o) { n = MeshOps::mergeNear(o, float(tol)); });
               w->showStatus(QObject::tr("%1 point(s) merged").arg(n));
           },
           selPts});
    r.add({Cmd::RemovePoints, [=] {
               d->editActive(QObject::tr("Remove"), [](ObjectData &o) {
                   QVector<bool> rm(o.points.size());
                   for (int i = 0; i < rm.size(); ++i)
                       rm[i] = o.selection.point(i);
                   o.deletePoints(rm);
               });
           },
           selPts});
    r.add({Cmd::PointProperties, [=] { PointPropertiesDialog(d, w).exec(); }, selPts});

    // --- Faces ----------------------------------------------------------------
    r.add({Cmd::FaceReverse, [=] { d->editActive(c.undo(10044, QObject::tr("Reverse")), MeshOps::reverseSelectedFaces); }, selFaces});
    r.add({Cmd::FaceUncross, [=] { d->editActive(c.undo(10045, QObject::tr("Uncross")), MeshOps::uncrossSelectedFaces); }, selFaces});
    r.add({Cmd::FaceRemove, [=] { d->editActive(c.undo(10046, QObject::tr("Remove Faces")), MeshOps::removeSelectedFaces); }, selFaces});
    CommandRegistry *reg = c.reg;
    r.add({Cmd::DegeneratedCheck, [reg] { reg->trigger(Cmd::CheckFaces); }, hasPts});
    r.add({Cmd::DegeneratedRepair, [=] {
               int n = 0;
               d->editActive(QObject::tr("Repair Degenerated Faces"), [&](ObjectData &o) { n = MeshOps::repairDegenerated(o); });
               w->showStatus(QObject::tr("%1 face(s) repaired").arg(n));
           },
           hasPts});
    r.add({Cmd::FacesMoveTop, [=] { d->editActive(QObject::tr("Move top"), [](ObjectData &o) { MeshOps::moveSelectedFaces(o, true); }); }, selFaces});
    r.add({Cmd::FacesMoveBottom, [=] { d->editActive(QObject::tr("Move bottom"), [](ObjectData &o) { MeshOps::moveSelectedFaces(o, false); }); }, selFaces});
    r.add({Cmd::RecalcNormals, [=] { d->editActive(c.undo(10028, QObject::tr("Recalculate Normals")), [](ObjectData &o) { o.recalculateNormals(); }); },
           hasPts});
    r.add({Cmd::FaceProperties, [=] { FacePropertiesDialog(d, w).exec(); }, selFaces});

    // --- Surfaces ---------------------------------------------------------------
    r.add({Cmd::SharpEdge, [=] { d->editActive(QObject::tr("Sharp Edges"), [](ObjectData &o) { MeshOps::setSharpEdges(o, true); }); }, selPts});
    r.add({Cmd::SmoothEdge, [=] { d->editActive(c.undo(10048, QObject::tr("Smooth Edges")), [](ObjectData &o) { MeshOps::setSharpEdges(o, false); }); },
           selPts});
    r.add({Cmd::UVSetAdd, [=] {
               d->editActive(QObject::tr("Add UV Set"), [](ObjectData &o) {
                   quint32 id = 1;
                   while (o.findUVSet(id))
                       ++id;
                   UVSet s = o.uvSets[o.activeUVSet];
                   s.id = id;
                   o.uvSets.append(s);
                   o.activeUVSet = o.uvSets.size() - 1;
               });
           }});
    r.add({Cmd::UVSetDelete, [=] {
               d->editActive(QObject::tr("Delete UV Set"), [](ObjectData &o) {
                   o.uvSets.removeAt(o.activeUVSet);
                   o.activeUVSet = 0;
                   o.normalizeArrays();
               });
           },
           [=] { return d->active().uvSets.size() > 1 && d->active().uvSets[d->active().activeUVSet].id != 0; }});
    r.add({Cmd::UVSetChangeId, [=] {
               const ObjectData &cur = d->active();
               bool ok = false;
               const int id = QInputDialog::getInt(w, QObject::tr("Change ID"), QObject::tr("UV set ID:"), int(cur.uvSets[cur.activeUVSet].id), 0, 99, 1, &ok);
               if (!ok)
                   return;
               if (cur.findUVSet(quint32(id))) {
                   QMessageBox::warning(w, QObject::tr("Change ID"), QObject::tr("UV set %1 already exists.").arg(id));
                   return;
               }
               d->editActive(QObject::tr("Change UV Set ID"), [&](ObjectData &o) { o.uvSets[o.activeUVSet].id = quint32(id); });
           },
           [=] { return d->active().uvSets[d->active().activeUVSet].id != 0; }});
    for (int id = 0; id <= 100; ++id) {
        r.add({Cmd::UVSetActivateFirst + id, [=] {
                   d->editActive(QObject::tr("Activate UV Set %1").arg(id), [&](ObjectData &o) {
                       for (int i = 0; i < o.uvSets.size(); ++i)
                           if (o.uvSets[i].id == quint32(id))
                               o.activeUVSet = i;
                   }, 0);
               },
               [=] { return d->active().findUVSet(quint32(id)) != nullptr; },
               [=] { return d->active().uvSets[d->active().activeUVSet].id == quint32(id); }});
    }

    // --- LOD panel (context menu 150) -------------------------------------------
    r.add({Cmd::LodNew, [=] {
               ResolutionDialog dlg(d->model().lods.last().resolution + 1.0f, w);
               float res;
               if (dlg.exec() != QDialog::Accepted || !dlg.resolution(res))
                   return;
               if (d->model().find(res) >= 0) {
                   QMessageBox::warning(w, QObject::tr("New LOD"), QObject::tr("LOD %1 already exists.").arg(LodNames::name(res)));
                   return;
               }
               d->editAll(QObject::tr("Add LOD %1").arg(LodNames::name(res)), [&](LodObject &m) { m.active = m.addLod(res); });
           }});
    r.add({Cmd::LodDuplicate, [=] {
               ResolutionDialog dlg(d->model().activeResolution() + 1.0f, w);
               float res;
               if (dlg.exec() != QDialog::Accepted || !dlg.resolution(res))
                   return;
               if (d->model().find(res) >= 0) {
                   QMessageBox::warning(w, QObject::tr("Duplicate LOD"), QObject::tr("LOD %1 already exists.").arg(LodNames::name(res)));
                   return;
               }
               d->editAll(QObject::tr("Duplicate LOD"), [&](LodObject &m) {
                   const ObjectData copy = m.activeData();
                   m.active = m.addLod(res, copy);
               });
           }});
    r.add({Cmd::LodDelete, [=] {
               const QString name = LodNames::name(d->model().activeResolution());
               if (QMessageBox::question(w, QObject::tr("Delete LOD"), QObject::tr("Delete LOD %1?").arg(name)) != QMessageBox::Yes)
                   return;
               d->editAll(QObject::tr("Remove LOD %1").arg(name), [](LodObject &m) { m.removeLod(m.active); });
           },
           [=] { return d->model().lods.size() > 1; }});
    r.add({Cmd::LodProperties, [=] {
               const float old = d->model().activeResolution();
               ResolutionDialog dlg(old, w);
               float res;
               if (dlg.exec() != QDialog::Accepted || !dlg.resolution(res) || res == old)
                   return;
               if (d->model().find(res) >= 0) {
                   QMessageBox::warning(w, QObject::tr("LOD Properties"), QObject::tr("LOD %1 already exists.").arg(LodNames::name(res)));
                   return;
               }
               d->editAll(QObject::tr("Change LOD from %1 to %2").arg(LodNames::name(old), LodNames::name(res)),
                          [&](LodObject &m) { m.setResolution(m.active, res); });
           }});
}
