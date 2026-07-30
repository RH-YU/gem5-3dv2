# Bare-metal XiangShan runner for local/custom ISA experiments.
#
# This script intentionally lives outside the original XiangShan entrypoints.
# It does not require --generic-rv-cpt and keeps difftest disabled by default.

import argparse
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal
from m5.util.convert import toFrequency

addToPath("../../")

from common import Options
from common import MemConfig
from common import Simulation
from common.Benchmarks import SysConfig
from common.FSConfig import makeBareMetalXiangshanSystem
from common.xiangshan import _finish_xiangshan_system, get_xiangshan_cpu_class


GEM5_TICKS_PER_SECOND = 1_000_000_000_000


def add_local_options(parser):
    parser.add_argument(
        "--baremetal-bin",
        required=True,
        help="Path to the RISC-V bare-metal ELF to load as RiscvBareMetal.bootloader.",
    )
    parser.add_argument(
        "--raw-binary",
        action="store_true",
        help="Treat --baremetal-bin as a flat binary instead of an ELF.",
    )
    parser.add_argument(
        "--reset-vect",
        default="0x80000000",
        help="Reset vector used for --raw-binary. ELF entry overrides this value.",
    )
    parser.add_argument(
        "--enable-npu",
        action="store_true",
        help="Enable direct Xai instruction dispatch to the SystemC NPU.",
    )
    parser.add_argument(
        "--npu-dispatch-id",
        default="1",
        help="Direct-dispatch target id used to bind Xai instructions to the NPU.",
    )
    parser.add_argument(
        "--npu-count",
        type=int,
        default=1,
        help="Number of NPU instances behind the direct-dispatch target.",
    )
    parser.add_argument(
        "--npu-enable-sim-gm-file-io",
        action="store_true",
        help="Enable the simulator-only NPU GM file I/O commands.",
    )
    parser.add_argument(
        "--npu-sim-gm-file-io-root",
        default="",
        help="Root directory for simulator-only NPU GM file I/O fixture files.",
    )
    parser.add_argument(
        "--dramsim3-output-dir",
        default="",
        help="Directory for DRAMSim3-generated statistic files.",
    )
    parser.add_argument(
        "--npu-vcd-trace-file",
        default="",
        help=(
            "Optional NPU VCD trace output basename. A .vcd suffix is added "
            "by SystemC if omitted."
        ),
    )
    parser.add_argument(
        "--npu-cpu-type",
        default="TimingSimpleCPU",
        help=(
            "CPU model used by this NPU bare-metal runner. Defaults to "
            "TimingSimpleCPU for in-order NPU timing debug."
        ),
    )
    parser.set_defaults(cpu_clock="1GHz")


def set_baremetal_defaults(args):
    args.xiangshan_system = True
    args.enable_difftest = False
    args.difftest_ref_so = None
    args.enable_h_gcpt = False
    args.raw_cpt = False
    args.gcpt_restorer = None
    args.generic_rv_cpt = None
    args.enable_trace_mode = False

    args.bp_type = "DecoupledBPUWithBTB"
    args.cpu_type = args.npu_cpu_type
    args.caches = True
    args.l2cache = True
    args.l3cache = not args.no_l3cache
    args.l1_to_l2_pf_hint = True
    args.l2_to_l3_pf_hint = not args.no_l3cache

    if args.mem_type == "DRAMsim3" and args.dramsim3_ini is None:
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
        args.dramsim3_ini = os.path.join(
            root_dir,
            "ext/dramsim3/xiangshan_configs/xiangshan_DDR4_8Gb_x8_3200_2ch.ini",
        )

    if args.mem_type == "Ramulator2" and args.ramulator2_ini is None:
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
        args.ramulator2_ini = os.path.join(
            root_dir, "ext/ramulator2/xs_ramulator_config.yaml"
        )


def select_baremetal_cpu_class(args):
    if args.npu_cpu_type == "XiangshanCore":
        return get_xiangshan_cpu_class(args), True
    return Simulation.getCPUClass(args.npu_cpu_type)[0], False


def cpu_cycle_ticks_from_clock(clock):
    frequency_hz = toFrequency(clock)
    if frequency_hz <= 0:
        fatal("CPU clock must be positive: %s", clock)
    return max(1, int(round(GEM5_TICKS_PER_SECOND / frequency_hz)))


def attach_private_l1_caches(cpu, membus):
    cpu.addPrivateSplitL1Caches(
        Cache(
            size="16kB",
            assoc=2,
            tag_latency=1,
            data_latency=1,
            response_latency=1,
            mshrs=4,
            tgts_per_mshr=8,
            is_read_only=True,
        ),
        Cache(
            size="16kB",
            assoc=2,
            tag_latency=1,
            data_latency=1,
            response_latency=1,
            mshrs=8,
            tgts_per_mshr=8,
        ),
    )
    cpu.connectCachedPorts(membus.cpu_side_ports)


