# SNN Head three-chip artifacts

This directory contains the frozen PBCA bundle for SNN Head multi-chip scheme
1.0. The five layers are resident at the same time on chips 0, 1, and 2 of one
four-chip PAICORE 2.5 board. Chip 3 is not used by this model. The application
still selects the single-chip artifacts in `../../assets/` unless resident mode
is enabled explicitly; adding this directory does not change that default.

These files are a fixed model-and-board asset set, not a general multi-chip
compiler output contract. They must be used together with the matching runtime,
CPU parameters, audit plan, and firmware identity described below.

## Provenance and immutable identities

The bundle was derived from the application's five single-chip PBCA files. The
adaptation changed placement, routing, hardware thread identity, and output
address mapping for the fixed three-chip layout. It did not retrain the model.
For each layer, `manifest.json` records that parameter and arithmetic bits are
unchanged outside its enumerated allowed word masks.

| Layer | Single-chip source SHA-256 | Three-chip PBCA SHA-256 |
| --- | --- | --- |
| FC1 + LIF | `1edd33d7fe0f95a44b35f08ac29a3be97e996ed907d6c37cbe9b1335b15e82a9` | `44f9f2d1c8e80f5bdbcada23b8d5770d2fa462447db715f2eab8ae4b31fe7417` |
| Block0 + LIF | `31a130ca4a799e8704e4572267c29322f8e3edbe2c32e2d1ef28067e20947428` | `636aa0b30334c75d160479a3551a996cbc6f1e5d75cca60dba1c3e2f8b106bd3` |
| Block1 + LIF | `ed9b3d272baa1187c4dd1d8e3c25381329c639acde0d352f073bd83dd0468b69` | `2b078b74cab52dc329adca4c218047380099e295c0072c839f1ac1333f959093` |
| FC2 | `76edbdf8ffe445b6206d2f721db5918288b7277e69fe7ed05648ac90c0772258` | `5b66f02f615f39f137745488e5c5261bc9544f33e02c8fbe7b7fc3ecde5523c7` |
| FC3 | `d60e156c3a6ed2acb938a8402f3ccc7ddd006500400abeda5a90751356307c1c` | `95b1ce510cae2ccdff507f8dd82b89651eeb57933a18ce038dc344a251336c62` |

The direct SHA-256 of [manifest.json](manifest.json) is
`f633e568829517542cf2124318fb3b17dd8085b8665ea9dd497bcede9b7ab35e`.
The manifest is the detailed source-to-target record, including every model and
configuration coordinate, route, output map, and allowed changed word.

## Fixed placement and receive namespaces

Coordinates below are global PAICORE coordinates. Each layer has
`pipeline_latency = 1`, and the five layers execute serially through a single
active CPU0 session.

| Layer | Chip | Thread ID | Model cores | Configuration targets | Root |
| --- | ---: | ---: | ---: | ---: | --- |
| FC1 + LIF | 0 | 1 | 19 | 19 | `(0,6)` |
| Block0 + LIF | 1 | 2 | 38 | 41 | `(9,2)` |
| Block1 + LIF | 2 | 3 | 38 | 41 | `(8,11)` |
| FC2 | 0 | 4 | 38 | 41 | `(0,2)` |
| FC3 | 0 | 5 | 1 | 1 | `(0,5)` |
| **Total** | **3 chips** | **5 IDs** | **134** | **143** | |

The per-chip configuration-target counts are 61, 41, and 41 for chips 0, 1,
and 2 respectively. The sets are disjoint within this bundle.

The receive mappings also form part of the frozen interface:

| Layer | Frame kind | Output address namespace | Logical channels | Target LCN |
| --- | --- | --- | ---: | ---: |
| FC1 + LIF | DATA | 0 through 1535 | 1536 | 4 |
| Block0 + LIF | DATA | 1536 through 3071 | 1536 | 4 |
| Block1 + LIF | DATA | 3072 through 4607 | 1536 | 4 |
| FC2 | VOLTAGE, four lanes | manifest minimum 0, maximum 6119 | 1536 | 4 |
| FC3 | VOLTAGE, four lanes | relocated logical base 6144, maximum 6150 | 7 | 4 |

The non-overlapping DATA ranges and the relocated FC3 range let the exact
receiver reject delayed frames from another layer. Consumers must validate the
full manifest mapping and matching COMPLETE identity; the minimum/maximum
values in this table are not a substitute for that validation.

## Audit plan

[audit/audit_plan.json](audit/audit_plan.json) and
[audit/snn_head_audit_plan.h](audit/snn_head_audit_plan.h) describe the reads
used to verify the resident configuration and parameter SRAM.

