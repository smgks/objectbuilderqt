#include "Paa.h"

#include "Compression.h"

#include <QRegularExpression>

#include <algorithm>
#include <cstring>

namespace {

enum class Fmt { DXT1, DXT3, DXT5, ARGB4444, ARGB1555, ARGB8888, AI88, Unknown };

Fmt formatOf(quint16 tag)
{
    switch (tag) {
    case 0xFF01: return Fmt::DXT1;
    case 0xFF02:
    case 0xFF03: return Fmt::DXT3;
    case 0xFF04:
    case 0xFF05: return Fmt::DXT5;
    case 0x4444: return Fmt::ARGB4444;
    case 0x1555: return Fmt::ARGB1555;
    case 0x8888: return Fmt::ARGB8888;
    case 0x8080: return Fmt::AI88;
    default: return Fmt::Unknown;
    }
}

bool isDxt(Fmt f)
{
    return f == Fmt::DXT1 || f == Fmt::DXT3 || f == Fmt::DXT5;
}

qsizetype dataSize(Fmt f, int w, int h)
{
    switch (f) {
    case Fmt::DXT1: return qsizetype(std::max(1, (w + 3) / 4)) * std::max(1, (h + 3) / 4) * 8;
    case Fmt::DXT3:
    case Fmt::DXT5: return qsizetype(std::max(1, (w + 3) / 4)) * std::max(1, (h + 3) / 4) * 16;
    case Fmt::ARGB8888: return qsizetype(w) * h * 4;
    default: return qsizetype(w) * h * 2;
    }
}

QRgb rgb565(quint16 c)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return qRgb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

void decodeColorBlock(const quint8 *p, QRgb out[16], bool dxt1)
{
    const quint16 c0 = quint16(p[0] | (p[1] << 8)), c1 = quint16(p[2] | (p[3] << 8));
    QRgb pal[4];
    pal[0] = rgb565(c0);
    pal[1] = rgb565(c1);
    auto mix = [](QRgb a, QRgb b, int wa, int wb, int d) {
        return qRgba((qRed(a) * wa + qRed(b) * wb) / d, (qGreen(a) * wa + qGreen(b) * wb) / d, (qBlue(a) * wa + qBlue(b) * wb) / d, 255);
    };
    if (!dxt1 || c0 > c1) {
        pal[2] = mix(pal[0], pal[1], 2, 1, 3);
        pal[3] = mix(pal[0], pal[1], 1, 2, 3);
    } else {
        pal[2] = mix(pal[0], pal[1], 1, 1, 2);
        pal[3] = qRgba(0, 0, 0, 0);
    }
    const quint32 bits = quint32(p[4]) | (quint32(p[5]) << 8) | (quint32(p[6]) << 16) | (quint32(p[7]) << 24);
    for (int i = 0; i < 16; ++i)
        out[i] = pal[(bits >> (2 * i)) & 3];
}

QImage decodeDxt(const quint8 *d, int w, int h, Fmt f)
{
    QImage img(w, h, QImage::Format_ARGB32);
    const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    const int blockSize = f == Fmt::DXT1 ? 8 : 16;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const quint8 *b = d + (qsizetype(by) * bw + bx) * blockSize;
            QRgb px[16];
            int alpha[16];
            if (f == Fmt::DXT1) {
                decodeColorBlock(b, px, true);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = qAlpha(px[i]);
            } else if (f == Fmt::DXT3) {
                for (int i = 0; i < 16; ++i) {
                    const int a = (b[i / 2] >> ((i & 1) * 4)) & 15;
                    alpha[i] = a * 17;
                }
                decodeColorBlock(b + 8, px, false);
            } else {
                const int a0 = b[0], a1 = b[1];
                int pal[8] = {a0, a1};
                if (a0 > a1)
                    for (int i = 1; i < 7; ++i)
                        pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                else {
                    for (int i = 1; i < 5; ++i)
                        pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
                    pal[6] = 0;
                    pal[7] = 255;
                }
                quint64 bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= quint64(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = pal[(bits >> (3 * i)) & 7];
                decodeColorBlock(b + 8, px, false);
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h)
                    img.setPixel(x, y, qRgba(qRed(px[i]), qGreen(px[i]), qBlue(px[i]), alpha[i]));
            }
        }
    return img;
}

