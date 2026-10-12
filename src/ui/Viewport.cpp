#include "Viewport.h"

#include "app/Document.h"
#include "core/TextureLibrary.h"

#include <QApplication>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>
#include <limits>

namespace {

const char *kFlatVs = R"(
attribute vec3 aPos;
attribute vec4 aColor;
uniform mat4 uMvp;
uniform float uPointSize;
varying vec4 vColor;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
    vColor = aColor;
})";

const char *kFlatFs = R"(
varying vec4 vColor;
void main() { gl_FragColor = vColor; })";

const char *kLitVs = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec4 aColor;
uniform mat4 uMvp;
uniform vec3 uLight;
uniform float uTwoSided;
varying vec4 vColor;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    float d = dot(normalize(aNormal), uLight);
    d = mix(max(d, 0.0), abs(d), uTwoSided);
    vColor = vec4(aColor.rgb * (0.35 + 0.65 * d), aColor.a);
})";

const char *kTexVs = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aUV;
uniform mat4 uMvp;
uniform vec3 uLight;
uniform float uTwoSided;
varying vec4 vColor;
varying vec2 vUV;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    float d = dot(normalize(aNormal), uLight);
    d = mix(max(d, 0.0), abs(d), uTwoSided);
    vColor = vec4(aColor.rgb * (0.45 + 0.75 * d), aColor.a);
    vUV = aUV;
})";

const char *kTexFs = R"(
uniform sampler2D uTex;
uniform float uBlend; // 0: alpha-tested cut-out, 1: alpha-blended (translucent)
varying vec4 vColor;
varying vec2 vUV;
void main() {
    vec4 t = texture2D(uTex, vUV);
    if (t.a < mix(0.5, 0.01, uBlend)) discard;
    gl_FragColor = vec4(t.rgb * vColor.rgb, mix(1.0, t.a, uBlend));
})";

constexpr float kFov = 45.0f;

const QColor kWire(20, 20, 20);
const QColor kSelected(230, 0, 0);
const QColor kLocked(30, 30, 160);
const QColor kSolid(205, 205, 205);
const QColor kSolidSelected(235, 110, 110);
const QColor kGrid(150, 150, 150);
const QColor kPin(0, 140, 0);

void push(QVector<float> &v, const QVector3D &p)
{
    v << p.x() << p.y() << p.z();
}
void push(QVector<float> &v, const QColor &c)
{
    v << float(c.redF()) << float(c.greenF()) << float(c.blueF()) << float(c.alphaF());
}

QColor weightColor(quint8 w)
{
    const float t = w / 255.0f;
    return QColor::fromRgbF(kWire.redF() * (1 - t) + kSelected.redF() * t, kWire.greenF() * (1 - t) + kSelected.greenF() * t,
                            kWire.blueF() * (1 - t) + kSelected.blueF() * t);
}

bool pointInPolygon(const QPointF &p, const QPointF *poly, int n)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y() > p.y()) != (poly[j].y() > p.y()))
            && (p.x() < (poly[j].x() - poly[i].x()) * (p.y() - poly[i].y()) / (poly[j].y() - poly[i].y()) + poly[i].x()))
            in = !in;
    }
    return in;
}

} // namespace

QString viewTypeName(ViewType t)
{
    switch (t) {
    case ViewType::Front: return QObject::tr("Front");
    case ViewType::Back: return QObject::tr("Back");
    case ViewType::Left: return QObject::tr("Left");
    case ViewType::Right: return QObject::tr("Right");
    case ViewType::Top: return QObject::tr("Top");
    case ViewType::Bottom: return QObject::tr("Bottom");
    case ViewType::Projected: return QObject::tr("Projected");
    }
    return {};
}

Viewport::Viewport(Document *doc, ViewSettings *settings, ViewType type, QWidget *parent)
    : QOpenGLWidget(parent), m_doc(doc), m_settings(settings), m_type(type)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(100, 80);
    connect(doc, &Document::changed, this, [this] {
        m_dirty = true;
        update();
    });
    connect(doc, &Document::pinChanged, this, qOverload<>(&QWidget::update));
}

Viewport::~Viewport()
{
    makeCurrent();
    qDeleteAll(m_glTex);
    m_glTex.clear();
    m_flat.reset();
    m_texProg.reset();
    m_lit.reset();
    doneCurrent();
}

