#pragma once

#include <QByteArray>

// Decompressors used by Real Virtuality data files.
namespace Compression {

// LZO1X (as produced by lzo1x_1 / minilzo). `expected` is the exact output
// size. Returns false on corrupt input. `consumed` receives the number of
// input bytes read (the stream is self-terminating).
bool lzo1x(const char *in, qsizetype inSize, qsizetype expected, QByteArray &out, qsizetype *consumed = nullptr);

// Bohemia LZSS (PBO "Cprs" entries, PAA, old ODOL). Output size must be known.
// `checksum` controls whether a trailing 4-byte additive checksum follows and
// is verified/skipped. Returns false on corrupt input.
bool lzss(const char *in, qsizetype inSize, qsizetype expected, QByteArray &out, qsizetype *consumed = nullptr,
          bool checksum = true);

} // namespace Compression
