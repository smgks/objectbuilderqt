#!/usr/bin/env node
// Regenerates resources/ui_spec.json from the original ObjectBuilder.exe.
// Reads the PE resource directory directly (no external tools) and extracts:
//   - main menu (RT_MENU 128) and context menus
//   - accelerator table (RT_ACCELERATOR 128)
//   - command status/tooltip strings (RT_STRING, ids >= 32768 and MFC ids 57600+)
//   - toolbars (RT_TOOLBAR)
// Usage: node tools/gen_ui_spec.js <ObjectBuilder.exe> <out.json>
'use strict';
const fs = require('fs');

const [, , exePath, outPath] = process.argv;
if (!exePath || !outPath) {
    console.error('usage: gen_ui_spec.js <ObjectBuilder.exe> <out.json>');
    process.exit(1);
}
const b = fs.readFileSync(exePath);

// ---------------------------------------------------------------- PE parsing
const peOff = b.readUInt32LE(0x3c);
const nSections = b.readUInt16LE(peOff + 6);
const optSize = b.readUInt16LE(peOff + 20);
const opt = peOff + 24;
const magic = b.readUInt16LE(opt);
const ddOff = opt + (magic === 0x20b ? 112 : 96);
const rsrcRva = b.readUInt32LE(ddOff + 2 * 8);
const secOff = opt + optSize;
const sections = [];
for (let i = 0; i < nSections; i++) {
    const s = secOff + i * 40;
    sections.push({ va: b.readUInt32LE(s + 12), vsize: b.readUInt32LE(s + 8), raw: b.readUInt32LE(s + 20), rsize: b.readUInt32LE(s + 16) });
}
const rva2off = rva => {
    for (const s of sections) if (rva >= s.va && rva < s.va + Math.max(s.vsize, s.rsize)) return rva - s.va + s.raw;
    throw new Error('bad rva ' + rva.toString(16));
};
const rsrc = rva2off(rsrcRva);

function readDir(off) {
    const nNamed = b.readUInt16LE(off + 12), nId = b.readUInt16LE(off + 14), out = [];
    for (let i = 0; i < nNamed + nId; i++) {
        const e = off + 16 + i * 8, nameF = b.readUInt32LE(e), dataF = b.readUInt32LE(e + 4);
        let key;
        if (nameF & 0x80000000) {
            const so = rsrc + (nameF & 0x7fffffff), len = b.readUInt16LE(so);
            key = b.toString('utf16le', so + 2, so + 2 + len * 2);
        } else key = nameF;
        out.push({ key, sub: !!(dataF & 0x80000000), off: rsrc + (dataF & 0x7fffffff) });
    }
    return out;
}
const resources = {}; // type -> name -> Buffer (first language)
for (const t of readDir(rsrc)) {
    resources[t.key] = {};
    for (const n of readDir(t.off)) {
        const lang = readDir(n.off)[0];
        const rva = b.readUInt32LE(lang.off), size = b.readUInt32LE(lang.off + 4);
        const o = rva2off(rva);
        resources[t.key][n.key] = b.subarray(o, o + size);
    }
}

// ---------------------------------------------------------------- decoders
function wstr(buf, o) { let s = ''; for (;;) { const c = buf.readUInt16LE(o); o += 2; if (!c) break; s += String.fromCharCode(c); } return [s, o]; }

function decodeMenu(buf) {
    let o = 4;
    function items() {
        const list = [];
        for (;;) {
            const fl = buf.readUInt16LE(o); o += 2;
            let id = 0;
            if (!(fl & 0x10)) { id = buf.readUInt16LE(o); o += 2; }
            let s; [s, o] = wstr(buf, o);
            if (fl & 0x10) list.push({ text: s, items: items() });
            else if (s === '' && id === 0) list.push({ separator: true });
            else list.push({ id, text: s });
            if (fl & 0x80) return list;
        }
    }
    return items();
}

const VK = { 0x08: 'Backspace', 0x09: 'Tab', 0x0d: 'Return', 0x1b: 'Esc', 0x20: 'Space', 0x21: 'PgUp', 0x22: 'PgDown',
    0x23: 'End', 0x24: 'Home', 0x25: 'Left', 0x26: 'Up', 0x27: 'Right', 0x28: 'Down', 0x2d: 'Ins', 0x2e: 'Del',
    0x6a: 'Num+*', 0x6b: 'Num++', 0x6d: 'Num+-', 0x6e: 'Num+.', 0x6f: 'Num+/',
    0xba: ';', 0xbb: '=', 0xbc: ',', 0xbd: '-', 0xbe: '.', 0xbf: '/', 0xc0: '`', 0xdb: '[', 0xdc: '\\', 0xdd: ']', 0xde: '\'' };