void Viewport::setViewType(ViewType t)
{
    m_type = t;
    update();
}

void Viewport::setActiveFrame(bool active)
{
    m_activeFrame = active;
    update();
}

// ---------------------------------------------------------------- camera

void Viewport::basis(QVector3D &r, QVector3D &u, QVector3D &f) const
{
    switch (m_type) {
    case ViewType::Front: f = {0, 0, 1}; r = {1, 0, 0}; break;
    case ViewType::Back: f = {0, 0, -1}; r = {-1, 0, 0}; break;
    case ViewType::Left: f = {1, 0, 0}; r = {0, 0, -1}; break;
    case ViewType::Right: f = {-1, 0, 0}; r = {0, 0, 1}; break;
    case ViewType::Top: f = {0, -1, 0}; r = {1, 0, 0}; break;
    case ViewType::Bottom: f = {0, 1, 0}; r = {1, 0, 0}; break;
    case ViewType::Projected: {
        const float y = qDegreesToRadians(m_yaw), p = qDegreesToRadians(m_pitch);
        f = QVector3D(std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
        r = QVector3D(std::cos(y), 0, -std::sin(y));
        break;
    }
    }
    // Model space is left-handed, so "up" is f x r rather than r x f.
    u = QVector3D::crossProduct(f, r);
}

QMatrix4x4 Viewport::viewMatrix() const
{
    QVector3D r, u, f;
    basis(r, u, f);
    const QVector3D eye = m_type == ViewType::Projected ? m_target - f * m_scale : m_target - f * 1000.0f;
    // Rows r, u, -f: eye space looks down -Z as OpenGL expects. The matrix has
    // determinant -1, which converts the left-handed model space correctly.
    QMatrix4x4 m(r.x(), r.y(), r.z(), -QVector3D::dotProduct(r, eye),
                 u.x(), u.y(), u.z(), -QVector3D::dotProduct(u, eye),
                 -f.x(), -f.y(), -f.z(), QVector3D::dotProduct(f, eye),
                 0, 0, 0, 1);
    return m;
}

QMatrix4x4 Viewport::projMatrix() const
{
    QMatrix4x4 p;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    if (m_type == ViewType::Projected)
        p.perspective(kFov, aspect, std::max(0.01f, m_scale * 0.005f), m_scale * 50.0f + 1000.0f);
    else
        p.ortho(-m_scale * aspect, m_scale * aspect, -m_scale, m_scale, 1.0f, 3000.0f);
    return p;
}

float Viewport::worldPerPixel() const
{
    const float h = std::max(1, height());
    if (m_type == ViewType::Projected)
        return 2.0f * m_scale * std::tan(qDegreesToRadians(kFov) * 0.5f) / h;
    return 2.0f * m_scale / h;
}

QPointF Viewport::project(const QVector3D &p, float *depth) const
{
    const QVector4D c = mvp() * QVector4D(p, 1.0f);
    const float w = std::abs(c.w()) < 1e-9f ? 1e-9f : c.w();
    if (depth)
        *depth = c.w() <= 0 ? std::numeric_limits<float>::max() : c.z() / w;
    return QPointF((c.x() / w + 1.0f) * 0.5f * width(), (1.0f - c.y() / w) * 0.5f * height());
}

QVector3D Viewport::unprojectOnPlane(const QPointF &s, const QVector3D &planePoint) const
{
    QVector3D r, u, f;
    basis(r, u, f);
    const float wpp = worldPerPixel();
    const float dx = float(s.x() - width() * 0.5) * wpp;
    const float dy = float(height() * 0.5 - s.y()) * wpp;
    QVector3D origin, dir;
    if (m_type == ViewType::Projected) {
        origin = m_target - f * m_scale;
        // at the target distance one pixel is wpp, so scale the offset by 1/distance
        dir = (f + (r * dx + u * dy) / m_scale).normalized();
    } else {
        origin = m_target + r * dx + u * dy - f * 1000.0f;
        dir = f;
    }
    const float denom = QVector3D::dotProduct(dir, f);
    const float t = QVector3D::dotProduct(planePoint - origin, f) / (std::abs(denom) < 1e-6f ? 1e-6f : denom);
    return origin + dir * t;
}

void Viewport::zoomBy(float factor)
{
    m_scale = std::clamp(m_scale * factor, 0.001f, 100000.0f);
    update();
}

void Viewport::zoomToBox(const QVector3D &mn, const QVector3D &mx)
{
    m_target = (mn + mx) * 0.5f;
    const float radius = std::max(0.05f, (mx - mn).length() * 0.5f);
    m_scale = m_type == ViewType::Projected ? radius / std::tan(qDegreesToRadians(kFov) * 0.5f) * 1.1f : radius * 1.1f;
    update();
}

void Viewport::zoomToDrawing()
{
    // Ignore proxy-marker / special hidden vertices (flag 0x1000000) so a
    // model is framed by its real geometry rather than its proxy triangles.
    const ObjectData &o = m_doc->active();
    QVector3D mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    bool any = false;
    for (const Point &p : o.points) {
        if (p.flags & 0x01000000)
            continue;
        mn = QVector3D(std::min(mn.x(), p.pos.x()), std::min(mn.y(), p.pos.y()), std::min(mn.z(), p.pos.z()));
        mx = QVector3D(std::max(mx.x(), p.pos.x()), std::max(mx.y(), p.pos.y()), std::max(mx.z(), p.pos.z()));
        any = true;
    }
    if (!any)
        o.bounds(mn, mx);
    zoomToBox(mn, mx);
}

void Viewport::zoomToSelection()
{
    QVector3D mn, mx;
    m_doc->active().bounds(mn, mx, true);
    if (mn == mx && m_doc->active().selection.pointCount() == 0)
        return zoomToDrawing();
    zoomToBox(mn, mx);
}

void Viewport::lookAt(const QVector3D &p)
{
    m_target = p;
    update();
}

// ---------------------------------------------------------------- GL

void Viewport::initializeGL()
{
    initializeOpenGLFunctions();
    m_flat = std::make_unique<QOpenGLShaderProgram>();
    m_flat->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVs);
    m_flat->addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFs);
    m_flat->bindAttributeLocation("aPos", 0);
    m_flat->bindAttributeLocation("aColor", 1);
    m_flat->link();

    m_lit = std::make_unique<QOpenGLShaderProgram>();
    m_lit->addShaderFromSourceCode(QOpenGLShader::Vertex, kLitVs);
    m_lit->addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFs);
    m_lit->bindAttributeLocation("aPos", 0);
    m_lit->bindAttributeLocation("aColor", 1);
    m_lit->bindAttributeLocation("aNormal", 2);
    m_lit->link();

    m_texProg = std::make_unique<QOpenGLShaderProgram>();
    m_texProg->addShaderFromSourceCode(QOpenGLShader::Vertex, kTexVs);
    m_texProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kTexFs);
    m_texProg->bindAttributeLocation("aPos", 0);
    m_texProg->bindAttributeLocation("aColor", 1);
    m_texProg->bindAttributeLocation("aNormal", 2);
    m_texProg->bindAttributeLocation("aUV", 3);
    m_texProg->link();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        makeCurrent();
        qDeleteAll(m_glTex);
        m_glTex.clear();
        doneCurrent();
    });
    m_dirty = true;
}

