// Exports the named functions and named data labels of the open program to CSV, so the ROM
// reverse-engineering work can be browsed without Ghidra.
//
// Usage (headless):
//   analyzeHeadless <project dir> <project name> -process rom.u7 -noanalysis -readOnly \
//       -scriptPath tools/ghidra -postScript ExportSymbols.java <output dir>
//
// Writes <output dir>/rom_functions.csv and <output dir>/rom_globals.csv.
//
// @category GigaPets

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class ExportSymbols extends GhidraScript {
    private static String q(String s) {
        return "\"" + s.replace("\"", "\"\"") + "\"";
    }

    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        new File(outDir).mkdirs();

        try (PrintWriter w = new PrintWriter(new FileWriter(new File(outDir, "rom_functions.csv")))) {
            w.println("address,name,size_bytes,signature");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                Address a = f.getEntryPoint();
                long size = f.getBody().getNumAddresses();
                w.println(String.format("%06X,%s,%d,%s", a.getOffset(), f.getName(), size * 2,
                        q(f.getPrototypeString(false, false))));
            }
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(new File(outDir, "rom_globals.csv")))) {
            w.println("space,address,name,data_type");
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (s.getSource() == SourceType.DEFAULT) continue;
                if (s.getSymbolType() != SymbolType.LABEL) continue;
                Address a = s.getAddress();
                if (a.isExternalAddress()) continue;
                if (currentProgram.getFunctionManager().getFunctionAt(a) != null) continue;
                AddressSpace sp = a.getAddressSpace();
                Data d = currentProgram.getListing().getDataAt(a);
                String type = d != null ? d.getDataType().getName() : "";
                w.println(String.format("%s,%06X,%s,%s", sp.getName(), a.getOffset(), s.getName(), type));
            }
        }
        println("Exported functions and globals to " + outDir);
    }
}
