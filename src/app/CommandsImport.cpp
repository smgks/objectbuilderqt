// Game data, FBX import and texture display (features beyond the original).
#include "app/Commands.h"

#include "app/GameData.h"
#include "core/FbxIO.h"
#include "core/Pbo.h"
#include "core/TextureLibrary.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QVBoxLayout>

namespace {

// Lists every .p3d in the indexed game archives / work drive.
QString browseGameModels(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Open from Game Data"));
    auto *filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(QObject::tr("Filter, e.g. akm or structures\\military"));
    filter->setClearButtonEnabled(true);
    auto *model = new QStringListModel(GameFileSystem::instance().files(QStringLiteral(".p3d")), &dlg);
    auto *proxy = new QSortFilterProxyModel(&dlg);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    auto *view = new QListView(&dlg);
    view->setModel(proxy);
    view->setUniformItemSizes(true);
    auto *count = new QLabel(&dlg);
    auto updateCount = [=] { count->setText(QObject::tr("%1 of %2 models").arg(proxy->rowCount()).arg(model->rowCount())); };
    QObject::connect(filter, &QLineEdit::textChanged, &dlg, [=](const QString &t) {
        proxy->setFilterFixedString(QString(t).replace(QLatin1Char('/'), QLatin1Char('\\')));
        updateCount();
    });
    updateCount();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(view, &QListView::doubleClicked, &dlg, &QDialog::accept);
    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(filter);
    l->addWidget(view);
    l->addWidget(count);
    l->addWidget(buttons);
    dlg.resize(640, 560);
    filter->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return {};
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid() && proxy->rowCount() == 1)
        idx = proxy->index(0, 0);
    return idx.isValid() ? idx.data().toString() : QString();
}

} // namespace

void registerImportCommands(AppContext &c)
{
    CommandRegistry &r = *c.reg;
    MainWindow *w = c.win;
    Document *d = c.doc;
    ViewSettings *view = c.view;

    r.add({Cmd::OpenFromGameData, [=] {
               if (!GameData::instance().isIndexed()) {
                   w->showStatus(QObject::tr("Game data is still being indexed..."));
                   return;
               }
               if (GameFileSystem::instance().pboCount() == 0 && GameData::workDrive().isEmpty()) {
                   QMessageBox::information(w, QObject::tr("Open from Game Data"),
                                            QObject::tr("No game data found. Set the DayZ folder in File > Options."));
                   return;
               }
               if (!w->maybeSave())
                   return;
               const QString path = browseGameModels(w);
               if (!path.isEmpty())
                   w->openGameFile(path);
           },
           {}, {}, QStringLiteral("&File"), QObject::tr("Open from Game Data..."), QKeySequence(QStringLiteral("Ctrl+Shift+O"))});

    r.add({Cmd::ImportFbx, [=] {
               QSettings s;
               const QString path = QFileDialog::getOpenFileName(w, QObject::tr("Import FBX"), s.value(QStringLiteral("LastFbxFolder")).toString(),
                                                                 QObject::tr("FBX (*.fbx);;All files (*.*)"));
               if (path.isEmpty())
                   return;
               s.setValue(QStringLiteral("LastFbxFolder"), QFileInfo(path).absolutePath());
               FbxIO::Options opt;
               opt.defaultResolution = d->model().activeResolution();
               opt.workDrive = GameData::workDrive();
               FbxIO::Report rep;
               QString err;
               bool ok = false;
               QApplication::setOverrideCursor(Qt::WaitCursor);
               d->editAll(QObject::tr("Import FBX %1").arg(QFileInfo(path).fileName()),
                          [&](LodObject &m) { ok = FbxIO::import(path, m, opt, &rep, &err); });
               QApplication::restoreOverrideCursor();
               if (!ok) {
                   d->undoStack()->undo();
                   QMessageBox::critical(w, QObject::tr("Import FBX"), QObject::tr("IMPORT FAILED: %1").arg(err));
                   return;
               }
               TextureLibrary::instance().setModelFolder(QFileInfo(path).absolutePath());
               ++view->textureGeneration; // files may have just been extracted
               for (Viewport *v : w->viewports())
                   v->zoomToDrawing();
               QString textures = QObject::tr("%1 texture(s) found").arg(rep.texturesFound);
               if (!rep.texturesExtracted.isEmpty())
                   textures += QObject::tr(", %1 extracted from the FBX").arg(rep.texturesExtracted.size());
               if (!rep.texturesMissing.isEmpty())
                   textures += QObject::tr(", %1 missing").arg(rep.texturesMissing.size());
               w->showStatus(QObject::tr("Imported %1 mesh(es), %2 face(s) into %3 LOD(s); %4")
                                 .arg(rep.meshes)
                                 .arg(rep.faces)
                                 .arg(rep.lods)
                                 .arg(textures),
                             10000);
               if (!rep.texturesExtracted.isEmpty() || !rep.texturesMissing.isEmpty()) {
                   QString text;
                   if (!rep.texturesExtracted.isEmpty())
                       text += QObject::tr("Textures embedded in the FBX were saved to:\n%1\n\n").arg(rep.texturesExtracted.join(QLatin1Char('\n')));
                   if (!rep.texturesMissing.isEmpty())
                       text += QObject::tr("Texture files not found (paths kept as written in the FBX):\n%1").arg(rep.texturesMissing.join(QLatin1Char('\n')));
                   QMessageBox::information(w, QObject::tr("Import FBX"), text.trimmed());
               }
           },
           {}, {}, QStringLiteral("&File/Import"), QObject::tr("FBX File...")});

    r.add({Cmd::ShowTextures, [=] {
               view->textured = !view->textured;
               if (view->textured && !view->solid)
                   view->solid = true;
               emit d->changed(0);
           },
           {}, [=] { return view->textured && view->solid; }, QStringLiteral("&View"), QObject::tr("Show Textures"),
           QKeySequence(QStringLiteral("Ctrl+Alt+T"))});

    r.add({Cmd::ReloadTextures, [=] {
               TextureLibrary::instance().clear();
               ++view->textureGeneration;
               emit d->changed(0);
           },
           {}, {}, QStringLiteral("&View"), QObject::tr("Reload Textures")});
}