QOpenGLTexture *Viewport::glTexture(const QString &path)
{
    if (m_texGeneration != m_settings->textureGeneration) {
        qDeleteAll(m_glTex);
        m_glTex.clear();
        m_translucent.clear();
        m_texGeneration = m_settings->textureGeneration;
    }
    if (QOpenGLTexture *t = m_glTex.value(path))
        return t;
    TextureLibrary &lib = TextureLibrary::instance();
    if (!lib.isCached(path)) {
        // Decode off the GUI thread; repaint when ready.
        static QSet<QString> pending;
        if (!pending.contains(path)) {
            pending.insert(path);
            QPointer<Viewport> self(this);
            QThreadPool::globalInstance()->start([path, self] {
                TextureLibrary::instance().image(path);
                QMetaObject::invokeMethod(qApp, [path, self] {
                    pending.remove(path);
                    if (self)
                        self->update();
                    for (QWidget *w : QApplication::allWidgets())
                        if (auto *v = qobject_cast<Viewport *>(w))
                            v->update();
                });
            });
        }
        return nullptr;
    }
    const QImage img = lib.image(path);
    if (img.isNull()) {
        m_glTex.insert(path, nullptr);
        return nullptr;
    }
    auto *t = new QOpenGLTexture(QOpenGLTexture::Target2D);
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    // Translucent (e.g. "_ca" glass, clear plastic): enough texels that are
    // neither fully opaque nor fully clear. Those are blended like the game
    // does; textures with only on/off alpha stay alpha-tested cut-outs.
    {
        qint64 partial = 0, sampled = 0;
        const int step = std::max(1, rgba.width() / 128);
        for (int y = 0; y < rgba.height(); y += step) {
            const uchar *row = rgba.constScanLine(y);
            for (int x = 0; x < rgba.width(); x += step, ++sampled) {
                const uchar a = row[4 * x + 3];
                partial += a > 16 && a < 240;
            }
        }
        if (sampled && partial * 20 > sampled) // > 5 %
            m_translucent.insert(path);
    }
    // Uploaded top row first: texture v = 0 is the top of the image, the
    // DirectX convention used by P3D UVs.
    t->setData(rgba, QOpenGLTexture::GenerateMipMaps);
    t->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    t->setWrapMode(QOpenGLTexture::Repeat);
    m_glTex.insert(path, t);
    return t;
}

