# Reverse-engineering notes

Source: `ObjectBuilder.exe` (DayZ Tools build, x86 PE, MFC,
SHA-256 `0b2ce11cf6b754e1e11742cfc002b26242cac5c2141e7b16c890d22ab915212a`),
analysed with REA (Ghidra 12.1.4 provider). Addresses are virtual addresses in
that build. Evidence IDs (`ev_…`) refer to the REA session ledger.

Confidence labels used below:

- **Observed**: read directly from the binary (decompiled code, resources, strings).
- **Inferred**: deduced from observed code plus reasoning; not directly verified.
- **Public**: taken from public community documentation, not traced in the binary.

## 1. Application structure (observed)

| Item | Evidence |
|---|---|
| MFC doc/view app: `CObjektiv2App`, `CObjektiv2Doc`, `CObjektiv2View`, `CMainFrame` | RTTI strings, `ev_274cbd32…` |
| Panels: `CLodList`, `CMassBar`, `CNamedPropDlg`, `CFaceProp`, `CLodConfig`, `CConfig` | RTTI strings |
| Undoable edits are command objects (`CICommand` base, e.g. `CICreatePrim`, `CICreateProxy`) | vtables at `0x6e7afc`, `0x6e061c` |
| Plugins: DLLs exporting `InitPluginModule` (FBX, MAT, ModelConfigEditor) | `objectbuilder.log` |
| Settings: `HKCU\Software\Bohemia Interactive\DayZ Tools\Object Builder\CConfig`, typed key prefixes `#` bool, `0` number, `$` text, `\` folder, `%` file, `C` colour | `ObjectBuilder_defaultSettings.reg` |

UI resources (14 menus, 90 dialogs, 95 string tables, 3 accelerator tables,
6 toolbars) were decoded from the PE resource tree (`inspect_pe_resources`).
`tools/gen_ui_spec.js` re-extracts them into `resources/ui_spec.json` and
`resources/toolbars/*.bmp` without REA.

## 2. MLOD P3D format (observed)

All integers little-endian, `u32`/`i32` 4 bytes, `f32` IEEE float.

### File header — writer `0x5efcf0`, reader `0x5f0070` (`ev_03b5efe1…`)

```
char[4]  "MLOD"
u32      version = 0x101          (reader also accepts 9999)
u32      lodCount
lodCount × { LOD block ; f32 resolution }
```

Reader alternatives: `"NLOD"` + `u32 lodCount` (no version); `"ODOL"` goes to
a separate binarized-model loader (`0x5f0a60`, not rebuilt).

### LOD block — writer `0x5e5960`, reader `0x5e6600`

```
char[4]  "P3DM"  (old: "SP3X")
u32      headerSize = 28          (reader skips headerSize-28 extra bytes)
u32      version = 0x100          (SP3X: 0x99)
u32      nPoints, nNormals, nFaces
u32      flags = 0
nPoints  × { f32 x, y, z ; u32 pointFlags }
nNormals × { f32 x, y, z }
nFaces   × face
"TAGG" tag block
```

P3DM face:
```
u32 nVerts (3 or 4)
4 × { u32 point ; u32 normal ; f32 u ; f32 v }   (always 4 written)
u32 faceFlags
asciiz texture
asciiz material                  (both lower-cased by the reader)
```
SP3X face: `char[32] texture`, then the same `nVerts … faceFlags` (no material).
Face UVs are those of UV set id 0.

### Tag block — writer `0x5e6250` (`ev_8089438e…`), reader `0x5e7110` (`ev_7ce59825…`)

`"TAGG"`, then tags until `#EndOfFile#`. Tag header (`0x5e5dd0`):
P3DM: `u8 active(=1) ; asciiz name ; u32 dataSize`; SP3X: `char[64] name ; u32 dataSize`.

Write order and payloads:

| # | Tag | Condition | Payload |
|---|---|---|---|
| 1 | `#SharpEdges#` | any sharp edges | `n × {i32 a, i32 b}` (reader swaps so a ≤ b) |
| 2 | `#Selected#` | selection not empty | selection bytes |
| 3 | *named selection* | non-empty; name not starting with `!` | selection bytes (max 2048 selections) |
| 4 | `#Property#` | per property; not `!` | `char[64] name ; char[64] value` |
| 5 | `#Lock#` | not empty | selection bytes |
| 6 | `#Hide#` | not empty | selection bytes |
| 7 | `#Mass#` | resolution is ±1e13, ±3e13 or ±4e13 | `nPoints × f32` |
| 8 | `#Animation#` | per phase | `f32 time ; nPoints × {f32 x,y,z}` |
| 9 | `#UVSet#` | per UV set | `u32 id ; for each face, for each vertex {f32 u, v}` |
| 10 | `#EndOfFile#` | always | empty |

Selection bytes (`0x5d6910` / `0x5d69c0`): `nPoints` bytes holding the
**negated** point weight (in memory 255 = fully selected, on disk 1), then
`nFaces` bytes 0/1. With the export flag (`param_2`) set, names starting with
`.` or `-` and tags 2, 5, 6 are skipped. The reader replaces all UV sets on the
first `#UVSet#`; unknown `#…#` tags are skipped; any other name is a named
selection. A legacy `SS3D` block with 64-byte `$Name$` tags exists in the
reader but is not supported here.

**Verified**: `tests/core_tests.cpp` saves, reloads and re-saves a model with
every tag type and checks the bytes are identical.

## 3. LOD resolution names — `0x620d70` (`ev_73b8c36c…`), strings `0x6f9b8c..0x6f9e1c`

Observed ranges in the name function:

| Resolution | Name |
|---|---|
| 1000, 1100 | View - Gunner, View - Pilot |
| [1200, 1300) | `View - Cargo %.3f` (offset from 1200) |
| [10000, 20000) | `ShadowVolume %.3f` |
| [20000, 30000) | `Edit %.3f` |
| [8e15, 9e15) | `View - Cargo - Geometry %.3f` (index = res/1e15 − 8) |
| [1.8e16, 1.9e16) | `ShadowVolume - View - Cargo %.3f` |
| ≈ ±2e13, ±3e13, ±4e13 (±1e-3 relative) | geometry variants |
| ≥ 5e14 | switch on `round(res / 1e15)` |
| other | `%7.3f` |

**Inferred**: which literal belongs to which switch case (Memory = 1e15,
LandContact = 2e15, RoadWay = 3e15, Paths = 4e15, Hit-points = 5e15,
View Geometry = 6e15, Fire Geometry = 7e15, … Wreck = 2.1e16; Geometry = 1e13,
Buoyancy = 2e13, Phys Old = 3e13, Phys = 4e13). The case bodies were lost in
decompilation; the mapping matches the string order and public documentation.

## 4. Face flags — `CFaceProp` dialog → flags at `0x51cf80` (`ev_a8361e78…`), observed

| Field | Mask | Values |
|---|---|---|
| Lighting | `0x003000A7` | Normal 0, Both sides `0x20`, Position `0x80`, Flat `0x100000`, Reversed `0x200000` |
| Z bias | `0x300` | None 0, Low `0x100`, Middle `0x200`, High `0x300` |
| Enable shadow (unchecked sets) | `0x10` | |
| Enable texture merging (unchecked sets) | `0x1000000` | |
| User value | `0xFE000000` | value << 25 (`0x51d2b0` shows `flags >> 25`) |

## 5. Point flags

- `0x1000000` = hidden/special vertex — **observed** (proxy points, `0x5fa110`).
- Surface (`0xF`), lighting (`0xF0`), decal (`0x300`), fog (`0x3000`), user (`0xFF0000`) — **public**, matching the Point Properties dialog (resource 175) groups but not traced in its handler.

## 6. Geometry conventions

- **Winding**: the Plane generator (`0x4fdbb0`, `ev_58ae5e29…`) emits quads
  `(r,c) (r,c+1) (r+1,c+1) (r+1,c)` on a local XY grid at z=0 that faces the
  creating camera, i.e. counter-clockwise on screen. In the left-handed model
  space (X right, Y up, Z forward) the outward (front) normal is therefore
  `(p2−p0) × (p1−p0)`.
- **Stored vertex normals point to the back side** (observed): the face
  normal function `0x5d3f00` (`ev_558404a6…`) sums
  `(v[i]−v[i−1]) × (v[i+1]−v[i−1])`, i.e. `(p1−p0) × (p2−p0)`, and
  Recalculate Normals (`0x5eb3f0`/`0x5eb7c0`, corner normals `0x5d40a0`)
  stores that direction. Shipped models agree: on the converted sedan and
  AKM, 96–100 % of visual-LOD faces have stored normals in that direction,
  while their closed parts are outward by the front normal above.
  `ObjectData::storedNormal` / `faceNormal` model the two directions.
- **Primitives** (`CICreatePrim`, `0x4fe5a0`): unit shape scaled by size/2,
  aligned (center / bottom / top) and placed by pin + view orientation; new
  points and faces become the selection.
- **Proxy** (`CICreateProxy` → `0x5fa110`, `ev_a6313834…`): triangle `pin`,
  `pin+(0,2,0)`, `pin+(0,0,1)`, all with point flag `0x1000000`; selection named
  `proxy:<name>`. The `.NNN` index suffix follows public convention.
- **Clipboard**: copy writes one LOD block (P3DM + TAGG) to a private
  clipboard format (`0x5f7240`); paste reads it back (`0x5f7450`).

## 7. Not reverse-engineered yet

- `SS3D` legacy tags. (ODOL is covered by section 8, from binarize.exe.)
- External viewer (Buldozer) protocol, source-control integration.
- O2Script (`.bio2s`) interpreter (`O2Script.exe` / scripting console).
- UV editor (toolbar 189), gizmo mapping, animation tools, texture library,
  3DS/FBX/ASF/BVH import, plugin ABI (`InitPluginModule`).
- Exact behaviour of "Center All", "Squarize", "Uncross", "Merge near" and
  "Remove" (re-implemented from their names/descriptions).

## 8. Binarized models (ODOL) — from DayZ Tools `binarize.exe`

The original Object Builder cannot import binarized models: its ODOL branch
(`0x5f0a60`, `ev_19b17258…`) loads a placeholder box and reports
"Binarized model, cannot import - loaded fake for some calculations". The
reader here was reconstructed from the engine code embedded in
`DayZ Tools\Bin\Binarize\binarize.exe` (x64, SHA-256
`8dea817b88247ed13f29f76bf5a1ded218030ee4c6d1994f5df24963318b314a`), analysed
with headless Ghidra 12.1.4 (the same engine REA drives; the binary was too
large for REA's request timeout). Addresses below are in that binary.
All **observed** unless marked otherwise.

**DayZ Tools 1.30** (`BINARIZE 1.30.164078.38`, SHA-256
`f1594bce29a6d3cabe9ff08a24af6c4f31da6e9def5b09159ce0428614cb6d5b`) writes
ODOL **v56**. Re-analysed the same way (`tools/ghidra`, `imm:38` finds the new
version checks). In that binary `LODShape::SerializeBin` is `0x1407f9490`
(rejects versions outside 0x1c..0x38: "Model %s is in unsupported format %d
(current format is %d, minimum supported is %d)") and ModelInfo is
`0x1407fd730`. The only v56 change in the file layout is one ModelInfo field
(below); the rest of the serializer's version checks are the same as before,
and v56 LODs parse like v55 ones (checked on the AKM binarized by 1.30 and on
`tests/data/binarize_v56_odol.p3d`). Addresses elsewhere in this section are
in the older binary.

### File (`LODShape::SerializeBin`, `0x140937300`)
```
"ODOL" u32 version            (1.30 engine accepts 28..56; DayZ ships 54)
u32 nLods (low byte used) ; f32 resolution[nLods]
ModelInfo                      (0x14093ad50, see below)
v>29: u8 hasAnimations ; AnimationHolder (skipped by the reader)
u32 lodStart[nLods] ; u32 lodEnd[nLods] ; u8 permanent[nLods]
per non-permanent LOD: 25-byte summary (0x140933ec0: 4 x u32, u8, u32, f32)
LOD blocks at lodStart[i]
```
Versions > 43 compress large arrays with LZO, older with LZSS.

The reader locates the LOD table by its signature (max end = file size and the
first LOD starting right after table + summaries), so animation data does not
need decoding. Verified on all 14,688 models in the DayZ install.

### ModelInfo (v54–56)
special, bounding/geometry sphere, remarks, and/or hints, aiming centre,
colours, view density, bbox, visual bbox (v≥52), bounding/geometry centre,
centre of mass, inverse inertia (3×3), 4 bools, v>52: u8 + f32,
**v≥56: i32** (a byte member at ModelInfo+0x1e0 written sign-extended to 4
bytes, read back truncated to a byte; 1.30 `0x1407fda7e`; meaning not
recovered, 0 in every file seen), v≥42: 4 floats,
v≥43: 2 floats, v>32: u8, v>36: u32 + u8, v≥48: shadow offset, u8 animated,
skeleton name + (if set) `SkeletonSource` (u8, bones as name/parent pairs,
v>40 pivots name), u8 map type, **mass array** (compressed f32), **mass**,
inverse mass, armor, inverse armor, special LOD indices, minShadow (u32),
v>37: canBlend, class, damage, u8, v>54: u8, v>30: string list.

### LOD (`Shape::SerializeBin`, `0x140938ca0`)
```
proxies: n x { asciiz name ; f32[12] transform (aside, up, dir, pos) ; i32 seq ;
               i32 selection ; i32 bone ; v>39 i32 section }
u32 n, u32[n] sub-skeleton map ; u32 n, n x (u32 m, u32[m]) skeleton map
v>=50 u32 vertexCount ; v>=51 f32 faceArea ; u32 orHints, andHints
vec3 bmin, bmax, centre ; f32 radius
textures: n x asciiz
materials: n x { asciiz name ; TexMaterial (0x140907ee0, version <= 20) }
u16 array pointToVertex ; u16 array vertexToPoint
u32 nFaces, u32 dataSize, u16 ; nFaces x { u8 n ; u16[n] vertex }
sections: n x { i32 begin, end, minBone, boneCount ; u32 ; i16 texture ;
                u32 special ; i32 material (if invalid: asciiz name) ;
                v>=36 u32 n, f32[n] }
named selections: n x { asciiz ; u16[] faces ; u8[] ; u8 sectional ;
                        i32[] sections ; u16[] vertices ; u8[] weights }
properties: n x { asciiz, asciiz } ; keyframes: n x { f32 time ; vec3[] }
u32 colorTop, color, special ; u8 boneRefSimple
u32 restSize (counted from this field)
v>=50 condensed u32 clip flags
UV set 0 ; u32 nUVSets ; (n-1) more UV sets
vec3[] positions ; condensed packed normals ; u64[] ST coords ;
12-byte vertex bone refs ; 32-byte neighbour bone refs
```
* Arrays: `u32 count` + block; blocks under 1024 bytes are raw (`0x1409da470`).
* Condensed arrays: `u32 count`, `u8 isDefault`, then one element or a block.
* UV set (`0x14093fe60`, decode `0x1409401c0`): file floats f2, f3, f0, f1 then
  packed `s16 u, s16 v`: `u = (s + 32767) / 65534 * (f0 - f2) + f2` (constants
  read from `0x140ced0fc`, `0x140bd06a8`).
* Normals (`0x14093ead0`): 10:10:10 signed, × −1/511 (`0x140c96460`).
* Sections address faces by index or by face-data offset (2 + 2n bytes per
  face); both are handled.
* Embedded materials are skipped (the face keeps the `.rvmat` path).

### Conversion to MLOD (design choices, **inferred** where noted)
* Vertices are welded back into points by identical position (binarized LODs
  drop the point map); per-vertex UVs/normals stay per face-vertex.
* Hard edges (shared edges with different vertex normals) become `#SharpEdges#`.
* **binarize reverses every face's vertex order and copies the vertex normals
  unchanged** (observed with `tests/data/binarize_box_src.p3d` →
  `binarize_box_odol.p3d`, made by DayZ Tools' binarize.exe). The converter
  reverses faces back unconditionally and keeps the normals. An earlier
  version guessed the winding from the normals and left game models
  inside-out.
* **Bone selections** (model.cfg skeleton bones such as `bolt`, `trigger`)
  are stored with empty vertex lists; membership is in the per-vertex bone
  table: 12 bytes per vertex = `u32 count; 4 x {u8 subSkeletonIndex, u8
  weight}`, with the LOD's `subSkeletonToSkeleton` (i32 per entry, read right
  after the proxies) mapping to the ModelInfo skeleton's bone list. The
  converter rebuilds the points (with weights) and the faces whose points all
  belong to the bone. Checked by round trip: the StalkerFWWeapons L115A3
  converted, saved and binarized again gives identical `boltbody`/`bolthandle`/
  `trigger` point and face counts. Selections that are neither bones nor
  face-listed (e.g. `bullet001`) carry no geometry in the binarized file and
  stay empty; like the original, empty selections are not saved.
* Face-only selections (hiddenSelections like `camo`) get their faces' points
  added, as an editable model has them.
* **Welding split vertices back into points** uses position + the vertex's
  bone refs + its point-selection membership + "used by a proxy face" as the
  key. Splits of one original point share all of these; separate parts that
  only touch (bolt against receiver) do not. Position alone merged 169 points
  across parts on the L115A3, 269 on the AKM and 1690 on the sedan, so moving
  parts would have dragged neighbouring faces. Regression fixture:
  `tests/data/binarize_bones_*` (two touching boxes, one bone each).
* Section `special` flags depend on the textures and materials binarize can
  load: the G36 round trip lost `0x100` (alpha) and `0x20000` when its
  `data` folder was missing and matched the original once it was present.
* Proxy triangles and `proxy:` selections are kept; their points get flag
  `0x1000000`. A proxy without a selection is rebuilt from its transform as
  pin, pin + 2·up, pin + dir (the CICreateProxy shape) — **inferred** mapping.
* Geometry LOD mass comes from the ModelInfo mass array (per vertex) or the
  total mass spread evenly.
* Not recovered: model.cfg animations (AnimationHolder), bone weights, clip
  flags → point flags, embedded material parameters.

## 9. Other formats implemented

* **PBO**: entries `asciiz name, u32 method, original size, reserved,
  timestamp, data size`; first entry with method `Vers` carries properties
  (`prefix`…); `Cprs` entries are LZSS. Observed on the DayZ archives.
* **PAA**: type tag (FF01 DXT1 … 8080 AI88), `GGAT` taggs, palette, mipmaps
  `u16 w, u16 h, u24 size`; DXT mipmaps with `w & 0x8000` are LZO compressed,
  others LZSS when smaller than raw. Verified by decoding DayZ textures.
* **FBX**: own parser for binary 7.x (zlib arrays) and ASCII. The import
  conventions (LodGroup, units, axes) follow option names found in the
  original `FBX.dll` plugin's strings; its code was not analysed.
