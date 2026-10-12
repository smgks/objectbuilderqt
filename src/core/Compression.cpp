#include "Compression.h"

namespace Compression {

bool lzo1x(const char *inp, qsizetype inSize, qsizetype expected, QByteArray &out, qsizetype *consumed)
{
    const auto *in = reinterpret_cast<const quint8 *>(inp);
    out.resize(expected);
    auto *o = reinterpret_cast<quint8 *>(out.data());
    qsizetype ip = 0, op = 0;
    qsizetype t = 0, m = 0;

    auto need = [&](qsizetype n) { return ip + n <= inSize; };
    auto room = [&](qsizetype n) { return op + n <= expected; };
    auto copyLiterals = [&](qsizetype n) {
        if (!need(n) || !room(n))
            return false;
        for (qsizetype i = 0; i < n; ++i)
            o[op++] = in[ip++];
        return true;
    };
    auto copyMatch = [&](qsizetype from, qsizetype n) {
        if (from < 0 || !room(n))
            return false;
        for (qsizetype i = 0; i < n; ++i)
            o[op++] = o[from + i];
        return true;
    };
    auto runLength = [&](qsizetype base) -> qsizetype {
        qsizetype n = 0;
        while (need(1) && in[ip] == 0) {
            n += 255;
            ++ip;
        }
        if (!need(1))
            return -1;
        return n + base + in[ip++];
    };

    if (!need(1))
        return false;
    enum State { Loop, FirstLiteralRun, Match, MatchDone, MatchNext } st = Loop;
    if (in[0] > 17) {
        t = in[ip++] - 17;
        if (t < 4) {
            st = MatchNext;
        } else {
            if (!copyLiterals(t))
                return false;
            st = FirstLiteralRun;
        }
    }
    for (;;) {
        switch (st) {
        case Loop:
            if (!need(1))
                return false;
            t = in[ip++];
            if (t >= 16) {
                st = Match;
                break;
            }
            if (t == 0 && (t = runLength(15)) < 0)
                return false;
            if (!copyLiterals(t + 3))
                return false;
            st = FirstLiteralRun;
            break;
        case FirstLiteralRun:
            if (!need(1))
                return false;
            t = in[ip++];
            if (t >= 16) {
                st = Match;
                break;
            }
            if (!need(1))
                return false;
            m = op - (1 + 0x0800) - (t >> 2) - (qsizetype(in[ip++]) << 2);
            if (!copyMatch(m, 3))
                return false;
            st = MatchDone;
            break;
        case Match:
            if (t >= 64) {
                if (!need(1))
                    return false;
                m = op - 1 - ((t >> 2) & 7) - (qsizetype(in[ip++]) << 3);
                t = (t >> 5) - 1;
            } else if (t >= 32) {
                t &= 31;
                if (t == 0 && (t = runLength(31)) < 0)
                    return false;
                if (!need(2))
                    return false;
                m = op - 1 - ((in[ip] >> 2) + (qsizetype(in[ip + 1]) << 6));
                ip += 2;
            } else if (t >= 16) {
                m = op - ((t & 8) << 11);
                t &= 7;
                if (t == 0 && (t = runLength(7)) < 0)
                    return false;
                if (!need(2))
                    return false;
                m -= (in[ip] >> 2) + (qsizetype(in[ip + 1]) << 6);
                ip += 2;
                if (m == op) { // end of stream
                    if (consumed)
                        *consumed = ip;
                    return op == expected;
                }
                m -= 0x4000;
            } else {
                if (!need(1))
                    return false;
                m = op - 1 - (t >> 2) - (qsizetype(in[ip++]) << 2);
                if (!copyMatch(m, 2))
                    return false;
                st = MatchDone;
                break;
            }
            if (!copyMatch(m, t + 2))
                return false;
            st = MatchDone;
            break;
        case MatchDone:
            t = in[ip - 2] & 3;
            st = t == 0 ? Loop : MatchNext;
            break;
        case MatchNext:
            if (!copyLiterals(t) || !need(1))
                return false;
            t = in[ip++];
            st = Match;
            break;
        }
    }
}

bool lzss(const char *inp, qsizetype inSize, qsizetype expected, QByteArray &out, qsizetype *consumed, bool checksum)
{
    const auto *in = reinterpret_cast<const quint8 *>(inp);
    out.resize(expected);
    auto *o = reinterpret_cast<quint8 *>(out.data());
    qsizetype ip = 0, op = 0;
    quint32 sum = 0;
    while (op < expected) {
        if (ip >= inSize)
            return false;
        const quint8 flags = in[ip++];
        for (int bit = 0; bit < 8 && op < expected; ++bit) {
            if (flags & (1 << bit)) {
                if (ip >= inSize)
                    return false;
                const quint8 c = in[ip++];
                o[op++] = c;
                sum += c;
            } else {
                if (ip + 2 > inSize)
                    return false;
                qsizetype rpos = (in[ip] | ((in[ip + 1] & 0xF0) << 4));
                qsizetype rlen = (in[ip + 1] & 0x0F) + 3;
                ip += 2;
                rpos = op - rpos;
                while (rpos < 0 && rlen > 0 && op < expected) { // before the buffer start: spaces
                    o[op++] = ' ';
                    sum += ' ';
                    ++rpos;
                    --rlen;
                }
                while (rlen-- > 0 && op < expected) {
                    const quint8 c = o[rpos++];
                    o[op++] = c;
                    sum += c;
                }
            }
        }
    }
    if (checksum) {
        if (ip + 4 > inSize)
            return false;
        const quint32 stored = quint32(in[ip]) | (quint32(in[ip + 1]) << 8) | (quint32(in[ip + 2]) << 16) | (quint32(in[ip + 3]) << 24);
        ip += 4;
        if (stored != sum)
            return false;
    }
    if (consumed)
        *consumed = ip;
    return true;
}

} // namespace Compression