void Viewport::resizeGL(int, int) {}

void Viewport::rebuildGeometry()
{
    m_tris.clear();
    m_texTris.clear();
    m_lines.clear();
    m_points.clear();
    m_selPoints.clear();
    const ObjectData &o = m_doc->active();
    const bool faceMode = m_settings->mode == EditMode::Faces;
    const UVSet &uv = o.uvSet0();

    for (int f = 0; f < o.faces.size(); ++f) {
        if (!o.faceVisible(f))
            continue;
        const Face &fc = o.faces[f];
        const bool sel = o.selection.face(f);
        const bool locked = o.locked.face(f);
        if (m_settings->solid) {
            const QVector3D n = o.faceNormal(f);
            const bool textured = m_settings->textured && !fc.texture.isEmpty();
            const QColor c = textured ? (sel ? QColor(255, 140, 140) : QColor(255, 255, 255)) : (sel ? kSolidSelected : kSolid);
            QVector<float> &dst = textured ? m_texTris[fc.texture] : m_tris;
            auto tri = [&](int a, int b, int c2) {
                for (int k : {a, b, c2}) {
                    push(dst, o.points[fc.v[k].point].pos);
                    push(dst, n);
                    push(dst, c);
                    if (textured)
                        dst << uv.uv[f][k].x() << uv.uv[f][k].y();
                }
            };
            tri(0, 1, 2);
            if (fc.n == 4)
                tri(0, 2, 3);
        }
        for (int k = 0; k < fc.n; ++k) {
            const int a = fc.v[k].point, b = fc.v[(k + 1) % fc.n].point;
            QColor c = m_settings->solid ? QColor(0, 0, 0, 40) : kWire;
            if (locked)
                c = kLocked;
            else if (faceMode ? sel : (o.selection.point(a) && o.selection.point(b)))
                c = faceMode ? kSelected : weightColor(std::min(o.selection.weight(a), o.selection.weight(b)));
            push(m_lines, o.points[a].pos);
            push(m_lines, c);
            push(m_lines, o.points[b].pos);
            push(m_lines, c);
        }
    }
    for (int i = 0; i < o.points.size(); ++i) {
        if (!o.pointVisible(i))
            continue;
        if (o.selection.point(i)) {
            push(m_selPoints, o.points[i].pos);
            push(m_selPoints, weightColor(o.selection.weight(i)));
        } else {
            push(m_points, o.points[i].pos);
            push(m_points, o.locked.point(i) ? kLocked : kWire);
        }
    }
    m_dirty = false;
}

