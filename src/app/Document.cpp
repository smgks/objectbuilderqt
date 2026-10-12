#include "Document.h"

#include "core/Odol.h"
#include "core/P3dIO.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>

#include <QUndoCommand>

namespace {

class LodSnapshotCommand : public QUndoCommand {
public:
    LodSnapshotCommand(Document *doc, const QString &text, int lod, ObjectData before, ObjectData after, int changes)
        : QUndoCommand(text), m_doc(doc), m_lod(lod), m_before(std::move(before)), m_after(std::move(after)), m_changes(changes)
    {
    }
    void undo() override { m_doc->applyLodSnapshot(m_lod, m_before, m_changes); }
    void redo() override
    {
        if (m_skipFirstRedo) {
            m_skipFirstRedo = false;
            return;
        }
        m_doc->applyLodSnapshot(m_lod, m_after, m_changes);
    }
    bool m_skipFirstRedo = true; // the edit is already applied when pushed

private:
    Document *m_doc;
    int m_lod;
    ObjectData m_before, m_after;
    int m_changes;
};

class ModelSnapshotCommand : public QUndoCommand {
public:
    ModelSnapshotCommand(Document *doc, const QString &text, LodObject before, LodObject after, int changes)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)), m_changes(changes)
    {
    }
    void undo() override { m_doc->applyModelSnapshot(m_before, m_changes); }
    void redo() override
    {
        if (m_skipFirstRedo) {
            m_skipFirstRedo = false;
            return;
        }
        m_doc->applyModelSnapshot(m_after, m_changes);
    }
    bool m_skipFirstRedo = true;

private:
    Document *m_doc;
    LodObject m_before, m_after;
    int m_changes;
};

} // namespace

Document::Document(QObject *parent) : QObject(parent)
{
    m_undo.setUndoLimit(64);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &Document::fileChanged);
}

void Document::newModel()
{
    replaceModel(LodObject(), QString());
}

void Document::replaceModel(const LodObject &obj, const QString &path)
{
    m_model = obj;
    if (m_model.lods.isEmpty())
        m_model = LodObject();
    m_path = path;
    m_pin = QVector3D();
    m_undo.clear();
    m_undo.setClean();
    m_dirtyWithoutUndo = false;
    m_previewActive = false;
    emit changed(int(Change::All));
    emit pinChanged();
    emit fileChanged();
}

bool Document::open(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return openData(f.readAll(), path, error);
}

bool Document::openData(const QByteArray &data, const QString &sourcePath, QString *error)
{
    LodObject obj;
    if (Odol::isOdol(data)) {
        Odol::Info info;
        if (!Odol::load(data, obj, error, &info))
            return false;
        // A converted binarized model must not overwrite its source: save as new.
        replaceModel(obj, QString());
        m_suggestedName = QFileInfo(sourcePath).completeBaseName() + QStringLiteral(".p3d");
        m_dirtyWithoutUndo = true;
        m_lastWarnings = info.warnings;
        emit fileChanged();
        return true;
    }
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    if (!P3dIO::load(buf, obj, error))
        return false;
    const bool onDisk = QFileInfo::exists(sourcePath);
    replaceModel(obj, onDisk ? sourcePath : QString());
    m_suggestedName = QFileInfo(sourcePath).fileName();
    m_lastWarnings.clear();
    if (!onDisk) {
        m_dirtyWithoutUndo = true;
        emit fileChanged();
    }
    return true;
}

bool Document::save(const QString &path, QString *error, bool oldFormat)
{
    P3dIO::SaveOptions opt;
    opt.oldFormat = oldFormat;
    if (!P3dIO::save(path, m_model, opt, error))
        return false;
    if (!oldFormat) {
        m_path = path;
        m_undo.setClean();
        m_dirtyWithoutUndo = false;
        emit fileChanged();
    }
    return true;
}

void Document::editActive(const QString &text, const std::function<void(ObjectData &)> &fn, int changes)
{
    if (m_previewActive)
        cancelPreview();
    ObjectData before = m_model.activeData();
    fn(m_model.activeData());
    m_model.activeData().normalizeArrays();
    m_undo.push(new LodSnapshotCommand(this, text, m_model.active, std::move(before), m_model.activeData(), changes));
    emit changed(changes);
}

void Document::editAll(const QString &text, const std::function<void(LodObject &)> &fn, int changes)
{
    if (m_previewActive)
        cancelPreview();
    LodObject before = m_model;
    fn(m_model);
    if (m_model.lods.isEmpty())
        m_model = LodObject();
    m_model.active = qBound(0, m_model.active, int(m_model.lods.size()) - 1);
    for (auto &l : m_model.lods)
        l.data.normalizeArrays();
    m_undo.push(new ModelSnapshotCommand(this, text, std::move(before), m_model, changes));
    emit changed(changes);
}

void Document::beginPreview()
{
    if (m_previewActive)
        return;
    m_previewActive = true;
    m_previewLod = m_model.active;
    m_previewBefore = m_model.activeData();
}

void Document::previewActive(const std::function<void(ObjectData &)> &fn, int changes)
{
    if (!m_previewActive)
        beginPreview();
    fn(m_model.lods[m_previewLod].data);
    emit changed(changes);
}

void Document::commitPreview(const QString &text)
{
    if (!m_previewActive)
        return;
    m_previewActive = false;
    m_undo.push(new LodSnapshotCommand(this, text, m_previewLod, std::move(m_previewBefore), m_model.lods[m_previewLod].data,
                                       int(Change::All)));
    m_previewBefore = ObjectData();
}

void Document::cancelPreview()
{
    if (!m_previewActive)
        return;
    m_previewActive = false;
    m_model.lods[m_previewLod].data = m_previewBefore;
    m_previewBefore = ObjectData();
    emit changed(int(Change::All));
}

void Document::setActiveLod(int index)
{
    if (index < 0 || index >= m_model.lods.size() || index == m_model.active)
        return;
    if (m_previewActive)
        cancelPreview();
    m_model.active = index;
    emit changed(int(Change::Lods) | int(Change::Geometry) | int(Change::Selection) | int(Change::Named));
}

void Document::setPin(const QVector3D &p)
{
    m_pin = p;
    emit pinChanged();
}

void Document::applyLodSnapshot(int lodIndex, const ObjectData &data, int changes)
{
    if (lodIndex < 0 || lodIndex >= m_model.lods.size())
        return;
    m_model.lods[lodIndex].data = data;
    if (m_model.active != lodIndex) {
        m_model.active = lodIndex; // show the user what was undone
        changes |= int(Change::Lods);
    }
    emit changed(changes);
}

void Document::applyModelSnapshot(const LodObject &obj, int changes)
{
    m_model = obj;
    emit changed(changes | int(Change::Lods));
}
