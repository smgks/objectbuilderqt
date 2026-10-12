#pragma once

#include "core/LodObject.h"

#include <QObject>
#include <QUndoStack>
#include <QVector3D>

#include <functional>

// The open model (CObjektiv2Doc in the original). All modifications go through
// editActive()/editAll() so they land on the undo stack; read access is direct.
//
//   doc->editActive(tr("Reverse"), [](ObjectData &o) { ... });
//
// Each edit stores a before/after snapshot of what it touched (one LOD for
// editActive, the whole LodObject for editAll). Simple and robust; switch to
// finer-grained commands if memory ever becomes an issue.
class Document : public QObject {
    Q_OBJECT
public:
    enum class Change {
        Geometry = 1,   // points/faces/normals/UV
        Selection = 2,  // current selection, hide, lock
        Named = 4,      // named selections / properties / mass
        Lods = 8,       // LOD list or active LOD
        All = 15,
    };

    explicit Document(QObject *parent = nullptr);

    const LodObject &model() const { return m_model; }
    const ObjectData &active() const { return m_model.activeData(); }
    int activeLod() const { return m_model.active; }
    QUndoStack *undoStack() { return &m_undo; }

    QString filePath() const { return m_path; }
    bool isModified() const { return !m_undo.isClean() || m_dirtyWithoutUndo; }

    // --- file -------------------------------------------------------------
    void newModel();
    bool open(const QString &path, QString *error);
    // Opens MLOD or binarized (ODOL) data. Converted ODOL models and models read
    // from game archives have no file path yet (Save asks where to save).
    bool openData(const QByteArray &data, const QString &sourcePath, QString *error);
    QString suggestedName() const { return m_suggestedName; }
    QStringList lastWarnings() const { return m_lastWarnings; }
    bool save(const QString &path, QString *error, bool oldFormat = false);
    void replaceModel(const LodObject &obj, const QString &path);

    // --- editing ----------------------------------------------------------
    void editActive(const QString &text, const std::function<void(ObjectData &)> &fn, int changes = int(Change::All));
    void editAll(const QString &text, const std::function<void(LodObject &)> &fn, int changes = int(Change::All));
    // Edit without an undo entry (live previews, interactive drags). Call
    // commitPreview() afterwards to collapse the whole drag into one entry.
    void beginPreview();
    void previewActive(const std::function<void(ObjectData &)> &fn, int changes = int(Change::Geometry));
    void commitPreview(const QString &text);
    void cancelPreview();
    bool inPreview() const { return m_previewActive; }

    void setActiveLod(int index);

    // --- editor state (not part of the file, not undoable) ----------------
    QVector3D pin() const { return m_pin; }
    void setPin(const QVector3D &p);

    // Stored selection for Edit > Selection operators.
    Selection storedSelection;

    // Called by undo commands.
    void applyLodSnapshot(int lodIndex, const ObjectData &data, int changes);
    void applyModelSnapshot(const LodObject &obj, int changes);

signals:
    void changed(int changes);
    void pinChanged();
    void fileChanged(); // path or modified state

private:
    LodObject m_model;
    QUndoStack m_undo;
    QString m_path;
    QVector3D m_pin;
    bool m_dirtyWithoutUndo = false;
    QString m_suggestedName;
    QStringList m_lastWarnings;

    bool m_previewActive = false;
    ObjectData m_previewBefore;
    int m_previewLod = 0;
};
