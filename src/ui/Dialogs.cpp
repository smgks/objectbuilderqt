#include "Dialogs.h"

#include "app/Document.h"
#include "core/Flags.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const QString kMixed = QStringLiteral("...");

QGroupBox *radioGroup(const QString &title, const QStringList &labels, QButtonGroup *&group, QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    auto *l = new QVBoxLayout(box);
    group = new QButtonGroup(box);
    group->setExclusive(false); // allows the "mixed" (nothing checked) state
    for (int i = 0; i < labels.size(); ++i) {
        auto *r = new QRadioButton(labels[i], box);
        group->addButton(r, i);
        l->addWidget(r);
    }
    QObject::connect(group, &QButtonGroup::idClicked, box, [group](int id) {
        for (QAbstractButton *b : group->buttons())
            b->setChecked(group->id(b) == id);
    });
    l->addStretch(1);
    return box;
}

void setRadio(QButtonGroup *g, int id)
{
    for (QAbstractButton *b : g->buttons())
        b->setChecked(g->id(b) == id);
}

int radioValue(QButtonGroup *g)
{
    for (QAbstractButton *b : g->buttons())
        if (b->isChecked())
            return g->id(b);
    return -1;
}

// Shared value across items, or "mixed".
template <typename T>
struct Common {
    bool any = false, mixed = false;
    T value{};
    void add(const T &v)
    {
        if (!any) {
            value = v;
            any = true;
        } else if (!(v == value)) {
            mixed = true;
        }
    }
};

QString fmt(float v)
{
    return QString::number(double(v), 'g', 7);
}

// Label whose '&' mnemonic focuses `buddy`, like the original dialog statics.
QLabel *buddyLabel(const QString &text, QWidget *buddy)
{
    auto *l = new QLabel(text, buddy->parentWidget());
    l->setBuddy(buddy);
    return l;
}

} // namespace

// ================================================================ Face properties

FacePropertiesDialog::FacePropertiesDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Face Properties"));
    m_list = new QTreeWidget(this);
    m_list->setHeaderLabels({tr("V1"), tr("V2"), tr("V3"), tr("V4"), tr("Texture"), tr("Material"), tr("User")});
    m_list->setRootIsDecorated(false);
    m_list->setMinimumHeight(140);

    auto *mapping = new QGroupBox(tr("&Mapping"), this);
    auto *mg = new QGridLayout(mapping);
    mg->addWidget(new QLabel(QStringLiteral("U")), 1, 0);
    mg->addWidget(new QLabel(QStringLiteral("V")), 2, 0);
    for (int k = 0; k < 4; ++k) {
        mg->addWidget(new QLabel(QStringLiteral("v%1").arg(k + 1)), 0, k + 1);
        for (int c = 0; c < 2; ++c) {
            m_uv[k][c] = new QLineEdit(mapping);
            mg->addWidget(m_uv[k][c], c + 1, k + 1);
        }
    }

    auto *lightBox = radioGroup(tr("&Lighting && shadows"),
                                {tr("Normal"), tr("Both sides"), tr("Position"), tr("Flat"), tr("Reversed (transparent)")},
                                m_light, this);
    m_shadow = new QCheckBox(tr("Enable s&hadow"), lightBox);
    m_shadow->setTristate(true);
    lightBox->layout()->addWidget(m_shadow);
    auto *zbox = radioGroup(tr("&Z bias"), {tr("None"), tr("Low"), tr("Middle"), tr("High")}, m_zbias, this);

    auto *tex = new QGroupBox(tr("Texture && materials"), this);
    auto *tg = new QGridLayout(tex);
    m_texture = new QLineEdit(tex);
    m_material = new QLineEdit(tex);
    m_merge = new QCheckBox(tr("Enable texture merging"), tex);
    m_merge->setTristate(true);
    auto *browseTex = new QPushButton(QStringLiteral("B"), tex);
    auto *browseMat = new QPushButton(QStringLiteral("B"), tex);
    browseTex->setToolTip(tr("Browse"));
    browseMat->setToolTip(tr("Browse"));
    browseTex->setFixedWidth(28);
    browseMat->setFixedWidth(28);
    tg->addWidget(buddyLabel(tr("&Texture"), m_texture), 0, 0);
    tg->addWidget(m_texture, 1, 0);
    tg->addWidget(browseTex, 1, 1);
    tg->addWidget(m_merge, 2, 0);
    tg->addWidget(buddyLabel(tr("Mate&rial"), m_material), 3, 0);
    tg->addWidget(m_material, 4, 0);
    tg->addWidget(browseMat, 4, 1);
    m_user = new QLineEdit(tex);
    auto *userRow = new QHBoxLayout;
    userRow->addStretch(1);
    userRow->addWidget(buddyLabel(tr("&User value:"), m_user));
    userRow->addWidget(m_user);
    tg->addLayout(userRow, 5, 0, 1, 2);
    auto browse = [this](QLineEdit *e, const QString &filter) {
        const QString f = QFileDialog::getOpenFileName(this, tr("Select file"), e->text(), filter);
        if (!f.isEmpty())
            e->setText(QDir::toNativeSeparators(f));
    };
    connect(browseTex, &QPushButton::clicked, this, [=] { browse(m_texture, tr("Textures (*.paa *.pac *.tga *.png);;All files (*.*)")); });
    connect(browseMat, &QPushButton::clicked, this,
            [=] { browse(m_material, tr("Real Virtuality Material (*.rvmat);;All files (*.*)")); });

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(lightBox);
    bottom->addWidget(zbox);
    bottom->addWidget(tex, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        apply();
        load();
    });

    auto *l = new QVBoxLayout(this);
    l->addWidget(buddyLabel(tr("Selected &Faces"), m_list));
    l->addWidget(m_list);
    l->addWidget(mapping);
    l->addLayout(bottom);
    l->addWidget(buttons);
    load();
}