QImage decodeRaw(const quint8 *d, int w, int h, Fmt f)
{
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const qsizetype i = qsizetype(y) * w + x;
            switch (f) {
            case Fmt::ARGB8888: {
                const quint8 *p = d + i * 4;
                line[x] = qRgba(p[2], p[1], p[0], p[3]);
                break;
            }
            case Fmt::ARGB4444: {
                const quint16 c = quint16(d[i * 2] | (d[i * 2 + 1] << 8));
                line[x] = qRgba(((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17, ((c >> 12) & 15) * 17);
                break;
            }
            case Fmt::ARGB1555: {
                const quint16 c = quint16(d[i * 2] | (d[i * 2 + 1] << 8));
                auto x5 = [](int v) { return (v << 3) | (v >> 2); };
                line[x] = qRgba(x5((c >> 10) & 31), x5((c >> 5) & 31), x5(c & 31), (c & 0x8000) ? 255 : 0);
                break;
            }
            case Fmt::AI88: {
                const int g = d[i * 2], a = d[i * 2 + 1];
                line[x] = qRgba(g, g, g, a);
                break;
            }
            default: line[x] = qRgb(255, 0, 255);
            }
        }
    }
    return img;
}

} // namespace

QImage Paa::decode(const QByteArray &data, int maxSize, QString *error)
{
    const auto *b = reinterpret_cast<const quint8 *>(data.constData());
    const qsizetype n = data.size();
    qsizetype p = 0;
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return QImage();
    };
    if (n < 4)
        return fail(QStringLiteral("PAA too short"));
    const Fmt f = formatOf(quint16(b[0] | (b[1] << 8)));
    if (f == Fmt::Unknown)
        return fail(QStringLiteral("Unknown PAA format %1").arg(quint16(b[0] | (b[1] << 8)), 4, 16, QLatin1Char('0')));
    p = 2;
    // TAGGs
    while (p + 12 <= n && std::memcmp(b + p, "GGAT", 4) == 0) {
        const quint32 len = quint32(b[p + 8]) | (quint32(b[p + 9]) << 8) | (quint32(b[p + 10]) << 16) | (quint32(b[p + 11]) << 24);
        p += 12 + len;
    }
    if (p + 2 > n)
        return fail(QStringLiteral("Truncated PAA"));
    const int palette = b[p] | (b[p + 1] << 8);
    p += 2 + palette * 3;

    struct Mip {
        int w, h;
        bool lzo;
        qsizetype off, size;
    };
    QVector<Mip> mips;
    while (p + 7 <= n) {
        int w = b[p] | (b[p + 1] << 8);
        const int h = b[p + 2] | (b[p + 3] << 8);
        if (w == 0 || h == 0)
            break;
        const qsizetype size = qsizetype(b[p + 4]) | (qsizetype(b[p + 5]) << 8) | (qsizetype(b[p + 6]) << 16);
        p += 7;
        const bool lzo = isDxt(f) && (w & 0x8000);
        w &= 0x7FFF;
        if (p + size > n)
            break;
        mips.append({w, h, lzo, p, size});
        p += size;
    }
    if (mips.isEmpty())
        return fail(QStringLiteral("PAA has no mipmaps"));
    int pick = int(mips.size()) - 1;
    for (int i = 0; i < mips.size(); ++i)
        if (mips[i].w <= maxSize && mips[i].h <= maxSize) {
            pick = i;
            break;
        }
    const Mip &m = mips[pick];
    const qsizetype expected = dataSize(f, m.w, m.h);
    QByteArray raw;
    const char *src = data.constData() + m.off;
    if (m.lzo) {
        if (!Compression::lzo1x(src, m.size, expected, raw))
            return fail(QStringLiteral("LZO error in PAA mipmap"));
    } else if (m.size < expected) {
        if (!Compression::lzss(src, m.size, expected, raw, nullptr, true) && !Compression::lzss(src, m.size, expected, raw, nullptr, false))
            return fail(QStringLiteral("LZSS error in PAA mipmap"));
    } else {
        raw = QByteArray(src, expected);
    }
    const auto *d = reinterpret_cast<const quint8 *>(raw.constData());
    return isDxt(f) ? decodeDxt(d, m.w, m.h, f) : decodeRaw(d, m.w, m.h, f);
}

QImage Paa::procedural(const QString &spec)
{
    static const QRegularExpression re(QStringLiteral(R"(^#\(\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\)\s*color\(([^)]*)\))"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(spec.trimmed());
    if (!m.hasMatch())
        return {};
    const QStringList v = m.captured(4).split(QLatin1Char(','));
    auto ch = [&](int i, double def) { return i < v.size() ? std::clamp(v[i].trimmed().toDouble(), 0.0, 1.0) : def; };
    const int w = std::clamp(m.captured(2).toInt(), 1, 64), h = std::clamp(m.captured(3).toInt(), 1, 64);
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor::fromRgbF(float(ch(0, 1)), float(ch(1, 1)), float(ch(2, 1)), float(ch(3, 1))));
    return img;
}