void Viewport::drawArrays(QOpenGLShaderProgram &prog, GLenum mode, const QVector<float> &data, int stride, bool lit, bool uv)
{
    if (data.isEmpty())
        return;
    QOpenGLBuffer vbo(QOpenGLBuffer::VertexBuffer);
    vbo.create();
    vbo.bind();
    vbo.allocate(data.constData(), int(data.size() * sizeof(float)));
    const int bytes = stride * int(sizeof(float));
    prog.enableAttributeArray(0);
    prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, bytes);
    if (lit) {
        prog.enableAttributeArray(2);
        prog.setAttributeBuffer(2, GL_FLOAT, 3 * sizeof(float), 3, bytes);
        prog.enableAttributeArray(1);
        prog.setAttributeBuffer(1, GL_FLOAT, 6 * sizeof(float), 4, bytes);
        if (uv) {
            prog.enableAttributeArray(3);
            prog.setAttributeBuffer(3, GL_FLOAT, 10 * sizeof(float), 2, bytes);
        }
    } else {
        prog.enableAttributeArray(1);
        prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 4, bytes);
    }
    glDrawArrays(mode, 0, GLsizei(data.size() / stride));
    prog.disableAttributeArray(0);
    prog.disableAttributeArray(1);
    if (lit)
        prog.disableAttributeArray(2);
    if (uv)
        prog.disableAttributeArray(3);
    vbo.release();
    vbo.destroy();
}

void Viewport::drawGrid(const QMatrix4x4 &m)
{
    QVector3D r, u, f;
    basis(r, u, f);
    QVector<float> v;
    const float step = std::pow(10.0f, std::floor(std::log10(std::max(m_scale, 0.01f) * 0.5f)));
    const int n = 40;
    // Grid plane: the principal plane most facing the camera, through origin.
    QVector3D a1, a2;
    const QVector3D af(std::abs(f.x()), std::abs(f.y()), std::abs(f.z()));
    if (af.y() >= af.x() && af.y() >= af.z()) {
        a1 = {1, 0, 0};
        a2 = {0, 0, 1};
    } else if (af.x() >= af.z()) {
        a1 = {0, 0, 1};
        a2 = {0, 1, 0};
    } else {
        a1 = {1, 0, 0};
        a2 = {0, 1, 0};
    }
    const QVector3D c(std::round(QVector3D::dotProduct(m_target, a1) / step) * step * a1
                      + std::round(QVector3D::dotProduct(m_target, a2) / step) * step * a2);
    for (int i = -n; i <= n; ++i) {
        const QColor col = (i == 0) ? kGrid.darker(115) : kGrid;
        push(v, c + a1 * (i * step) - a2 * (n * step));
        push(v, col);
        push(v, c + a1 * (i * step) + a2 * (n * step));
        push(v, col);
        push(v, c + a2 * (i * step) - a1 * (n * step));
        push(v, col);
        push(v, c + a2 * (i * step) + a1 * (n * step));
        push(v, col);
    }
    // axes
    const float L = step * 2;
    const QColor xs(200, 0, 0), ys(0, 160, 0), zs(0, 0, 200);
    push(v, QVector3D());
    push(v, xs);
    push(v, QVector3D(L, 0, 0));
    push(v, xs);
    push(v, QVector3D());
    push(v, ys);
    push(v, QVector3D(0, L, 0));
    push(v, ys);
    push(v, QVector3D());
    push(v, zs);
    push(v, QVector3D(0, 0, L));
    push(v, zs);
    m_flat->bind();
    m_flat->setUniformValue("uMvp", m);
    drawArrays(*m_flat, GL_LINES, v, 7, false);
}

