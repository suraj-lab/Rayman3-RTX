// Ghidra script: locate string references and decompile the containing functions.
// Used to answer how Rayman 3's IPT layer selects joystick (analog) vs pad (digital).
// @category Rayman3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.*;
import java.util.*;

public class FindIPT extends GhidraScript {
    private static final String[] NEEDLES = {
        "IPT_fn_vActiveJoystickControl",
        "IPT_fn_vActivePaddleControl",
        "IPT_fn_bIsJoystickControlAvailable",
        "IPT_fn_vGetJoystickAxesForPadNumber",
        "IPT_fn_vSetJoystickAxesForPadNumber"
    };

    @Override
    public void run() throws Exception {
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        Set<Address> seen = new HashSet<>();

        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Data d = it.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            String hit = null;
            for (String n : NEEDLES) if (s.contains(n)) { hit = n; break; }
            if (hit == null) continue;

            println("=== STRING " + hit + " @ " + d.getAddress());
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function f = getFunctionContaining(ref.getFromAddress());
                if (f == null) { println("  xref " + ref.getFromAddress() + " (no function)"); continue; }
                if (!seen.add(f.getEntryPoint())) { println("  xref -> " + f.getName() + " (already dumped)"); continue; }
                println("  xref from " + ref.getFromAddress() + " in " + f.getName() + " @ " + f.getEntryPoint());
                DecompileResults res = ifc.decompileFunction(f, 90, monitor);
                if (res.decompileCompleted()) {
                    println("---- DECOMPILED " + f.getName() + " ----");
                    println(res.getDecompiledFunction().getC());
                } else {
                    println("  decompile failed: " + res.getErrorMessage());
                }
            }
        }
        ifc.dispose();
        println("done: " + seen.size() + " functions decompiled");
    }
}
