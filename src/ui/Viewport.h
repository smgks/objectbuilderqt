#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QHash>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include <memory>

class Document;
class ObjectData;

enum class ViewType { Front, Back, Left, Right, Top, Bottom, Projected };
enum class EditMode { Vertices, Faces, Object, Pin };

// Display/edit state shared by all viewports (View menu toggles, edit mode).
struct ViewSettings {
    QColor background{192, 192, 192}; // CConfig "Backgroun Color", default 00C0C0C0
    bool solid = false;
    bool textured = true;
    bool culling = false;
    bool grid = true;
    int textureGeneration = 0; // bump to make viewports drop their GL textures
    EditMode mode = EditMode::Vertices;
};

QString viewTypeName(ViewType t);

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    Viewport(Document *doc, ViewSettings *settings, ViewType type, QWidget *parent = nullptr);
    ~Viewport() override;

    ViewType viewType() const { return m_type; }
    void setViewType(ViewType t);

    void zoomBy(float factor);
    void zoomToBox(const QVector3D &mn, const QVector3D &mx);
    void zoomToDrawing();
    void zoomToSelection();
    void lookAt(const QVector3D &p);
    void setActiveFrame(bool active);

    // Camera basis in model space (left-handed: X right, Y up, Z forward).
    void basis(QVector3D &right, QVector3D &up, QVector3D &forward) const;

signals:
    void activated(Viewport *self);
    void cursorPosition(const QVector3D &modelPos);
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    enum class Drag { None, Select, Orbit, Pan, Move, Rotate, Scale };

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    QMatrix4x4 mvp() const { return projMatrix() * viewMatrix(); }
    QPointF project(const QVector3D &p, float *depth = nullptr) const;
    QVector3D unprojectOnPlane(const QPointF &screen, const QVector3D &planePoint) const;
    float worldPerPixel() const;

    void rebuildGeometry();
    void drawGrid(const QMatrix4x4 &m);
    void drawArrays(QOpenGLShaderProgram &prog, GLenum mode, const QVector<float> &data, int stride, bool lit, bool uv = false);
    QOpenGLTexture *glTexture(const QString &path);

    void finishSelection(const QPoint &a, const QPoint &b, Qt::KeyboardModifiers mods);
    int pickPoint(const QPoint &pos) const;
    int pickFace(const QPoint &pos) const;
    void applyFreehand(const QPoint &pos);

    Document *m_doc;
    ViewSettings *m_settings;
    ViewType m_type;
    bool m_activeFrame = false;

    // camera
    QVector3D m_target;
    float m_scale = 5.0f; // half of the visible height in metres (ortho) / distance (projected)
    float m_yaw = 30.0f, m_pitch = 20.0f;

    // interaction
    Drag m_drag = Drag::None;
    QPoint m_pressPos, m_lastPos;
    Qt::KeyboardModifiers m_pressMods;
    QVector<int> m_dragPoints;
    QVector<QVector3D> m_dragOrig;

    // GL
    std::unique_ptr<QOpenGLShaderProgram> m_flat, m_lit, m_texProg;
    QHash<QString, QOpenGLTexture *> m_glTex;
    QHash<QString, QVector<float>> m_texTris; // per texture: pos3 normal3 color4 uv2
    bool m_dirty = true;
    int m_texGeneration = 0;
    QVector<float> m_tris;   // pos3 normal3 color4
    QVector<float> m_lines;  // pos3 color4
    QVector<float> m_points; // pos3 color4
    QVector<float> m_selPoints;
};