void Viewport::paintGL()
{
    if (m_dirty)
        rebuildGeometry();

    const QColor bg = m_settings->background;
    glClearColor(float(bg.redF()), float(bg.greenF()), float(bg.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(0x8642); // GL_PROGRAM_POINT_SIZE (desktop GL)

    const QMatrix4x4 m = mvp();
    QVector3D r, u, f;
    basis(r, u, f);

    if (m_settings->grid) {
        glDepthMask(GL_FALSE);
        drawGrid(m);
        glDepthMask(GL_TRUE);
    }

    if (m_settings->culling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    if (m_settings->solid && (!m_tris.isEmpty() || !m_texTris.isEmpty())) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        const QVector3D light = (-f + u * 0.3f).normalized();
        const float twoSided = m_settings->culling ? 0.0f : 1.0f;
        m_lit->bind();
        m_lit->setUniformValue("uMvp", m);
        m_lit->setUniformValue("uLight", light);
        m_lit->setUniformValue("uTwoSided", twoSided);
        drawArrays(*m_lit, GL_TRIANGLES, m_tris, 10, true);
        // Textured batches; textures still loading (or missing) draw untextured.
        // Pass 0 draws opaque and cut-out textures; pass 1 then blends the
        // translucent ones over them without writing depth, so what is behind
        // glass or clear plastic stays visible.
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            for (auto it = m_texTris.cbegin(); it != m_texTris.cend(); ++it) {
                QOpenGLTexture *tex = glTexture(it.key());
                const bool translucent = tex && m_translucent.contains(it.key());
                if (translucent != (pass == 1))
                    continue;
                if (!tex) {
                    QVector<float> plain;
                    plain.reserve(it.value().size() / 12 * 10);
                    for (qsizetype i = 0; i + 12 <= it.value().size(); i += 12) {
                        for (int k = 0; k < 6; ++k)
                            plain << it.value()[i + k];
                        plain << float(kSolid.redF()) << float(kSolid.greenF()) << float(kSolid.blueF()) << 1.0f;
                    }
                    m_lit->bind();
                    drawArrays(*m_lit, GL_TRIANGLES, plain, 10, true);
                    continue;
                }
                m_texProg->bind();
                m_texProg->setUniformValue("uMvp", m);
                m_texProg->setUniformValue("uLight", light);
                m_texProg->setUniformValue("uTwoSided", twoSided);
                m_texProg->setUniformValue("uTex", 0);
                m_texProg->setUniformValue("uBlend", translucent ? 1.0f : 0.0f);
                tex->bind(0);
                drawArrays(*m_texProg, GL_TRIANGLES, it.value(), 12, true, true);
                tex->release(0);
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glDisable(GL_CULL_FACE);

    m_flat->bind();
    m_flat->setUniformValue("uMvp", m);
    if (!m_settings->solid)
        glDisable(GL_DEPTH_TEST); // wireframe: everything visible, like the original
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawArrays(*m_flat, GL_LINES, m_lines, 7, false);
    glDisable(GL_BLEND);
    m_flat->setUniformValue("uPointSize", 2.0f);
    drawArrays(*m_flat, GL_POINTS, m_points, 7, false);
    glDisable(GL_DEPTH_TEST);
    m_flat->setUniformValue("uPointSize", 5.0f);
    drawArrays(*m_flat, GL_POINTS, m_selPoints, 7, false);

    // pin
    QVector<float> pin;
    const QVector3D p = m_doc->pin();
    const float s = worldPerPixel() * 8;
    for (const QVector3D &axis : {r, u}) {
        push(pin, p - axis * s);
        push(pin, kPin);
        push(pin, p + axis * s);
        push(pin, kPin);
    }
    drawArrays(*m_flat, GL_LINES, pin, 7, false);
    m_flat->release();

    // overlay
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::black);
    QString label = viewTypeName(m_type);
    if (m_doc->model().lods.size() > 0)
        label += QStringLiteral("  -  ") + LodNames::name(m_doc->model().activeResolution());
    painter.drawText(QPoint(6, 14), label);
    if (m_drag == Drag::Select) {
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawRect(QRect(m_pressPos, m_lastPos).normalized());
    }
    if (m_activeFrame) {
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

// ---------------------------------------------------------------- picking

int Viewport::pickPoint(const QPoint &pos) const
{
    const ObjectData &o = m_doc->active();
    int best = -1;
    double bestD = 7.0 * 7.0;
    for (int i = 0; i < o.points.size(); ++i) {
        if (!o.pointVisible(i) || o.locked.point(i))
            continue;
        const QPointF s = project(o.points[i].pos);
        const double d = std::pow(s.x() - pos.x(), 2) + std::pow(s.y() - pos.y(), 2);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int Viewport::pickFace(const QPoint &pos) const
{
    const ObjectData &o = m_doc->active();
    QVector3D r, u, f;
    basis(r, u, f);
    int best = -1;
    float bestDepth = std::numeric_limits<float>::max();
    for (int i = 0; i < o.faces.size(); ++i) {
        if (!o.faceVisible(i) || o.locked.face(i))
            continue;
        const Face &fc = o.faces[i];
        if (m_settings->culling && QVector3D::dotProduct(o.faceNormal(i), f) > 0)
            continue;
        QPointF poly[4];
        float depth = 0;
        for (int k = 0; k < fc.n; ++k) {
            float d;
            poly[k] = project(o.points[fc.v[k].point].pos, &d);
            depth += d;
        }
        depth /= fc.n;
        if (pointInPolygon(pos, poly, fc.n) && depth < bestDepth) {
            bestDepth = depth;
            best = i;
        }
    }
    return best;
}

void Viewport::finishSelection(const QPoint &a, const QPoint &b, Qt::KeyboardModifiers mods)
{
    const ObjectData &o = m_doc->active();
    const bool click = (a - b).manhattanLength() < 3;
    const QRectF rect = QRectF(a, b).normalized();
    const bool add = mods & Qt::ShiftModifier;
    const bool sub = mods & Qt::ControlModifier;
    const EditMode mode = m_settings->mode;

    QVector<int> pts, fcs;
    if (click) {
        if (mode == EditMode::Vertices) {
            if (int p = pickPoint(a); p >= 0)
                pts.append(p);
        } else {
            int f = pickFace(a);
            if (f < 0 && mode == EditMode::Object)
                if (int p = pickPoint(a); p >= 0)
                    pts.append(p);
            if (f >= 0)
                fcs.append(f);
        }
    } else {
        for (int i = 0; i < o.points.size(); ++i)
            if (o.pointVisible(i) && !o.locked.point(i) && rect.contains(project(o.points[i].pos)))
                pts.append(i);
        if (mode != EditMode::Vertices) {
            QVector<bool> inside(o.points.size(), false);
            for (int p : pts)
                inside[p] = true;
            pts.clear();
            for (int i = 0; i < o.faces.size(); ++i) {
                if (!o.faceVisible(i) || o.locked.face(i))
                    continue;
                bool all = true;
                for (int k = 0; k < o.faces[i].n && all; ++k)
                    all = inside[o.faces[i].v[k].point];
                if (all)
                    fcs.append(i);
            }
        }
    }

    if (mode == EditMode::Object && (!fcs.isEmpty() || !pts.isEmpty())) {
        // Grow to connected components.
        QVector<QVector<int>> pointFaces(o.points.size());
        for (int i = 0; i < o.faces.size(); ++i)
            for (int k = 0; k < o.faces[i].n; ++k)
                pointFaces[o.faces[i].v[k].point].append(i);
        QVector<bool> seenP(o.points.size(), false), seenF(o.faces.size(), false);
        QVector<int> stack = pts;
        for (int fc : fcs)
            for (int k = 0; k < o.faces[fc].n; ++k)
                stack.append(o.faces[fc].v[k].point);
        pts.clear();
        fcs.clear();
        while (!stack.isEmpty()) {
            const int p = stack.takeLast();
            if (seenP[p])
                continue;
            seenP[p] = true;
            pts.append(p);
            for (int fc : pointFaces[p]) {
                if (seenF[fc])
                    continue;
                seenF[fc] = true;
                fcs.append(fc);
                for (int k = 0; k < o.faces[fc].n; ++k)
                    stack.append(o.faces[fc].v[k].point);
            }
        }
    }

    m_doc->editActive(tr("Make Selection"), [&](ObjectData &d) {
        if (!add && !sub)
            d.selection.clear();
        const quint8 w = sub ? 0 : 255;
        if (mode == EditMode::Vertices) {
            for (int p : pts)
                d.selection.setPoint(p, w);
            d.selectFacesFromPoints();
        } else {
            for (int fc : fcs) {
                d.selection.setFace(fc, !sub);
                for (int k = 0; k < d.faces[fc].n; ++k)
                    d.selection.setPoint(d.faces[fc].v[k].point, w);
            }
            for (int p : pts)
                d.selection.setPoint(p, w);
            if (sub) // keep faces consistent with the remaining points
                d.selectFacesFromPoints();
        }
    }, int(Document::Change::Selection));
}

// ---------------------------------------------------------------- mouse

void Viewport::mousePressEvent(QMouseEvent *e)
{
    emit activated(this);
    m_pressPos = m_lastPos = e->pos();
    m_pressMods = e->modifiers();
    if (e->button() == Qt::LeftButton) {
        if (m_settings->mode == EditMode::Pin) {
            m_doc->setPin(unprojectOnPlane(e->position(), m_doc->pin()));
            return;
        }
        if (e->modifiers() & Qt::AltModifier) {
            m_drag = (e->modifiers() & Qt::ShiftModifier) ? Drag::Rotate
                     : (e->modifiers() & Qt::ControlModifier) ? Drag::Scale
                                                               : Drag::Move;
            const ObjectData &o = m_doc->active();
            m_dragPoints.clear();
            m_dragOrig.clear();
            for (int i = 0; i < o.points.size(); ++i)
                if (o.selection.point(i) && !o.locked.point(i)) {
                    m_dragPoints.append(i);
                    m_dragOrig.append(o.points[i].pos);
                }
            if (m_dragPoints.isEmpty())
                m_drag = Drag::None;
            else
                m_doc->beginPreview();
            return;
        }
        m_drag = Drag::Select;
    } else if (e->button() == Qt::RightButton) {
        m_drag = m_type == ViewType::Projected && !(e->modifiers() & Qt::ShiftModifier) ? Drag::Orbit : Drag::Pan;
    } else if (e->button() == Qt::MiddleButton) {
        m_drag = Drag::Pan;
    }
}

void Viewport::applyFreehand(const QPoint &pos)
{
    QVector3D r, u, f;
    basis(r, u, f);
    const QPoint d = pos - m_pressPos;
    const float wpp = worldPerPixel();
    const QVector3D pin = m_doc->pin();
    QMatrix4x4 t;
    if (m_drag == Drag::Move) {
        t.translate(r * (d.x() * wpp) - u * (d.y() * wpp));
    } else if (m_drag == Drag::Rotate) {
        t.translate(pin);
        t.rotate(d.x() * 0.5f, f);
        t.translate(-pin);
    } else {
        const float s = std::exp(d.x() * 0.01f);
        t.translate(pin);
        t.scale(s);
        t.translate(-pin);
    }
    m_doc->previewActive([&](ObjectData &o) {
        for (int i = 0; i < m_dragPoints.size(); ++i)
            o.points[m_dragPoints[i]].pos = t.map(m_dragOrig[i]);
    });
}

void Viewport::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint delta = e->pos() - m_lastPos;
    emit cursorPosition(unprojectOnPlane(e->position(), m_doc->pin()));
    switch (m_drag) {
    case Drag::Select:
        m_lastPos = e->pos();
        update();
        return;
    case Drag::Orbit:
        m_yaw += delta.x() * 0.4f;
        m_pitch = std::clamp(m_pitch + delta.y() * 0.4f, -89.0f, 89.0f);
        break;
    case Drag::Pan: {
        QVector3D r, u, f;
        basis(r, u, f);
        const float wpp = worldPerPixel();
        m_target -= r * (delta.x() * wpp) - u * (delta.y() * wpp);
        break;
    }
    case Drag::Move:
    case Drag::Rotate:
    case Drag::Scale:
        applyFreehand(e->pos());
        break;
    case Drag::None:
        return;
    }
    m_lastPos = e->pos();
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent *e)
{
    const Drag d = m_drag;
    m_drag = Drag::None;
    if (d == Drag::Select && e->button() == Qt::LeftButton) {
        finishSelection(m_pressPos, e->pos(), m_pressMods);
    } else if (d == Drag::Move || d == Drag::Rotate || d == Drag::Scale) {
        if ((e->pos() - m_pressPos).manhattanLength() == 0)
            m_doc->cancelPreview();
        else
            m_doc->commitPreview(tr("Freehand Transform."));
    } else if (e->button() == Qt::RightButton && (e->pos() - m_pressPos).manhattanLength() < 3) {
        emit contextMenuRequested(e->globalPosition().toPoint());
    }
    update();
}

void Viewport::wheelEvent(QWheelEvent *e)
{
    const float steps = e->angleDelta().y() / 120.0f;
    if (steps == 0)
        return;
    const float factor = std::pow(0.85f, steps);
    if (m_type != ViewType::Projected) {
        // zoom towards the cursor
        const QVector3D before = unprojectOnPlane(e->position(), m_target);
        m_scale = std::clamp(m_scale * factor, 0.001f, 100000.0f);
        const QVector3D after = unprojectOnPlane(e->position(), m_target);
        m_target += before - after;
        update();
    } else {
        zoomBy(factor);
    }
}

void Viewport::focusInEvent(QFocusEvent *e)
{
    QOpenGLWidget::focusInEvent(e);
    emit activated(this);
}