function vkName(k) {
    if ((k >= 0x30 && k <= 0x39) || (k >= 0x41 && k <= 0x5a)) return String.fromCharCode(k);
    if (k >= 0x60 && k <= 0x69) return 'Num+' + (k - 0x60);
    if (k >= 0x70 && k <= 0x87) return 'F' + (k - 0x6f);
    return VK[k] || null;
}
function decodeAccel(buf) {
    const out = [];
    for (let o = 0; o + 8 <= buf.length; o += 8) {
        const f = buf.readUInt16LE(o), k = buf.readUInt16LE(o + 2), id = buf.readUInt16LE(o + 4);
        const key = f & 1 ? vkName(k) : String.fromCharCode(k);
        if (key) {
            const mods = (f & 0x08 ? 'Ctrl+' : '') + (f & 0x10 ? 'Alt+' : '') + (f & 0x04 ? 'Shift+' : '');
            out.push({ id, key: mods + key });
        }
        if (f & 0x80) break;
    }
    return out;
}
function decodeStrings() {
    const out = {};
    for (const [block, buf] of Object.entries(resources[6] || {})) {
        let o = 0;
        for (let i = 0; i < 16; i++) {
            const len = buf.readUInt16LE(o); o += 2;
            if (len) out[(block - 1) * 16 + i] = buf.toString('utf16le', o, o + len * 2);
            o += len * 2;
        }
    }
    return out;
}
function decodeToolbar(buf) {
    const n = buf.readUInt16LE(6), ids = [];
    for (let i = 0; i < n; i++) ids.push(buf.readUInt16LE(8 + 2 * i));
    return ids;
}

// ---------------------------------------------------------------- assemble
const strings = decodeStrings();
const tips = {};
for (const [id, s] of Object.entries(strings)) if (+id >= 32768) tips[id] = s;
const undoText = {};
for (const [id, s] of Object.entries(strings)) if (+id >= 10000 && +id < 11000) undoText[id] = s;

const toolbarNames = { 128: 'Main Toolbar', 137: 'View Control Bar', 145: 'Edit Toolbar', 189: 'UV Editor', 237: 'Selection Operators', 423: 'Source Control' };
const spec = {
    source: { file: exePath.split(/[\\/]/).pop(), bytes: b.length, generatedBy: 'tools/gen_ui_spec.js' },
    mainMenu: decodeMenu(resources[4][128]),
    contextMenus: {
        lodList: decodeMenu(resources[4][150]),
        namedSelections: decodeMenu(resources[4][153]),
        namedProperties: decodeMenu(resources[4][133]),
        animations: decodeMenu(resources[4][180]),
        history: decodeMenu(resources[4][161]),
    },
    accelerators: decodeAccel(resources[9][128]),
    toolbars: Object.entries(resources[241] || {}).map(([n, buf]) => ({ resource: +n, name: toolbarNames[n] || 'Toolbar ' + n, ids: decodeToolbar(buf) })),
    tips,
    undoText,
};
fs.writeFileSync(outPath, JSON.stringify(spec, null, 1));

// Toolbar strips: RT_BITMAP shares the RT_TOOLBAR resource id. Wrap the DIB in a
// BITMAPFILEHEADER so Qt can load it as a .bmp.
const outDir = require('path').join(require('path').dirname(outPath), 'toolbars');
fs.mkdirSync(outDir, { recursive: true });
for (const tb of spec.toolbars) {
    const dib = resources[2] && resources[2][tb.resource];
    if (!dib) continue;
    const hdrSize = dib.readUInt32LE(0), bpp = dib.readUInt16LE(14), clrUsed = dib.readUInt32LE(32);
    const palette = bpp <= 8 ? (clrUsed || (1 << bpp)) * 4 : 0;
    const fh = Buffer.alloc(14);
    fh.write('BM', 0, 'ascii');
    fh.writeUInt32LE(14 + dib.length, 2);
    fh.writeUInt32LE(14 + hdrSize + palette, 10);
    fs.writeFileSync(require('path').join(outDir, `tb_${tb.resource}.bmp`), Buffer.concat([fh, dib]));
}
console.log(`wrote ${outPath}: ${spec.accelerators.length} accelerators, ${Object.keys(tips).length} tips, ${spec.toolbars.length} toolbars`);
