// Decompile the function containing a given address, so a Windows fault offset can be explained.
// @category Rayman3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) { println("usage: DecompAt <address> [address...]"); return; }
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        println("image base: " + currentProgram.getImageBase());

        for (String a : args) {
            Address addr;
            try {
                addr = toAddr(Long.parseLong(a.replace("0x", ""), 16));
            } catch (Exception e) {
                println("bad address: " + a);
                continue;
            }
            Function f = getFunctionContaining(addr);
            println("=== address " + addr + " -> " + (f == null ? "no function" : f.getName() + " @ " + f.getEntryPoint()));
            if (f == null) { f = getFunctionBefore(addr); if (f != null) println("    (nearest function before: " + f.getName() + " @ " + f.getEntryPoint() + ")"); }
            if (f == null) continue;
            DecompileResults res = ifc.decompileFunction(f, 120, monitor);
            if (res.decompileCompleted()) println(res.getDecompiledFunction().getC());
            else println("    decompile failed: " + res.getErrorMessage());

            // show a little context: the instructions around the address
            println("---- instructions near " + addr + " ----");
            ghidra.program.model.listing.Instruction ins = getInstructionAt(addr);
            if (ins == null) ins = getInstructionAfter(addr);
            for (int i = 0; i < 8 && ins != null; i++) {
                println("    " + ins.getAddress() + "  " + ins.toString());
                ins = ins.getNext();
            }
        }
        ifc.dispose();
    }
}