| Identity or coverage item | Value |
| --- | --- |
| Audit-plan file SHA-256 | `68296a9618a44e22dcb9212f8452401959e49a90eaf3a1e654860fd63a239650` |
| Canonical JSON SHA-256 | `df4b80d5bc255137efe3252f1d1cc628a455d865dda8fec73d4e6be98ae01bb0` |
| CORE targets | 143 |
| Parameter SRAM bytes | 8,465,120 |
| Immutable weight bytes | 8,268,288 |
| Query windows | 8,450 |

The canonical hash is computed over compact, key-sorted JSON; it is deliberately
different from the hash of the formatted file. The plan status
`planned_reads_not_board_evidence` describes what the generator had produced at
that point. A plan is an expected-read specification, not proof that a board
returned those values.

Likewise, the manifest status
`candidate_pending_route_board_validation` is the preserved generation-time
status. It documents the conservative state before route and receive tests were
run. The files were not rewritten after testing merely to replace that history
with a success label. Scheme 1.0 board verification is instead bound to the
firmware and validation scope below.

## Scheme 1.0 firmware and validation scope

The final release candidate was built from PAIRV main commit
`ff53d28cd79a6bf7deed9cc56d8d8e07f26376d9`, plus the reviewed resident/runtime
changes. It used `SOC=evalsoc`, `CORE=n307fd`, `DOWNLOAD=ilmflashxip`, resident,
audit, and timing enabled, debug disabled, and a dedicated 4 KiB stack reserve.
The ELF files are evidence bindings and are not stored in this asset directory.

| Frozen object | SHA-256 |
| --- | --- |
| Production ELF | `7573e4dda030e1b67826507243d648a097cc07b451b38c68fa251275ad0bbfd6` |
| Numeric diagnostic ELF | `fdf146a7c3a8c1ea5f494eb7c7d37a2766506bcc810052d63b098bbd81466a83` |
| Frozen source archive | `c86171296809a9f64a32fe81e74a24408adfd249247265b2f5c477db5e5707e6` |
| 178-file source hash manifest | `0ca67bd1496b24de4eb8b88017d3de69246a80e92ddd697bfbfa72e18b9a1fc7` |
| Frozen build report | `ec533a19f91cfd53cb7cb243b81045e9cdc36437deaf07f2d9adfdf6e1ecea95` |

The 2026-09-14 qualification covered this exact asset/runtime/build combination
on one four-chip PAICORE 2.5 board, using chips 0 through 2 and CPU0:

- three fresh software starts, each running the A/B/A request sequence, for nine
  warm eight-timestep calls in total;
- byte-stable A and B result payloads across those starts, matching the earlier
  three-chip pre-cache results;
- nine complete NSNN transactions with valid sequence, shape, length, status,
  header CRC, payload CRC, and 504 finite float32 action values in total;
- zero warm configuration/deployment work in all 45 per-layer timing rows, with
  the expected nine COMPLETE frames and nine RX IRQs in every layer row;
- full startup reads of 143 CORE records and 8,465,120 parameter-SRAM bytes, plus
  immutable-weight reads after each chunk; and
- a separate diagnostic comparison in which all 19 stage CRC streams for eight
  timesteps, three full spike tensors, and the final actions were bit-identical
  to the pre-cache three-chip implementation.

The measured production warm mean was 0.405621189 seconds per complete
eight-timestep chunk, excluding initialization, UART transport, and the
post-inference weight audit. These measurements were software restarts, not
physical cold boots or power cycles.

This validation does not cover arbitrary chip topologies, another model or
artifact set, a different compiler/runtime build, concurrent instances,
long-duration or power-cycle stability, or an end-to-end robot pipeline. The
original model-reference golden differs from this PAICORE 2.5 execution for
some outputs and was retained as reference data rather than used as the board
execution gate. A board-adapted dataset is required before claiming strict
model-level accuracy acceptance.

## Generation and reproduction boundary

This directory publishes the frozen PBCA files, the detailed manifest, and the
audit plan/header. The experimental tools used to create and qualify the bundle
are not a supported or complete generation interface in this asset package.
In particular, the following are not supplied here as a reproducible toolchain:

- the experimental relocation/model builder and serializer (`model.py` and
  `serializer.py`);
- the route-enumeration and fixed-topology checker (`core_routes.py`);
- the audit-plan generator (`audit.py`);
- the firmware freeze/build orchestrator (`warm_build.py`); and
- the internal board runner, Flash backup/restore, preflight, raw-capture, and
  evidence-aggregation harnesses.

The original PAIBox export environment and training inputs are also outside
this directory. Therefore this README does not claim one-command regeneration.
Rebuilding or relocating these artifacts requires the original single-chip
inputs, compatible PAIBox/PAIlib tooling, the omitted migration checks, and new
host plus board validation. Hash-check the five PBCA files and the canonical
audit plan before using this fixed bundle.