void FacePropertiesDialog::load()
{
    const ObjectData &o = m_doc->active();
    m_faces.clear();
    for (int f = 0; f < o.faces.size(); ++f)
        if (o.selection.face(f))
            m_faces.append(f);

    m_list->clear();
    Common<QString> tex, mat;
    Common<int> light, zb, user, shadow, merge;
    const int lightValues[5] = {0, int(FaceFlags::LightBothSides), int(FaceFlags::LightPosition), int(FaceFlags::LightFlat),
                                int(FaceFlags::LightReversed)};
    for (int f : m_faces) {
        const Face &fc = o.faces[f];
        auto *it = new QTreeWidgetItem(m_list);
        for (int k = 0; k < 4; ++k)
            it->setText(k, k < fc.n ? QString::number(fc.v[k].point) : QString());
        it->setText(4, fc.texture);
        it->setText(5, fc.material);
        it->setText(6, QString::number(fc.flags >> FaceFlags::UserShift));
        tex.add(fc.texture);
        mat.add(fc.material);
        int li = -1;
        for (int i = 0; i < 5; ++i)
            if ((fc.flags & FaceFlags::LightingMask) == quint32(lightValues[i]))
                li = i;
        light.add(li);
        zb.add(int((fc.flags & FaceFlags::ZBiasMask) >> 8));
        user.add(int(fc.flags >> FaceFlags::UserShift));
        shadow.add((fc.flags & FaceFlags::NoShadow) ? 0 : 1);
        merge.add((fc.flags & FaceFlags::NoTextureMerge) ? 0 : 1);
    }
    m_texture->setText(tex.mixed ? kMixed : tex.value);
    m_material->setText(mat.mixed ? kMixed : mat.value);
    m_user->setText(user.mixed ? QString() : QString::number(user.value));
    setRadio(m_light, light.mixed ? -1 : light.value);
    setRadio(m_zbias, zb.mixed ? -1 : zb.value);
    m_shadow->setCheckState(shadow.mixed ? Qt::PartiallyChecked : shadow.value ? Qt::Checked : Qt::Unchecked);
    m_merge->setCheckState(merge.mixed ? Qt::PartiallyChecked : merge.value ? Qt::Checked : Qt::Unchecked);

    const bool single = m_faces.size() == 1;
    for (int k = 0; k < 4; ++k)
        for (int c = 0; c < 2; ++c) {
            const bool on = single && k < o.faces[m_faces[0]].n;
            m_uv[k][c]->setEnabled(on);
            m_uv[k][c]->setText(on ? fmt(c == 0 ? o.uv(m_faces[0], k).x() : o.uv(m_faces[0], k).y()) : QString());
        }
}

