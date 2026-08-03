from m5.objects.SystemC import SystemC_ScModule
from m5.objects.Probe import ProbeListenerObject
from m5.params import *


class NpuCluster(SystemC_ScModule):
    type = "NpuCluster"
    cxx_class = "npu_mvp::NpuCluster"
    cxx_header = "dev/npu/npu_cluster.hh"

    npu_count = Param.UInt8(4, "Number of NPU instances in the broadcast cluster")
    npu_dispatch_id = Param.UInt64(0, "Direct Xai dispatch target id")
    gm_phys_base = Param.UInt64(0x0000000000000000, "NPU GM physical base")
    gm_size = Param.UInt64(2 * 1024 * 1024 * 1024, "NPU GM byte size")
    gm_page_size = Param.UInt64(4096, "Sparse GM page size")
    ub_phys_base = Param.UInt64(0x0000000100000000, "NPU UB physical base")
    ub_size = Param.UInt64(512 * 1024, "NPU UB byte size")
    l1_phys_base = Param.UInt64(0x0000000100080000, "NPU L1 physical base")
    l1_size = Param.UInt64(512 * 1024, "NPU private L1 byte size")
    l0a_phys_base = Param.UInt64(0x0000000100100000, "NPU L0A physical base")
    l0a_size = Param.UInt64(64 * 1024, "NPU private L0A byte size")
    l0b_phys_base = Param.UInt64(0x0000000100110000, "NPU L0B physical base")
    l0b_size = Param.UInt64(64 * 1024, "NPU private L0B byte size")
    l0c_phys_base = Param.UInt64(0x0000000100120000, "NPU L0C physical base")
    l0c_size = Param.UInt64(64 * 1024, "NPU private L0C byte size")
    mte_max_transfer_bytes = Param.UInt64(
        1024 * 1024, "Maximum bytes in one MTE command"
    )

    max_vl = Param.UInt32(64, "Maximum NPU vector length")
    vector_register_count = Param.UInt32(32, "NPU vector register count")
    vector_register_bytes = Param.UInt32(256, "Bytes per NPU vector register")

    scheduler_queue_depth = Param.UInt32(32, "Scheduler ingress queue depth")
    mte4_queue_depth = Param.UInt32(32, "MTE4 queue depth")
    mte1_queue_depth = Param.UInt32(32, "MTE1 queue depth")
    mte2_queue_depth = Param.UInt32(32, "MTE2 queue depth")
    vcu_queue_depth = Param.UInt32(32, "VCU queue depth")
    cube_queue_depth = Param.UInt32(32, "Cube queue depth")
    fixpipe_queue_depth = Param.UInt32(32, "Fixpipe queue depth")
    file_io_queue_depth = Param.UInt32(32, "Simulator file I/O queue depth")
    niu_queue_depth = Param.UInt32(32, "NIU command queue depth")
    niu_tx_queue_depth = Param.UInt32(64, "NIU TX packet queue depth")
    niu_rx_queue_depth = Param.UInt32(64, "NIU RX packet queue depth")
    noc_link_queue_depth = Param.UInt32(16, "NOC packet queue depth per directed link")
    noc_packet_bytes = Param.UInt32(128, "NOC packet payload bytes")
    noc_link_latency_cycles = Param.UInt32(1, "NOC per-link latency in NPU cycles")
    noc_bytes_per_cycle = Param.UInt32(128, "NOC per-link payload bandwidth")

    enable_sim_file_io = Param.Bool(False, "Enable simulator-only file I/O commands")
    sim_file_io_root = Param.String("", "Root directory for file I/O fixture files")

    scheduler_dispatch_delay = Param.Latency("1ns", "Scheduler dispatch delay")
    mte4_setup_delay = Param.Latency("4ns", "MTE4 setup delay")
    mte1_setup_delay = Param.Latency("4ns", "MTE1 setup delay")
    mte2_setup_delay = Param.Latency("4ns", "MTE2 setup delay")
    cube_setup_delay = Param.Latency("1ns", "Cube setup delay")
    fixpipe_setup_delay = Param.Latency("1ns", "Fixpipe setup delay")
    file_io_setup_delay = Param.Latency("1ns", "file I/O setup delay")

    mte4_bytes_per_ns = Param.Float(16.0, "MTE4 transfer bandwidth")
    mte1_bytes_per_ns = Param.Float(16.0, "MTE1 transfer bandwidth")
    mte2_bytes_per_ns = Param.Float(16.0, "MTE2 transfer bandwidth")
    cube_fma_per_ns = Param.Float(128.0, "Cube FP32 FMA throughput")
    fixpipe_bytes_per_ns = Param.Float(16.0, "Fixpipe transfer bandwidth")
    file_io_bytes_per_ns = Param.Float(16.0, "file I/O bandwidth")
    vcu_bytes_per_ns = Param.Float(16.0, "VCU load/store bandwidth")
    vadd_elements_per_ns = Param.Float(16.0, "VCU vector-add throughput")

    vcd_trace_file = Param.String("", "Optional NPU VCD trace output basename")
    vcd_trace_cycle_ticks = Param.Tick(
        0, "CPU cycle period in gem5 ticks for NPU VCD cycle alignment"
    )


class NpuCpuVcdProbe(ProbeListenerObject):
    type = "NpuCpuVcdProbe"
    cxx_class = "npu_mvp::NpuCpuVcdProbe"
    cxx_header = "dev/npu/npu_cpu_trace.hh"

    cluster = Param.NpuCluster("NPU cluster that owns the shared VCD trace")
