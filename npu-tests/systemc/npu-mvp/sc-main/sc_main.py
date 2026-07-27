import sys

import m5
from m5.objects import Root, SystemC_Kernel

kernel = SystemC_Kernel()
root = Root(full_system=True, systemc_kernel=kernel)

m5.systemc.sc_main(*sys.argv)

m5.instantiate(None)
m5.simulate(m5.MaxTick)

result = m5.systemc.sc_main_result()
if result.code != 0:
    m5.util.panic("sc_main return code was %d." % result.code)
