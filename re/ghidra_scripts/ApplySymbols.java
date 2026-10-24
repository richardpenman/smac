// Apply names from re/out/symbols.tsv to the current program.
//@category SMAC
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.*;

public class ApplySymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : "re/out/symbols.tsv";
        Set<String> used = new HashSet<>();
        int nf = 0, nd = 0;
        for (String line : Files.readAllLines(Paths.get(path))) {
            String[] p = line.split("\t", -1);
            if (p.length < 3) continue;
            Address a = toAddr(Long.parseLong(p[0], 16));
            String name = p[2];
            if (!used.add(name)) name = name + "_" + p[0];
            if (p[1].equals("func")) {
                disassemble(a);
                Function f = getFunctionAt(a);
                if (f == null) f = createFunction(a, name);
                if (f != null) { f.setName(name, SourceType.IMPORTED); nf++; }
            } else {
                createLabel(a, name, true, SourceType.IMPORTED);
                nd++;
            }
            if (p.length > 4 && !p[4].isEmpty()) setPlateComment(a, p[4]);
        }
        println("Applied " + nf + " functions, " + nd + " data labels");
    }
}