void FacePropertiesDialog::apply()
{
    if (m_faces.isEmpty())
        return;
    const int light = radioValue(m_light), zb = radioValue(m_zbias);
    const quint32 lightValues[5] = {0, FaceFlags::LightBothSides, FaceFlags::LightPosition, FaceFlags::LightFlat,
                                    FaceFlags::LightReversed};
    const QString tex = m_texture->text(), mat = m_material->text(), user = m_user->text();
    const Qt::CheckState shadow = m_shadow->checkState(), merge = m_merge->checkState();
    QVector<QVector2D> uv(4);
    for (int k = 0; k < 4; ++k)
        uv[k] = QVector2D(m_uv[k][0]->text().toFloat(), m_uv[k][1]->text().toFloat());
    const QVector<int> faces = m_faces;

    m_doc->editActive(tr("Face Properties"), [&](ObjectData &o) {
        for (int f : faces) {
            Face &fc = o.faces[f];
            if (light >= 0)
                fc.flags = (fc.flags & ~FaceFlags::LightingMask) | lightValues[light];
            if (zb >= 0)
                fc.flags = (fc.flags & ~FaceFlags::ZBiasMask) | (quint32(zb) << 8);
            if (shadow != Qt::PartiallyChecked)
                fc.flags = shadow == Qt::Checked ? fc.flags & ~FaceFlags::NoShadow : fc.flags | FaceFlags::NoShadow;
            if (merge != Qt::PartiallyChecked)
                fc.flags = merge == Qt::Checked ? fc.flags & ~FaceFlags::NoTextureMerge : fc.flags | FaceFlags::NoTextureMerge;
            if (!user.isEmpty())
                fc.flags = (fc.flags & ~FaceFlags::UserMask) | ((user.toUInt() & 0x7f) << FaceFlags::UserShift);
            if (tex != kMixed)
                fc.texture = tex;
            if (mat != kMixed)
                fc.material = mat;
        }
        if (faces.size() == 1)
            for (int k = 0; k < o.faces[faces[0]].n; ++k)
                o.uvSet0().uv[faces[0]][k] = uv[k];
    }, int(Document::Change::Geometry));
}

// ================================================================ Point properties

PointPropertiesDialog::PointPropertiesDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Point Properties"));
    m_list = new QListWidget(this);
    m_list->setMaximumHeight(110);
    auto *coords = new QGridLayout;
    const char *labels[3] = {"&X", "&Y", "&Z"};
    for (int i = 0; i < 3; ++i) {
        m_xyz[i] = new QLineEdit(this);
        coords->addWidget(buddyLabel(tr(labels[i]), m_xyz[i]), 0, i);
        coords->addWidget(m_xyz[i], 1, i);
    }
    m_user = new QLineEdit(this);
    coords->addWidget(buddyLabel(tr("&User"), m_user), 0, 3);
    coords->addWidget(m_user, 1, 3);

    auto *surface = radioGroup(tr("&Surface"),
                               {tr("Normal"), tr("On surface"), tr("Above surface"), tr("Under surface"), tr("Keep height (fence)")},
                               m_surface, this);
    auto *fog = radioGroup(tr("&Fog"), {tr("Normal"), tr("Sky"), tr("None")}, m_fog, this);
    auto *decal = radioGroup(tr("&Decal"), {tr("Normal"), tr("Decal")}, m_decal, this);
    auto *light = radioGroup(tr("&Lighting (Obsolete)"),
                             {tr("Normal"), tr("Shining"), tr("Always in shadow"), tr("Half lighted"), tr("Fully lighted")}, m_light,
                             this);
    m_hidden = new QCheckBox(tr("&Hidden Vertex"), this);
    m_hidden->setTristate(true);

    auto *left = new QVBoxLayout;
    left->addWidget(surface);
    left->addWidget(fog);
    left->addWidget(m_hidden);
    auto *right = new QVBoxLayout;
    right->addWidget(decal);
    right->addWidget(light);
    auto *groups = new QHBoxLayout;
    groups->addLayout(left);
    groups->addLayout(right);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        apply();
        load();
    });
    auto *l = new QVBoxLayout(this);
    l->addWidget(buddyLabel(tr("Selected &Vertices"), m_list));
    l->addWidget(m_list);
    l->addLayout(coords);
    l->addLayout(groups);
    l->addWidget(buttons);
    load();
}

