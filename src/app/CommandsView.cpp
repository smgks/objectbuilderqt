// View, Window and Help menus.
#include "app/Commands.h"

#include "ui/Dialogs.h"

#include <QDockWidget>
#include <QStatusBar>

void registerViewCommands(AppContext &c)
{
    CommandRegistry &r = *c.reg;
    MainWindow *w = c.win;
    Document *d = c.doc;
    ViewSettings *view = c.view;

    // --- view direction of the active viewport -------------------------------
    for (auto [id, type] : {std::pair{Cmd::ViewFront, ViewType::Front}, std::pair{Cmd::ViewTop, ViewType::Top},
                            std::pair{Cmd::ViewLeft, ViewType::Left}, std::pair{Cmd::ViewProjected, ViewType::Projected},
                            std::pair{Cmd::ViewRight, ViewType::Right}, std::pair{Cmd::ViewBack, ViewType::Back},
                            std::pair{Cmd::ViewBottom, ViewType::Bottom}}) {
        r.add({id, [=] { w->activeViewport()->setViewType(type); }, {}, [=] { return w->activeViewport()->viewType() == type; }});
    }

    // --- zoom ---------------------------------------------------------------
    r.add({Cmd::ZoomIn, [=] { w->activeViewport()->zoomBy(0.8f); }});
    r.add({Cmd::ZoomOut, [=] { w->activeViewport()->zoomBy(1.25f); }});
    r.add({Cmd::ZoomDrawing, [=] {
               for (Viewport *v : w->viewports())
                   v->zoomToDrawing();
           }});
    r.add({Cmd::ZoomSelection, [=] {
               for (Viewport *v : w->viewports())
                   v->zoomToSelection();
           },
           [c] { return c.hasSelectedPoints(); }});
    r.add({Cmd::LookAtPin, [=] {
               for (Viewport *v : w->viewports())
                   v->lookAt(d->pin());
           }});
    r.add({Cmd::Refresh, [=] { w->refreshViews(); }});

    // --- display toggles -----------------------------------------------------
    auto toggle = [=](bool ViewSettings::*member) {
        return [=] {
            view->*member = !(view->*member);
            for (Viewport *v : w->viewports()) {
                v->update();
            }
            // geometry buffers depend on the solid flag
            emit d->changed(0);
        };
    };
    r.add({Cmd::SolidFill, toggle(&ViewSettings::solid), {}, [=] { return view->solid; }});
    r.add({Cmd::FaceCulling, toggle(&ViewSettings::culling), {}, [=] { return view->culling; }});
    r.add({Cmd::Grid, toggle(&ViewSettings::grid), {}, [=] { return view->grid; }});

    // --- Window ---------------------------------------------------------------
    r.add({Cmd::FourViews, [=] { w->setFourViews(!w->fourViews()); }, {}, [=] { return w->fourViews(); }});
    r.add({Cmd::StatusBar, [=] { w->statusBar()->setVisible(!w->statusBar()->isVisible()); }, {},
           [=] { return w->statusBar()->isVisible(); }});
    for (int id : {Cmd::WinHistory, Cmd::WinPosition, Cmd::WinLods, Cmd::WinMass, Cmd::WinNamedProps, Cmd::WinNamedSelections}) {
        QDockWidget *dock = w->dockFor(id);
        if (!dock)
            continue;
        r.add({id, [dock] { dock->setVisible(!dock->isVisible()); }, {}, [dock] { return dock->isVisible(); }});
    }

    // --- Help -----------------------------------------------------------------
    r.add({Cmd::About, [=] { AboutDialog(w).exec(); }});
}
