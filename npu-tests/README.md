# NPU Simulator Test Root

Store every CPU+NPU simulator test program outside `gem5/` in this directory.

| Directory | Contents |
|---|---|
| `baremetal/<feature>/` | RISC-V bare-metal NPU test programs and assembler macros |
| `systemc/<feature>/` | Isolated SystemC/TLM unit and component tests |
| `integration/<feature>/` | gem5-to-SystemC end-to-end test drivers and checkers |
| `fixtures/<feature>/` | Test-only binary inputs such as `a.bin` and `b.bin` |
| `reference/<feature>/` | Expected outputs and reference-data generators |
| `scripts/` | Ubuntu 24.04 one-command validation scripts |

Do not add generated build directories, simulator outputs, VCD files, or binary results to this source tree. Put generated outputs under `npu-tests/build/<feature>/` and keep them out of source-change logging.

Gem5/SystemC implementation and test source changes remain subject to `npu-work/change-log.md`; Markdown, fixture data, scripts, and generated outputs are excluded from that log.
