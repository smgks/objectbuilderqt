// Generates small FBX 7.4 test scenes (binary and ASCII) for the importer:
//   - "1.000"     : a cube mesh with UVs, material "box_mat" with texture box_co.paa
//   - "Geometry"  : a cube (Geometry LOD by name)
//   - "Lods"      : a LodGroup with two child meshes ("LOD0", "LOD1")
// usage: node tests/make_test_fbx.js <out-dir>
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const out = process.argv[2] || '.';

// cube with 8 control points, 6 quads, per-polygon-vertex UVs (indexed)
const V = [-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1].map(x => x * 50); // cm
const quads = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 4, 7, 3]]; // outward CCW (RH)
const PVI = [];
for (const q of quads) q.forEach((v, i) => PVI.push(i === 3 ? ~v : v));
const UV = [0, 0, 1, 0, 1, 1, 0, 1];
const UVIndex = PVI.map((_, i) => i % 4);

let nextId = 1000;
const ids = {};
const id = name => (ids[name] ??= nextId++);

// ---------------------------------------------------------------- model
function node(name, props, children = []) { return { name, props, children }; }
const S = s => ({ t: 'S', v: s });
const I = v => ({ t: 'I', v });
const L = v => ({ t: 'L', v });
const D = v => ({ t: 'D', v });
const Ad = a => ({ t: 'd', v: a });
const Ai = a => ({ t: 'i', v: a });
const P = (n, type, ...vals) => node('P', [S(n), S(type), S(''), S('A'), ...vals]);

function geometry(gname) {
    return node('Geometry', [L(id('geo:' + gname)), S(gname + '\x00\x01Geometry'), S('Mesh')], [
        node('Vertices', [Ad(V)]),
        node('PolygonVertexIndex', [Ai(PVI)]),
        node('LayerElementUV', [I(0)], [
            node('MappingInformationType', [S('ByPolygonVertex')]),
            node('ReferenceInformationType', [S('IndexToDirect')]),
            node('UV', [Ad(UV)]),
            node('UVIndex', [Ai(UVIndex)]),
        ]),
        node('LayerElementMaterial', [I(0)], [
            node('MappingInformationType', [S('AllSame')]),
            node('ReferenceInformationType', [S('IndexToDirect')]),
            node('Materials', [Ai([0])]),
        ]),
    ]);
}
function model(mname, attr = 'Mesh', tx = [0, 0, 0]) {
    return node('Model', [L(id('model:' + mname)), S(mname + '\x00\x01Model'), S(attr)], [
        node('Properties70', [], [P('Lcl Translation', 'Lcl Translation', D(tx[0]), D(tx[1]), D(tx[2]))]),
    ]);
}
const objects = [
    geometry('cube1'), model('1.000'),
    geometry('cube2'), model('Geometry'),
    geometry('cube3'), geometry('cube4'), model('Lods', 'Null'), model('LOD0', 'Mesh', [300, 0, 0]), model('LOD1', 'Mesh', [300, 0, 0]),
    node('NodeAttribute', [L(id('attr:lods')), S('Lods\x00\x01NodeAttribute'), S('LodGroup')]),
    node('Material', [L(id('mat')), S('box_mat\x00\x01Material'), S('')]),
    node('Texture', [L(id('tex')), S('box\x00\x01Texture'), S('')], [
        node('FileName', [S('P:\\test\\data\\box_co.paa')]),
        node('RelativeFilename', [S('data\\box_co.paa')]),
    ]),
];
const C = (a, b, p) => node('C', p ? [S('OP'), L(id(a)), L(id(b)), S(p)] : [S('OO'), L(id(a)), L(b === 0 ? 0 : id(b))]);
const connections = [
    C('model:1.000', 0), C('geo:cube1', 'model:1.000'), C('mat', 'model:1.000'), C('tex', 'mat', 'DiffuseColor'),
    C('model:Geometry', 0), C('geo:cube2', 'model:Geometry'),
    C('model:Lods', 0), C('attr:lods', 'model:Lods'),
    C('model:LOD0', 'model:Lods'), C('geo:cube3', 'model:LOD0'),
    C('model:LOD1', 'model:Lods'), C('geo:cube4', 'model:LOD1'),
];
const top = [
    node('GlobalSettings', [], [node('Properties70', [], [
        P('UpAxis', 'int', I(1)), P('UpAxisSign', 'int', I(1)), P('FrontAxis', 'int', I(2)), P('FrontAxisSign', 'int', I(1)),
        P('CoordAxis', 'int', I(0)), P('CoordAxisSign', 'int', I(1)), P('UnitScaleFactor', 'double', D(1.0)),
    ])]),
    node('Objects', [], objects),
    node('Connections', [], connections),
];

