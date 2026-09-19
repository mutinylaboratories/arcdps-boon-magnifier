"""Print constructor tail and the disassembly of a function (operand sizes)."""
import sys
from find_strings import open_view
bv = open_view(sys.argv[1])
for a in sys.argv[2:]:
    fn = bv.get_function_at(int(a, 16))
    print("=== %s disassembly" % fn.name)
    for bb in fn.basic_blocks:
        for line in bb.get_disassembly_text():
            print("  %#x  %s" % (line.address, str(line)))