namespace {
const quint32 kSurface[5] = {0, PointFlags::OnSurface, PointFlags::AboveSurface, PointFlags::UnderSurface, PointFlags::KeepHeight};
const quint32 kFog[3] = {0, PointFlags::FogSky, PointFlags::FogNone};
const quint32 kDecal[2] = {0, PointFlags::Decal};
const quint32 kLight[5] = {0, PointFlags::LightShining, PointFlags::LightInShadow, PointFlags::LightHalf, PointFlags::LightFull};

template <int N>
int indexOf(const quint32 (&values)[N], quint32 v)
{
    for (int i = 0; i < N; ++i)
        if (values[i] == v)
            return i;
    return -1;
}
} // namespace

void PointPropertiesDialog::load()
{
    const ObjectData &o = m_doc->active();
    m_points.clear();
    m_list->clear();
    Common<float> c[3];
    Common<int> user, surf, fog, decal, light, hidden;
    for (int i = 0; i < o.points.size(); ++i) {
        if (!o.selection.point(i))
            continue;
        m_points.append(i);
        const Point &p = o.points[i];
        if (m_list->count() < 500)
            m_list->addItem(QStringLiteral("%1:  %2, %3, %4").arg(i).arg(fmt(p.pos.x()), fmt(p.pos.y()), fmt(p.pos.z())));
        for (int k = 0; k < 3; ++k)
            c[k].add(p.pos[k]);
        user.add(int((p.flags & PointFlags::UserMask) >> PointFlags::UserShift));
        surf.add(indexOf(kSurface, p.flags & PointFlags::SurfaceMask));
        fog.add(indexOf(kFog, p.flags & PointFlags::FogMask));
        decal.add(indexOf(kDecal, p.flags & PointFlags::DecalMask));
        light.add(indexOf(kLight, p.flags & PointFlags::LightMask));
        hidden.add((p.flags & PointFlags::Hidden) ? 1 : 0);
    }
    for (int k = 0; k < 3; ++k)
        m_xyz[k]->setText(c[k].mixed || !c[k].any ? QString() : fmt(c[k].value));
    m_user->setText(user.mixed || !user.any ? QString() : QString::number(user.value));
    setRadio(m_surface, surf.mixed ? -1 : surf.value);
    setRadio(m_fog, fog.mixed ? -1 : fog.value);
    setRadio(m_decal, decal.mixed ? -1 : decal.value);
    setRadio(m_light, light.mixed ? -1 : light.value);
    m_hidden->setCheckState(hidden.mixed ? Qt::PartiallyChecked : hidden.value ? Qt::Checked : Qt::Unchecked);
}

void PointPropertiesDialog::apply()
{
    if (m_points.isEmpty())
        return;
    QString xyz[3];
    for (int k = 0; k < 3; ++k)
        xyz[k] = m_xyz[k]->text().trimmed();
    const QString user = m_user->text().trimmed();
    const int surf = radioValue(m_surface), fog = radioValue(m_fog), decal = radioValue(m_decal), light = radioValue(m_light);
    const Qt::CheckState hidden = m_hidden->checkState();
    const QVector<int> pts = m_points;
    m_doc->editActive(tr("Point Properties"), [&](ObjectData &o) {
        for (int i : pts) {
            Point &p = o.points[i];
            for (int k = 0; k < 3; ++k)
                if (!xyz[k].isEmpty())
                    p.pos[k] = xyz[k].toFloat();
            if (!user.isEmpty())
                p.flags = (p.flags & ~PointFlags::UserMask) | ((user.toUInt() & 0xff) << PointFlags::UserShift);
            if (surf >= 0)
                p.flags = (p.flags & ~PointFlags::SurfaceMask) | kSurface[surf];
            if (fog >= 0)
                p.flags = (p.flags & ~PointFlags::FogMask) | kFog[fog];
            if (decal >= 0)
                p.flags = (p.flags & ~PointFlags::DecalMask) | kDecal[decal];
            if (light >= 0)
                p.flags = (p.flags & ~PointFlags::LightMask) | kLight[light];
            if (hidden != Qt::PartiallyChecked)
                p.flags = hidden == Qt::Checked ? p.flags | PointFlags::Hidden : p.flags & ~PointFlags::Hidden;
        }
    }, int(Document::Change::Geometry));
}

