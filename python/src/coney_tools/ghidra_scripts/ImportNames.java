// SPDX-License-Identifier: GPL-3.0-or-later
// Applies Coney's published function names (docs/progress/ghidra-functions.tsv) to the open program by address.
// Run by `coney-tools ghidra import-names` through analyzeHeadless (docs/guides/ghidra.md#import-names), with three
// arguments: the input file (`0x<address>\t<size>\t<name>` per line), the report file it writes
// (`0x<address>\t<outcome>\t<size>` per line, size -1 when there is no function) and the mode: `apply` keeps the
// names a person gave by hand, `overwrite` replaces them too. coney_tools/ghidra_names.py reads the report; its
// OUTCOMES tuple lists every outcome written here.
//@category Coney

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ImportNames extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException("usage: ImportNames.java <names.tsv> <report.tsv> <apply|overwrite>");
        }
        boolean overwrite = args[2].equals("overwrite");
        List<String> report = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[0]), StandardCharsets.UTF_8)) {
            if (line.isBlank() || line.startsWith("#")) {
                continue;
            }
            monitor.checkCancelled();
            String[] fields = line.split("\t");
            Address address = toAddr(Long.parseLong(fields[0].substring(2), 16));
            report.add(fields[0] + "\t" + apply(address, Long.parseLong(fields[1]), fields[2], overwrite));
        }
        // Written only when every name has an outcome: a missing report tells the caller the run was cut short.
        Files.write(Path.of(args[1]), report, StandardCharsets.UTF_8);
    }

    /** Names the function at `address`, creating it when analysis found none; returns "outcome\tsize". */
    private String apply(Address address, long size, String name, boolean overwrite) {
        Function function = getFunctionAt(address);
        if (function != null && function.isThunk()) {
            // Renaming a thunk renames the function it jumps to, which has a listed name of its own.
            return "thunk	-1";
        }
        if (function != null) {
            long actual = size(function);
            if (function.getName().equals(name)) {
                return "same\t" + actual;
            }
            // Analysis, loader and earlier imports are ours to replace; a name typed by a person is not.
            if (function.getSymbol().getSource() == SourceType.USER_DEFINED && !overwrite) {
                return "kept\t" + actual;
            }
            return rename(function, name, "renamed");
        }
        if (getFunctionContaining(address) != null) {
            return "inside\t-1";
        }
        if (getInstructionAt(address) == null) {
            Data data = getDataAt(address);
            if (data != null && data.isDefined()) {
                return "data\t-1";
            }
            if (!disassemble(address)) {
                return "nocode\t-1";
            }
        }
        Function created = createFunction(address, null);
        if (created == null) {
            return "failed\t-1";
        }
        long actual = size(created);
        if (actual != size) {
            // A body of another size means analysis sees different code here: leave the address without a function.
            removeFunction(created);
            return "size\t" + actual;
        }
        return rename(created, name, "created");
    }

    /** Gives `function` the name with the source IMPORTED, so a later import may replace it. */
    private String rename(Function function, String name, String outcome) {
        long actual = size(function);
        try {
            function.setName(name, SourceType.IMPORTED);
        } catch (Exception error) {
            printerr(function.getEntryPoint() + ": " + error.getMessage());
            return "error\t" + actual;
        }
        return outcome + "\t" + actual;
    }

    /** The function's size as the listing measures it: from its lowest to its highest address. */
    private static long size(Function function) {
        AddressSetView body = function.getBody();
        return body.getMaxAddress().subtract(body.getMinAddress()) + 1;
    }
}
