// File menu.
#include "app/Commands.h"

#include "core/ObjIO.h"
#include "core/P3dIO.h"
#include "ui/Dialogs.h"
#include "app/GameData.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace {

const QString kP3dFilter = QObject::tr("Objektiv2 model (*.p3d);;All files (*.*)");

QString lastDir(const QString &key)
{
    return QSettings().value(QStringLiteral("CConfig/") + key).toString();
}

void merge(AppContext &c, bool asSelection)
{
    const QString path = QFileDialog::getOpenFileName(c.win, QObject::tr("Merge"), lastDir(QStringLiteral("\\Last Merge Folder")), kP3dFilter);
    if (path.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("CConfig/\\Last Merge Folder"), QFileInfo(path).absolutePath());
    LodObject other;
    QString err;
    if (!P3dIO::load(path, other, &err)) {
        QMessageBox::critical(c.win, QObject::tr("Merge"), err);
        return;
    }
    const QString selName = asSelection ? QFileInfo(path).completeBaseName() : QString();
    // Each LOD of the merged file goes into the LOD with the same resolution.
    c.doc->editAll(QObject::tr("Merge %1").arg(QFileInfo(path).fileName()), [&](LodObject &m) {
        for (const Lod &l : other.lods) {
            const int idx = m.addLod(l.resolution);
            m.lods[idx].data.merge(l.data, selName);
        }
    });
}

} // namespace

void registerFileCommands(AppContext &c)
{
    CommandRegistry &r = *c.reg;
    MainWindow *w = c.win;
    Document *d = c.doc;

    r.add({Cmd::FileNew, [=] {
               if (w->maybeSave())
                   d->newModel();
           }});
    r.add({Cmd::FileOpen, [=] {
               if (!w->maybeSave())
                   return;
               QSettings s;
               const QString path = QFileDialog::getOpenFileName(w, QObject::tr("Open"), s.value(QStringLiteral("LastOpenFolder")).toString(), kP3dFilter);
               if (path.isEmpty())
                   return;
               s.setValue(QStringLiteral("LastOpenFolder"), QFileInfo(path).absolutePath());
               w->openFile(path);
           }});
    r.add({Cmd::FileSave, [=] {
               if (d->filePath().isEmpty())
                   w->saveAs();
               else
                   w->saveFile(d->filePath());
           }});
    r.add({Cmd::FileSaveAs, [=] { w->saveAs(); }});
    r.add({Cmd::FileMerge, [c]() mutable { merge(c, false); }});
    r.add({Cmd::FileMergeAsSelection, [c]() mutable { merge(c, true); }});

    r.add({Cmd::ImportObj, [=] {
               const QString path = QFileDialog::getOpenFileName(w, QObject::tr("Import OBJ"), QString(), QObject::tr("Object (*.obj);;All files (*.*)"));
               if (path.isEmpty())
                   return;
               QString err;
               ObjectData imported;
               if (!ObjIO::import(path, imported, &err)) {
                   QMessageBox::critical(w, QObject::tr("Import"), QObject::tr("IMPORT FAILED: %1").arg(err));
                   return;
               }
               d->editActive(QObject::tr("Importing Object"), [&](ObjectData &o) { o.merge(imported); });
           }});
    r.add({Cmd::ExportObj, [=] {
               const QString path = QFileDialog::getSaveFileName(w, QObject::tr("Export OBJ"), QString(), QObject::tr("Object (*.obj)"));
               if (path.isEmpty())
                   return;
               QString err;
               if (!ObjIO::exportLod(path, d->active(), &err))
                   QMessageBox::critical(w, QObject::tr("Export"), err);
           },
           {}, {}, QStringLiteral("&File/Export"), QObject::tr("OBJ File (current LOD)...")});
    r.add({Cmd::ExportP3dOld, [=] {
               const QString path = QFileDialog::getSaveFileName(w, QObject::tr("Export P3D Old Version"), QString(), kP3dFilter);
               if (path.isEmpty())
                   return;
               QString err;
               if (!d->save(path, &err, true))
                   QMessageBox::critical(w, QObject::tr("Export"), err);
           }});

    r.add({Cmd::FileOptions, [=] {
               OptionsDialog dlg(w);
               if (dlg.exec() == QDialog::Accepted) {
                   const QColor bg = QSettings().value(QStringLiteral("CConfig/CBackgroun Color")).value<QColor>();
                   if (bg.isValid())
                       c.view->background = bg;
                   GameData::instance().startIndexing();
                   w->refreshViews();
               }
           }});
    r.add({Cmd::FileExit, [=] { w->close(); }});
}