// ================================================================ Transform

TransformDialog::TransformDialog(Kind kind, QWidget *parent) : QDialog(parent)
{
    const char *titles[3] = {"Move", "Rotate", "Scale"};
    setWindowTitle(tr("Transform - %1").arg(tr(titles[int(kind)])));
    auto *grid = new QGridLayout;
    const char *labels[3] = {"&X Value", "&Y Value", "&Z Value"};
    for (int i = 0; i < 3; ++i) {
        m_v[i] = new QDoubleSpinBox(this);
        grid->addWidget(buddyLabel(tr(labels[i]), m_v[i]), 0, i);
        m_v[i]->setRange(-1e6, 1e6);
        m_v[i]->setDecimals(4);
        m_v[i]->setValue(kind == Kind::Scale ? 1.0 : 0.0);
        if (kind == Kind::Rotate)
            m_v[i]->setSuffix(QStringLiteral(" °"));
        grid->addWidget(m_v[i], 1, i);
    }
    m_pin = new QCheckBox(tr("&Relative to pin"), this);
    m_pin->setChecked(kind != Kind::Move);
    m_pin->setEnabled(kind != Kind::Move);
    m_anims = new QCheckBox(tr("Apply to all &animations"), this);
    auto *allLods = new QCheckBox(tr("Apply to all &lods"), this);
    allLods->setEnabled(false);
    allLods->setToolTip(tr("Not implemented yet"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *l = new QVBoxLayout(this);
    l->addLayout(grid);
    l->addWidget(m_pin);
    l->addWidget(m_anims);
    l->addWidget(allLods);
    l->addWidget(buttons);
}

QVector3D TransformDialog::value() const
{
    return QVector3D(float(m_v[0]->value()), float(m_v[1]->value()), float(m_v[2]->value()));
}

bool TransformDialog::relativeToPin() const
{
    return m_pin->isChecked();
}

bool TransformDialog::allAnimations() const
{
    return m_anims->isChecked();
}

// ================================================================ Primitives

PrimitiveDialog::PrimitiveDialog(Kind kind, QWidget *parent) : QDialog(parent), m_kind(kind)
{
    const char *titles[5] = {"Create Plane", "Create Box", "Create Circle", "Create cylinder", "Create Sphere"};
    setWindowTitle(tr(titles[int(kind)]));
    QSettings s;
    const QString key = QStringLiteral("Primitives/%1/").arg(int(kind));
    auto *form = new QFormLayout;
    auto size = [&](const QString &label, const QString &name, double def) {
        auto *sb = new QDoubleSpinBox(this);
        sb->setRange(0.0001, 1e5);
        sb->setDecimals(4);
        sb->setValue(s.value(key + name, def).toDouble());
        form->addRow(label, sb);
        return sb;
    };
    auto seg = [&](const QString &label, const QString &name, int def) {
        auto *sb = new QSpinBox(this);
        sb->setRange(1, 1000);
        sb->setValue(s.value(key + name, def).toInt());
        form->addRow(label, sb);
        return sb;
    };
    switch (kind) {
    case Kind::Plane:
        m_sizeX = size(tr("Size X:"), QStringLiteral("sx"), 1);
        m_segX = seg(tr("Segments X:"), QStringLiteral("gx"), 1);
        m_sizeY = size(tr("Size Y:"), QStringLiteral("sy"), 1);
        m_segY = seg(tr("Segments Y:"), QStringLiteral("gy"), 1);
        break;
    case Kind::Box:
        m_sizeX = size(tr("Size X:"), QStringLiteral("sx"), 1);
        m_segX = seg(tr("Segments X:"), QStringLiteral("gx"), 1);
        m_sizeY = size(tr("Size Y:"), QStringLiteral("sy"), 1);
        m_segY = seg(tr("Segments Y:"), QStringLiteral("gy"), 1);
        m_sizeZ = size(tr("Size Z:"), QStringLiteral("sz"), 1);
        m_segZ = seg(tr("Segments Z:"), QStringLiteral("gz"), 1);
        break;
    case Kind::Circle:
        m_radius = size(tr("Radius:"), QStringLiteral("r"), 0.5);
        m_segR = seg(tr("Segments Radius:"), QStringLiteral("gr"), 16);
        break;
    case Kind::Cylinder:
        m_radius = size(tr("Radius:"), QStringLiteral("r"), 0.5);
        m_segR = seg(tr("Segments Radius:"), QStringLiteral("gr"), 16);
        m_sizeZ = size(tr("Size Z:"), QStringLiteral("sz"), 1);
        m_segZ = seg(tr("Segments Z:"), QStringLiteral("gz"), 1);
        break;
    case Kind::Sphere:
        m_radius = size(tr("Radius:"), QStringLiteral("r"), 0.5);
        m_segR = seg(tr("Segments:"), QStringLiteral("gr"), 16);
        break;
    }
    if (kind == Kind::Circle || kind == Kind::Cylinder) {
        m_center = new QCheckBox(tr("Center of circle"), this);
        m_center->setChecked(s.value(key + QStringLiteral("c"), false).toBool());
        form->addRow(QString(), m_center);
    }
    auto *align = radioGroup(tr("Align"), {tr("Center To Pin"), tr("Align bottom"), tr("Align top")}, m_align, this);
    setRadio(m_align, s.value(key + QStringLiteral("align"), 0).toInt());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, key] {
        QSettings st;
        auto save = [&](const QString &n, QWidget *w) {
            if (auto *d = qobject_cast<QDoubleSpinBox *>(w))
                st.setValue(key + n, d->value());
            else if (auto *i = qobject_cast<QSpinBox *>(w))
                st.setValue(key + n, i->value());
        };
        if (m_sizeX) save(QStringLiteral("sx"), m_sizeX);
        if (m_sizeY) save(QStringLiteral("sy"), m_sizeY);
        if (m_sizeZ) save(QStringLiteral("sz"), m_sizeZ);
        if (m_radius) save(QStringLiteral("r"), m_radius);
        if (m_segX) save(QStringLiteral("gx"), m_segX);
        if (m_segY) save(QStringLiteral("gy"), m_segY);
        if (m_segZ) save(QStringLiteral("gz"), m_segZ);
        if (m_segR) save(QStringLiteral("gr"), m_segR);
        if (m_center) st.setValue(key + QStringLiteral("c"), m_center->isChecked());
        st.setValue(key + QStringLiteral("align"), std::max(0, radioValue(m_align)));
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *l = new QVBoxLayout(this);
    l->addLayout(form);
    l->addWidget(align);
    l->addWidget(buttons);
}

Primitives::Params PrimitiveDialog::params() const
{
    Primitives::Params p;
    if (m_sizeX) p.sizeX = float(m_sizeX->value());
    if (m_sizeY) p.sizeY = float(m_sizeY->value());
    if (m_sizeZ) p.sizeZ = float(m_sizeZ->value());
    if (m_radius) p.radius = float(m_radius->value());
    if (m_segX) p.segX = m_segX->value();
    if (m_segY) p.segY = m_segY->value();
    if (m_segZ) p.segZ = m_segZ->value();
    if (m_segR) p.segRadius = m_segR->value();
    if (m_center) p.centerPoint = m_center->isChecked();
    p.align = Primitives::Align(std::max(0, radioValue(m_align)));
    return p;
}

// ================================================================ Resolution

ResolutionDialog::ResolutionDialog(float initial, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Select LOD"));
    m_list = new QListWidget(this);
    for (const auto &s : LodNames::specialLods()) {
        auto *it = new QListWidgetItem(s.first, m_list);
        it->setData(Qt::UserRole, double(s.second));
    }
    m_edit = new QLineEdit(LodNames::name(initial), this);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        if (it)
            m_edit->setText(it->text());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Resolution")));
    row->addWidget(m_edit, 1);
    auto *ok = new QPushButton(tr("OK"), this);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] {
        float r;
        if (resolution(r))
            accept();
        else
            m_edit->setFocus();
    });
    row->addWidget(ok);
    auto *l = new QVBoxLayout(this);
    l->addWidget(m_list);
    l->addLayout(row);
    resize(300, 420);
}

