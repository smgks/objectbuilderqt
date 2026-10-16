# Headless Ghidra queries

`binarize.exe` (17 MB) is too large for REA's per-request timeout, so the ODOL
format (docs/RE_NOTES.md section 8) was recovered with Ghidra's headless
analyzer and `DumpFuncs.java`.

One-time analysis (~25 min; copy the exe to a path without parentheses first,
the .bat launcher cannot handle `(x86)`):

```bat
set GHIDRA_MAXMEM=10G
analyzeHeadless.bat <projdir> binarize -import <copy>\binarize.exe -scriptPath tools\ghidra -postScript DumpFuncs.java out.txt "str:LZO decompression failed"
```

Later queries reuse the analysed project (seconds):

```bat
analyzeHeadless.bat <projdir> binarize -process binarize.exe -noanalysis -readOnly -scriptPath tools\ghidra -postScript DumpFuncs.java out.txt fn:140937300 calls:140937300 vt:AnimationHolder::vftable
```

Queries: `str:<text>` (decompile functions referencing a string), `fn:<addr>`,
`callers:<addr>`, `calls:<addr>`, `asm:<addr>`, `vt:<vtable symbol>`,
`imm:<hex>` (functions with `CMP x, <hex>`, e.g. `imm:38` for ODOL v56 checks).
