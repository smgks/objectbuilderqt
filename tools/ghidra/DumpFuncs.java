// Headless query helper.
// Args: <outFile> <query>...
//   str:<text>     decompile every function referencing a defined string containing <text>
//   fn:<hexaddr>   decompile the function containing the address
//   callers:<hex>  list callers of the function containing the address
//   calls:<hex>    list callees
//   asm:<hex>      disassembly of the function containing the address
//@category REA
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;

public class DumpFuncs extends GhidraScript {
    DecompInterface dec;
    PrintWriter out;
    Set<Function> done = new HashSet<>();

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        out = new PrintWriter(new FileWriter(args[0]));
        dec = new DecompInterface();
        dec.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            String q = args[i];
            out.println("##### " + q);
            if (q.startsWith("str:")) byString(q.substring(4));
            else if (q.startsWith("fn:")) decomp(fnAt(q.substring(3)));
            else if (q.startsWith("callers:")) callers(fnAt(q.substring(8)));
            else if (q.startsWith("calls:")) calls(fnAt(q.substring(6)));
            else if (q.startsWith("asm:")) asm(fnAt(q.substring(4)));
            else if (q.startsWith("vt:")) vtable(q.substring(3));
        }
        out.close();
    }

    Function fnAt(String hex) {
        Address a = toAddr(Long.parseUnsignedLong(hex.replace("0x", ""), 16));
        Function f = getFunctionContaining(a);
        if (f == null) out.println("no function at " + hex);
        return f;
    }

    void byString(String needle) {
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!(d.getValue() instanceof String)) continue;
            String s = (String) d.getValue();
            if (!s.contains(needle)) continue;
            out.println("== string @" + d.getAddress() + ": " + s);
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) {
                    out.println("   ref from " + r.getFromAddress() + " in " + f.getName() + " @" + f.getEntryPoint());
                    decomp(f);
                }
            }
        }
    }

    void decomp(Function f) {
        if (f == null || !done.add(f)) return;
        DecompileResults r = dec.decompileFunction(f, 120, monitor);
        out.println("---- " + f.getName() + " @" + f.getEntryPoint());
        out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("decompile failed: " + r.getErrorMessage()));
    }

    void callers(Function f) {
        if (f == null) return;
        for (Function c : f.getCallingFunctions(monitor)) out.println("caller " + c.getName() + " @" + c.getEntryPoint());
    }

    void calls(Function f) {
        if (f == null) return;
        for (Function c : f.getCalledFunctions(monitor)) out.println("callee " + c.getName() + " @" + c.getEntryPoint());
    }

    void vtable(String name) throws Exception {
        for (ghidra.program.model.symbol.Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
            if (!s.getName(true).equals(name) && !s.getName(true).endsWith(name)) continue;
            out.println("vtable " + s.getName(true) + " @" + s.getAddress());
            for (int i = 0; i < 12; i++) {
                long p = getLong(s.getAddress().add(i * 8));
                Function f = getFunctionAt(toAddr(p));
                out.println("  [" + i + "] " + Long.toHexString(p) + " " + (f == null ? "" : f.getName()));
            }
            return;
        }
        out.println("symbol not found " + name);
    }

    void asm(Function f) {
        if (f == null) return;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            out.println(ins.getAddress() + "  " + ins);
        }
    }
}