bool ResolutionDialog::resolution(float &out) const
{
    return LodNames::parse(m_edit->text(), out);
}

// ================================================================ Options

namespace {
// The original's CConfig entries (ObjectBuilder_defaultSettings.reg). The first
// character is the value type: '#' bool, '0' number, '$' text, '\\' folder,
// '%' file, 'C' colour.
const char *kConfig[] = {
    "\\DayZ folder",
    "\\Work drive (P:)",
    "#Start in four views",
    "CBackgroun Color",
    "0Translate step (/100)",
    "0Rotate step",
    "0Scale step",
    "\\User Script Path",
    "\\Last SaveAs Folder",
    "\\Last Merge Folder",
    "#View normals",
    "#Smart Normals",
    "#Disable R/O warning",
    "0Save object each min:",
    "$Backup filename",
};
} // namespace

OptionsDialog::OptionsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Options"));
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Option"), tr("Value")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    QSettings s;
    for (const char *raw : kConfig) {
        const QString key = QString::fromLatin1(raw);
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, key.mid(1));
        it->setData(0, Qt::UserRole, key);
        const QVariant v = s.value(QStringLiteral("CConfig/") + key);
        if (key[0] == QLatin1Char('#')) {
            it->setCheckState(1, v.toBool() ? Qt::Checked : Qt::Unchecked);
        } else if (key[0] == QLatin1Char('C')) {
            const QColor c = v.isValid() ? v.value<QColor>() : QColor(192, 192, 192);
            it->setText(1, c.name());
            it->setBackground(1, c);
        } else {
            it->setText(1, v.toString());
            it->setFlags(it->flags() | Qt::ItemIsEditable);
        }
    }
    // Double click opens the specialised editor, as the original suggested.
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        const QString key = it->data(0, Qt::UserRole).toString();
        if (key[0] == QLatin1Char('C')) {
            const QColor c = QColorDialog::getColor(QColor(it->text(1)), this);
            if (c.isValid()) {
                it->setText(1, c.name());
                it->setBackground(1, c);
            }
        } else if (key[0] == QLatin1Char('\\')) {
            const QString d = QFileDialog::getExistingDirectory(this, it->text(0), it->text(1));
            if (!d.isEmpty())
                it->setText(1, QDir::toNativeSeparators(d));
        } else if (key[0] != QLatin1Char('#')) {
            m_tree->editItem(it, 1);
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *l = new QVBoxLayout(this);
    l->addWidget(m_tree);
    l->addWidget(buttons);
    resize(520, 380);
}