// ---------------------------------------------------------------- binary writer (7400)
function encProp(p) {
    switch (p.t) {
    case 'S': { const s = Buffer.from(p.v, 'latin1'); const h = Buffer.alloc(5); h.write('S'); h.writeUInt32LE(s.length, 1); return Buffer.concat([h, s]); }
    case 'I': { const b = Buffer.alloc(5); b.write('I'); b.writeInt32LE(p.v, 1); return b; }
    case 'L': { const b = Buffer.alloc(9); b.write('L'); b.writeBigInt64LE(BigInt(p.v), 1); return b; }
    case 'D': { const b = Buffer.alloc(9); b.write('D'); b.writeDoubleLE(p.v, 1); return b; }
    case 'd': case 'i': {
        const es = p.t === 'd' ? 8 : 4;
        const raw = Buffer.alloc(p.v.length * es);
        p.v.forEach((x, i) => p.t === 'd' ? raw.writeDoubleLE(x, i * es) : raw.writeInt32LE(x, i * es));
        const z = zlib.deflateSync(raw);
        const h = Buffer.alloc(13); h.write(p.t); h.writeUInt32LE(p.v.length, 1); h.writeUInt32LE(1, 5); h.writeUInt32LE(z.length, 9);
        return Buffer.concat([h, z]);
    }
    }
}
function encNode(n, offset) {
    const name = Buffer.from(n.name);
    const props = Buffer.concat(n.props.map(encProp));
    const headLen = 13 + name.length;
    let pos = offset + headLen + props.length;
    const kids = [];
    for (const c of n.children) { const b = encNode(c, pos); kids.push(b); pos += b.length; }
    if (n.children.length) { kids.push(Buffer.alloc(13)); pos += 13; }
    const h = Buffer.alloc(13);
    h.writeUInt32LE(pos, 0); h.writeUInt32LE(n.props.length, 4); h.writeUInt32LE(props.length, 8); h.writeUInt8(name.length, 12);
    return Buffer.concat([h, name, props, ...kids]);
}
const header = Buffer.concat([Buffer.from('Kaydara FBX Binary  \x00', 'latin1'), Buffer.from([0x1a, 0x00]), Buffer.alloc(4)]);
header.writeUInt32LE(7400, 23);
let off = header.length;
const body = [];
for (const n of top) { const b = encNode(n, off); body.push(b); off += b.length; }
body.push(Buffer.alloc(13));
fs.writeFileSync(path.join(out, 'test_binary.fbx'), Buffer.concat([header, ...body]));

// ---------------------------------------------------------------- ASCII writer
function asciiProp(p) {
    switch (p.t) {
    case 'S': { const s = p.v.includes('\x00\x01') ? p.v.split('\x00\x01').reverse().join('::') : p.v; return '"' + s + '"'; } // FBX ASCII does not escape backslashes
    case 'D': return p.v.toFixed(6);
    default: return String(p.v);
    }
}
function asciiNode(n, ind) {
    const arr = n.props.length === 1 && (n.props[0].t === 'd' || n.props[0].t === 'i');
    let s = ind + n.name + ': ';
    if (arr) return s + `*${n.props[0].v.length} {\n${ind}\ta: ${n.props[0].v.join(',')}\n${ind}}\n`;
    s += n.props.map(asciiProp).join(', ');
    if (n.children.length) s += ' {\n' + n.children.map(c => asciiNode(c, ind + '\t')).join('') + ind + '}';
    return s + '\n';
}
fs.writeFileSync(path.join(out, 'test_ascii.fbx'), '; FBX 7.4.0 project file\n' + top.map(n => asciiNode(n, '')).join(''));
console.log('wrote test_binary.fbx and test_ascii.fbx');
