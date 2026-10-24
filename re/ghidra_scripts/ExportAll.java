// Export every function: address, name, size, callees, callers, decompiled C.
// Writes JSON lines to the path given as first script arg.
//@category SMAC
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class ExportAll extends GhidraScript {
    static String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int)c));
                    else b.append(c);
            }
        }
        return b.toString();
    }
    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        DecompileOptions opts = new DecompileOptions();
        di.setOptions(opts);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isThunk() || f.isExternal()) continue;
            StringBuilder callees = new StringBuilder(), callers = new StringBuilder();
            for (Function c : f.getCalledFunctions(mon)) callees.append(callees.length() > 0 ? "," : "").append("\"").append(c.getEntryPoint()).append("\"");
            for (Function c : f.getCallingFunctions(mon)) callers.append(callers.length() > 0 ? "," : "").append("\"").append(c.getEntryPoint()).append("\"");
            String code = "";
            DecompileResults r = di.decompileFunction(f, 120, mon);
            if (r != null && r.decompileCompleted()) code = r.getDecompiledFunction().getC();
            else code = "/* decompile failed: " + (r == null ? "null" : r.getErrorMessage()) + " */";
            w.println("{\"addr\":\"" + f.getEntryPoint() + "\",\"name\":\"" + esc(f.getName()) +
                "\",\"size\":" + f.getBody().getNumAddresses() + ",\"cc\":\"" + esc(String.valueOf(f.getCallingConventionName())) +
                "\",\"callees\":[" + callees + "],\"callers\":[" + callers + "],\"c\":\"" + esc(code) + "\"}");
            if (++n % 200 == 0) { println("exported " + n); w.flush(); }
        }
        w.close();
        println("Exported " + n + " functions");
    }
}
