# NPU Simulator Test Root

Store every CPU+NPU simulator test program outside `gem5/` in this directory.

| Directory | Contents |
|---|---|
| `baremetal/common/` | Shared bare-metal linker script and startup code |
| `baremetal/vcu/` | VCU bare-metal smoke and backpressure programs |
| `baremetal/cube/` | Cube bare-metal smoke program |
| `baremetal/multinpu/` | Multi-NPU bare-metal smoke program |
| `systemc/<feature>/` | Isolated SystemC/TLM unit and component tests |
| `integration/<feature>/` | gem5-to-SystemC end-to-end test drivers and checkers |
| `fixtures/<feature>/` | Test-only binary inputs such as `a.bin` and `b.bin` |
| `reference/common/` | Shared VCD and log checkers |
| `reference/vcu/` | VCU smoke and backpressure generators |
| `reference/cube/` | Cube smoke generators |
| `reference/multinpu/` | Multi-NPU smoke generators |
| `scripts/` | Ubuntu 24.04 one-command validation scripts (`build_gem5.sh`, `vcu.sh`, `cube.sh`, `multinpu.sh`) |

Do not add generated build directories, simulator outputs, VCD files, or binary results to this source tree. Put generated outputs under `npu-tests/build/<module>/` and keep them out of source-change logging.

Gem5/SystemC implementation and test source changes remain subject to `npu-work/change-log.md`; Markdown, fixture data, scripts, and generated outputs are excluded from that log.