void OptionsDialog::accept()
{
    QSettings s;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        const QString key = it->data(0, Qt::UserRole).toString();
        const QString path = QStringLiteral("CConfig/") + key;
        if (key[0] == QLatin1Char('#'))
            s.setValue(path, it->checkState(1) == Qt::Checked);
        else if (key[0] == QLatin1Char('C'))
            s.setValue(path, QColor(it->text(1)));
        else
            s.setValue(path, it->text(1));
    }
    QDialog::accept();
}

// ================================================================ About

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("About Object Builder"));
    auto *text = new QLabel(this);
    text->setTextFormat(Qt::RichText);
    text->setText(tr("<h3>Object Builder (Qt reconstruction)</h3>"
                     "<p>A Qt re-implementation of Bohemia Interactive's Object Builder, "
                     "rebuilt from the shipped ObjectBuilder.exe with the REA reverse-engineering toolkit.</p>"
                     "<p>Edits MLOD (P3DM) models: LODs, named selections, named properties, mass, "
                     "UV sets and face/point flags.</p>"
                     "<p>Built with Qt %1.</p>")
                      .arg(QString::fromLatin1(qVersion())));
    text->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    auto *l = new QVBoxLayout(this);
    l->addWidget(text);
    l->addWidget(buttons);
    resize(420, 220);
}
