#pragma once

#include "core/Primitives.h"

#include <QDialog>
#include <QVector3D>

class Document;
class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTreeWidget;

// Dialogs reconstructed from the original RT_DIALOG resources (ids in
// comments). Layouts follow the originals' grouping and labels, using Qt
// layouts instead of fixed dialog units.

// "Face Properies" (178). Edits the selected faces of the active LOD.
class FacePropertiesDialog : public QDialog {
    Q_OBJECT
public:
    FacePropertiesDialog(Document *doc, QWidget *parent = nullptr);

private:
    void load();
    void apply();

    Document *m_doc;
    QTreeWidget *m_list;
    QLineEdit *m_uv[4][2];
    QButtonGroup *m_light, *m_zbias;
    QCheckBox *m_shadow, *m_merge;
    QLineEdit *m_texture, *m_material, *m_user;
    QVector<int> m_faces;
};

// Point properties (175).
class PointPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    PointPropertiesDialog(Document *doc, QWidget *parent = nullptr);

private:
    void load();
    void apply();

    Document *m_doc;
    QListWidget *m_list;
    QLineEdit *m_xyz[3], *m_user;
    QButtonGroup *m_surface, *m_fog, *m_decal, *m_light;
    QCheckBox *m_hidden;
    QVector<int> m_points;
};

// "Transform" (160) used by Points > Transform 3D > Move/Rotate/Scale.
class TransformDialog : public QDialog {
    Q_OBJECT
public:
    enum class Kind { Move, Rotate, Scale };
    TransformDialog(Kind kind, QWidget *parent = nullptr);
    QVector3D value() const;
    bool relativeToPin() const;
    bool allAnimations() const;

private:
    QDoubleSpinBox *m_v[3];
    QCheckBox *m_pin, *m_anims;
};

// Create Plane/Box/Circle/Cylinder/Sphere (295, 298, 296, 297, 299).
class PrimitiveDialog : public QDialog {
    Q_OBJECT
public:
    enum class Kind { Plane, Box, Circle, Cylinder, Sphere };
    PrimitiveDialog(Kind kind, QWidget *parent = nullptr);
    Primitives::Params params() const;

private:
    Kind m_kind;
    QDoubleSpinBox *m_sizeX = nullptr, *m_sizeY = nullptr, *m_sizeZ = nullptr, *m_radius = nullptr;
    QSpinBox *m_segX = nullptr, *m_segY = nullptr, *m_segZ = nullptr, *m_segR = nullptr;
    QCheckBox *m_center = nullptr;
    QButtonGroup *m_align;
};

// "Select LOD" (151): pick a special LOD or type a resolution.
class ResolutionDialog : public QDialog {
    Q_OBJECT
public:
    ResolutionDialog(float initial, QWidget *parent = nullptr);
    bool resolution(float &out) const;

private:
    QListWidget *m_list;
    QLineEdit *m_edit;
};

// "Config" (171): name/value option list stored in QSettings.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QTreeWidget *m_tree;
};

// "About Object Builder" (100).
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