def finish_in_order_baremetal_system(args, system, cpu_class, ruby):
    if ruby:
        fatal("The NPU in-order bare-metal runner currently supports classic memory only.")
    if args.smt:
        fatal("The NPU in-order bare-metal runner currently supports one thread per CPU.")

    system.cache_line_size = args.cacheline_size
    system.voltage_domain = VoltageDomain(voltage=args.sys_voltage)
    system.clk_domain = SrcClockDomain(
        clock=args.sys_clock,
        voltage_domain=system.voltage_domain,
    )
    system.cpu_voltage_domain = VoltageDomain()
    system.cpu_clk_domain = SrcClockDomain(
        clock=args.cpu_clock,
        voltage_domain=system.cpu_voltage_domain,
    )
    system.cpu = [
        cpu_class(clk_domain=system.cpu_clk_domain, cpu_id=i)
        for i in range(args.num_cpus)
    ]

    for cpu in system.cpu:
        cpu.mmu.pma_checker = PMAChecker(
            uncacheable=[AddrRange(0, size=0x80000000)]
        )
        cpu.mmu.functional = args.functional_tlb
        cpu.createThreads()
        cpu.createInterruptController()
        attach_private_l1_caches(cpu, system.membus)
        print("Create threads for NPU bare-metal CPU ({})".format(type(cpu)))

    MemConfig.config_mem(args, system)
    return system


def build_baremetal_system(args):
    if buildEnv["TARGET_ISA"] != "riscv":
        fatal("baremetal_xiangshan.py requires a RISCV gem5 build.")

    test_cpu_class, use_xiangshan_finish = select_baremetal_cpu_class(args)
    ruby = bool(hasattr(args, "ruby") and args.ruby)
    num_threads = args.num_cpus * (2 if args.smt else 1)

    system = makeBareMetalXiangshanSystem(
        "timing",
        SysConfig(mem=args.mem_size),
        None,
        np=args.num_cpus,
        ruby=ruby,
        num_threads=num_threads,
    )
    system.num_cpus = args.num_cpus
    system.xiangshan_system = True
    system.enable_difftest = False
    system.restore_from_gcpt = False

    system.workload.bootloader = args.baremetal_bin
    system.workload.raw_bootloader = args.raw_binary
    system.workload.xiangshan_cpt = False
    system.workload.reset_vect = int(args.reset_vect, 0)

    if use_xiangshan_finish:
        return _finish_xiangshan_system(args, system, test_cpu_class, ruby)
    return finish_in_order_baremetal_system(args, system, test_cpu_class, ruby)



def configure_dramsim3_output_dir(system, args):
    output_dir = args.dramsim3_output_dir
    if args.mem_type != "DRAMsim3" or not output_dir:
        return

    os.makedirs(output_dir, exist_ok=True)
    for mem_ctrl in getattr(system, "mem_ctrls", []):
        if hasattr(mem_ctrl, "filePath"):
            mem_ctrl.filePath = output_dir


def attach_npu(system, args):
    if not buildEnv.get("USE_SYSTEMC", False):
        fatal("--enable-npu requires a gem5 binary built with USE_SYSTEMC=1.")

    if not hasattr(system, "membus"):
        fatal("NPU attachment requires system.membus.")

    dispatch_id = int(args.npu_dispatch_id, 0)
    if dispatch_id <= 0:
        fatal("--npu-dispatch-id must be positive.")
    if args.npu_count <= 0 or args.npu_count > 4:
        fatal("--npu-count must be in the range [1, 4].")

    for cpu in system.cpu:
        for isa in cpu.isa:
            isa.npu_dispatch_id = dispatch_id

    cpu_cycle_ticks = cpu_cycle_ticks_from_clock(args.cpu_clock)

    system.npu = NpuCluster(
        npu_dispatch_id=dispatch_id,
        npu_count=args.npu_count,
        enable_sim_gm_file_io=args.npu_enable_sim_gm_file_io,
        sim_gm_file_io_root=args.npu_sim_gm_file_io_root,
        vcd_trace_file=args.npu_vcd_trace_file,
        vcd_trace_cycle_ticks=cpu_cycle_ticks,
    )
    if args.npu_vcd_trace_file:
        system.npu_cpu_vcd_probe = NpuCpuVcdProbe(
            manager=system.cpu[0],
            cluster=system.npu,
        )


if __name__ == "__m5_main__":
    parser = argparse.ArgumentParser()
    Options.addCommonOptions(parser, configure_xiangshan=True)
    Options.addXiangshanCommonOptions(parser)
    add_local_options(parser)

    args = parser.parse_args()
    set_baremetal_defaults(args)

    if not os.path.exists(args.baremetal_bin):
        fatal("Bare-metal binary does not exist: %s", args.baremetal_bin)

    if args.enable_difftest:
        fatal("This local bare-metal runner keeps difftest disabled.")

    Simulation.setMemClass(args)

    test_sys = build_baremetal_system(args)
    configure_dramsim3_output_dir(test_sys, args)
    if args.enable_npu:
        attach_npu(test_sys, args)
        root = Root(full_system=True, system=test_sys, systemc_kernel=SystemC_Kernel())
    else:
        root = Root(full_system=True, system=test_sys)

    Simulation.run_vanilla(args, root, test_sys, None)
