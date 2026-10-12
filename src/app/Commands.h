#pragma once

#include "app/CommandIds.h"
#include "app/CommandRegistry.h"
#include "app/Document.h"
#include "ui/MainWindow.h"
#include "ui/Viewport.h"

// Everything a command handler needs. Commands are grouped by menu into
// Commands*.cpp; each register* function adds its handlers to the registry.
// A command that is not registered stays visible in the menu but disabled.
struct AppContext {
    MainWindow *win;
    Document *doc;
    CommandRegistry *reg;
    ViewSettings *view;

    // Common predicates for `Command::enabled`.
    bool hasPoints() const { return !doc->active().points.isEmpty(); }
    bool hasSelectedPoints() const { return doc->active().selection.pointCount() > 0; }
    bool hasSelectedFaces() const { return doc->active().selection.faceCount() > 0; }
    // Original undo-history caption for a string-table id (10000+), or fallback.
    QString undo(int stringId, const QString &fallback) const { return win->undoText(stringId, fallback); }
};

void registerFileCommands(AppContext &ctx);     // File menu
void registerEditCommands(AppContext &ctx);     // Edit menu, named selections/properties panels
void registerGeometryCommands(AppContext &ctx); // Create, Structure, Points, Faces, Surfaces, LOD panel
void registerViewCommands(AppContext &ctx);     // View, Window, Help
void registerImportCommands(AppContext &ctx);   // game data, FBX, textures
