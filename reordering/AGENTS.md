# AGENTS.md — Intel DLB 2.0 Reordering Reverse-Engineering Project

## 1. Mission and instructions for Codex

Two-person NC State microarchitecture security and reverse-engineering course project: characterize **effective capacity, allocation granularity, backpressure and sharing of ordered-event tracking/reordering resources in Intel DLB 2.0** on `artemisia`. Aim for a reproducible, novel finding, not a promised grade or just a functional demo.

- Inspect existing code, results and `docs/STATUS.md` first; preserve working code and immutable raw measurements. Implement the earliest incomplete milestone in small, verified increments.
- Verify actual local Intel headers/examples/driver before using APIs, flags, limits or register layouts; no invented interfaces/results. If hardware/source unavailable, provide commands for the student; clearly label offline tests.
- Explain code, exact build/run commands, expected vs actual results, and competing hypotheses. Cite source path/symbol/line or document page/figure. Mark claims DOCUMENTED, OBSERVED, INFERRED, or HYPOTHESIS. Patents are not proof of implementation; generations differ.
- Maintain `docs/STATUS.md`, `docs/API_MAP.md`, `docs/DECISIONS.md` and `docs/literature.md`. Record changes, actual tests, results, blockers, and next action at each session. Never fabricate measurements or claim unrun tests succeeded.
- Prioritize ordered capacity and sequence-allocation controls; optional security work only after establishing mechanism and permissions. Strong findings distinguish models of capacity, recovery and sharing. Novelty must be checked against documentation and prior research; negative results are valid.

## 2. Platform and permission boundaries

- Host `artemisia`, user `pgpatel` (Windows/VS Code, Linux SSH). Device `/dev/dlb0`; expected DLB 2.0, verify actual generation. Earlier ACL showed `user:pgpatel:rw-`, `mask::rw-`; device open/scheduling not yet verified.
- Native release `dlb_linux_src_release_8.14.0.txz`, expected path `~/dlb-research/dlb/libdlb`. Verify build status. Prefer native libdlb over DPDK initially.
- Sources: `Intel_DLB_2_0_Partner_Setup_Steps_1-5.pdf`, professor announcement `Pasted markdown.md`, previous `DLB_2_0_Reordering_Research_Kit.zip` (README, probe, experiment matrix, trace template, analysis script). Reuse if available; this file is self-contained.
- Shared host: only authorized owned workloads/resources. Coordinate saturation and concurrency with professor/admin. No sudo, driver install/rebind/reset, ACL/kernel tuning, global sysfs writes or device-wide resource changes (including global sequence-number groups) without admin approval. Never reset other domains/device. Start small, stay within approved budgets; no unattended or indefinite stress.
- Finite counts, bounded retries and deadlines, stop flag, release intentional hold on timeout, drain safely, join threads and clean up only owned resources using verified APIs. External process timeout is a last safeguard. Log failed/incomplete runs. Security evaluation restricted to consenting synthetic test workloads under team control; never target unrelated users or private data.

## 3. Setup and read-only discovery

On the Linux host:

```bash
hostname
id
getfacl /dev/dlb0
test -r /dev/dlb0 && echo "READ: YES" || echo "READ: NO"
test -w /dev/dlb0 && echo "WRITE: YES" || echo "WRITE: NO"
uname -a
lscpu
lscpu -e
lspci -nnk
command -v numactl >/dev/null && numactl --hardware
ls -l /dev/dlb*
readlink -f /sys/class/dlb/dlb0/device 2>/dev/null || true
cc --version
```

The sysfs path is a candidate only; discover actual path through the driver/class/device listing. Record NUMA and CPU affinity constraints (`taskset -pc $$`, `/proc/self/status`), driver version and available device resources. Do not assume CPU numbering or unrestricted affinity. If a kit probe exists, inspect before executing.

If libdlb is not yet present, obtain the exact release from Intel's official download source (verify filename/checksum if provided). In Windows PowerShell, not the remote shell:

```powershell
$archive = "$HOME\Downloads\dlb_linux_src_release_8.14.0.txz"
Test-Path $archive
scp $archive pgpatel@artemisia:~/
```

Use actual SSH hostname/VPN if necessary. On artemisia:

```bash
mkdir -p ~/dlb-research
cd ~/dlb-research
tar -xJf ~/dlb_linux_src_release_8.14.0.txz
cd dlb/libdlb
make
ls -l libdlb.so examples/ldb_traffic
```

Do not overwrite an existing source/build tree blindly. Inspect actual archive layout if different. Build uses userspace source; does not require installing a driver. Inspect example source for usage and bounded parameters before running it. Determine linker flags and runtime library search path from actual build files; use a per-command `LD_LIBRARY_PATH` or appropriate rpath as necessary rather than system-wide changes. Verify library with `ldd` where relevant.

```bash
cd ~/dlb-research/dlb/libdlb
find . -maxdepth 5 -type f | sort | head -100
grep -RniE 'ordered|sched_type|seq_num|num_sn|sn_group|forward|release|credit|cq_depth|token|history' \
  --include='*.c' --include='*.h' --include='*.md' . | head -150
```

Also inspect the extracted driver source for generation-specific resource allocation, group/slot accounting and ioctl semantics if supplied. Search more targeted symbols after locating headers. Use local source rather than guessing. If source is outside workspace, record its exact path; never silently modify vendor code. A local git/source hash or release archive checksum helps establish reproducibility.

## 4. References and evidence plan

These URLs/page references were supplied by the professor or project guide; their contents beyond the announcement have not been independently verified here. Use actual PDFs/local source when available. Record publication/revision, page/figure, quoted/paraphrased fact, hypothesis, proposed test and caveat in `docs/literature.md`.

| Source | Focus |
|---|---|
| ISCA 2023 accelerator tutorial: https://reesekuper.com/intel-site/index.html | PDF viewer pages 20–39; p.26 DLB processing flow, p.28 atomic, p.32 libdlb, pp.36–38 measurements. |
| Intel patent US12375408B2: https://patents.google.com/patent/US12375408B2/en | Figs.19–20 queue/order buffering, Fig.22 detailed datapath, Figs.25A–D scheduling modes, Figs.32–34 credits/completion, Fig.35 control words, Fig.24 resource allocation, Figs.26/29 application pipelines. |
| Intel DLB 2.5 Programmer Guide 639481 rev.0.4: https://cdrdv2.intel.com/v1/dl/getContent/639481 | Queue, port, event/HCW, credit, flow, sequence-number and domain concepts. Cross-check every generation-specific fact against 2.0. |
| Intel quick-start: https://www.intel.com/content/www/us/en/support/articles/000096647.html | Driver and device verification. |
| DPDK DLB eventdev manual: https://doc.dpdk.org/guides/eventdevs/dlb2.html | Ordering/forwarding semantics and resource limits, noting PMD-specific behavior and version. |
| Local Intel native libdlb 8.14.0 and driver source | Primary implementation interface; search sequence-number groups, queue allocation, histories, credits, hardware generation constants, API completion and error handling. |
| Intel product brief: https://cdrdv2-public.intel.com/793345/optimize-network-performance-better-business-results-pb.pdf | Motivation: up to 96% lower application latency for Istio ingress gateway at same throughput; NOT a general per-event DLB result. |
| Envoy DLB integration: https://www.envoyproxy.io/docs/envoy/v1.30.5/configuration/other_features/dlb | Optional application discussion; verify which scheduling modes it actually uses before associating it with reordering. |

Professor's explicit goals: identify an internal component; infer capacity, allocation, indexing/arbitration or stall behavior; establish sharing boundaries; reconstruct the observable pipeline; explain performance benefit; investigate potential leakage using controlled workloads; report competing explanations and uncertainty. Figure 22 labels producer ports (2210), reorder logic (2220), internal queue-element storage (2216), QID priority arbiters (2222), consumer arbiters (2224), dequeue logic (2226), and consumer queues (2212). These are patent labels, not confirmed silicon layout. Figures 18/23/35 and 32–34 provide alternative mechanisms to consider. Use diagram hypotheses, not proof by patent.

## 5. Hardware concepts and capacity units

DLB offloads event scheduling, not CPU work or OS thread scheduling. Software chooses queue, scheduling type, priority and metadata; hardware dispatches to eligible ports. Event descriptors carry opaque data/pointers; do not assume DLB reads pointed-to payload.

- Producer port submits descriptors. Load-balanced (LDB) queues distribute to linked ports; directed (DIR) queues use fixed association. Consumer queues (CQs) deliver events; depth/tokens regulate delivery.
- Ordered scheduling permits out-of-order CPU processing while supported forwarding preserves defined order. Verify destination/command semantics; this is not a CPU instruction ROB. Atomic mode serializes same-flow events within applicable scope; optional independent-flow control.
- Credits, CQ tokens, sequence-number slots, worker history/inflight entries and context completion are distinct. Token return may not release ordering context. Forward may implicitly release context; avoid double release; verify per-port dequeue-order requirements.
- Domains allocate logical resources, not necessarily independent physical paths.

Measure separately: (1) accepted source descriptors not delivered at sink, (2) worker-delivered ordered contexts still incomplete (only visible portion), (3) accepted later forwards not yet visible while oldest context held, (4) configured sequence allocation, credit, CQ, history and storage limits. These use different units. A 64-event submission ceiling could be credits, while a 64-event ordering window could be a configured allocation, not a hidden physical ROB size. An order context need not equal one stored descriptor. Report effective capacity, scope or bounds; do not claim physical entries/bytes without independent evidence.

**Priority: sequence-number resources.** Inspect actual 2.0 headers/driver/examples/capability queries for per-queue sequence allocation, group modes, slots, history/inflight and limits. Determine which knobs are per-queue/domain/global/fixed/unobservable, and whether allocation fails at configuration or runtime. Document source-derived group/slot accounting rather than claiming it as novel black-box discovery. Do not change global modes without approval. Sweep supported per-queue sizes. If boundary tracks allocation, report that documented/configured relationship; pursue unexpected secondary limits, recovery or partitioning instead. Never assume 2.5 values or unsupported knob names.

## 6. Proposed repository structure

Adapt to existing project; do not destroy vendor sources or user work.

```text
AGENTS.md
README.md                      # build, run, reproduce, quickstart
Makefile or CMakeLists.txt     # verified native-libdlb paths
src/                           # native C/C++ benchmark
include/                       # local helpers, no copied vendor ABI guesses
scripts/                       # environment capture, bounded runner, sweeps
analysis/                      # Python analysis and plot generation
tests/                        # unit tests for logic, parsers; integration separately
configs/                       # explicit supported experiment configurations
results/raw/<run_id>/          # immutable trace, config, stdout/stderr, status
results/processed/             # derived CSV and statistics
results/figures/               # plots
docs/                         # status, evidence, design, decisions, report
```

Prefer C11 or C++17 (choose based on existing code and library examples), pthreads or existing project concurrency, and Python 3 for analysis. No unnecessary dependencies. Expose library path/device through CLI/build options, not hard-coded personal paths. Keep vendor source and large raw logs out of version control if appropriate; retain run manifests and small reproducible examples. Do not commit credentials, private host details beyond necessary project metadata, or fabricated sample data mixed with actual results.

## 7. Native benchmark requirements

```text
producer -- NEW --> Q0 (ordered LDB or matched unordered)
                       |   |   |   |
                      W0  W1  W2  W3 (linked worker ports)
                       \   |   |   /
                      supported ordered-context FORWARD
                              |
                        Q1 (single sink path)
                              |
                          sink/collector
```

Start with 1 producer, 2 workers and 1 sink, then 4 workers. One producer establishes enqueue order; use a single sink configuration that does not add reordering. Verify DIR or LDB output compatibility. Each role owns port per thread-safety rules; workers forward using correct dequeued context/port. NEW events unrelated to the ordered context or release-only tests do not exercise intended ordered forwarding.

- Query resources; configure domain, pools, Q0/Q1, ports, links, history/CQs and start domain per verified API. Log requested/actual values, IDs, return codes and failures.
- Producer generates unique monotonically increasing IDs (separate run ID); preserve through forward. Count only API-accepted events, handle partial batches. Finite total and outstanding budget.
- Worker receiving E0 (not necessarily W0) acknowledges hold-start and withholds correct completion/forward while others proceed. Verify token policy and context lifetime. Controller/deadline releases hold; never indefinite. Follow per-port release rules. Batch 1 initially.
- Sink continuously drains, records ID and timestamp, returns tokens/releases as required. Verify no duplicates/loss, accepted work fully drains. Avoid double release.
- Use atomics, start barrier, verified affinity, preallocated per-thread tracing; no shared logging locks or printf in timed loops. Use bounded retries/stop flags. On abort stop submissions, release hold, drain/owned-domain recovery, join and close in reverse order. Signal handler only sets flag.

Suggested project-owned CLI (not vendor flags): `--device`, `--mode ordered|unordered|atomic`, `--events`, `--workers`, `--batch-size`, `--hold-id`, `--hold-us`, `--hold-policy time|controlled`, `--hold-timeout-ms`, `--outstanding-cap`, `--cq-depth`, `--credits`, `--sequence-allocation`, `--history-entries`, `--cpu-map`, `--run-id`, `--seed`, `--output-dir`, `--timeout-ms`, `--dry-run`. Only expose supported settings; validate arguments and requested/actual config; never silently substitute unordered. Default small bounded run e.g. 16 events, 2 workers, batch 1, no hold.

Offline tests: argument parser, counter/ID reconciliation, timestamp calculations, timeout and partial-batch logic; label mock tests. Hardware: bounded smoke, basic queue send/receive, 8–16 then 100/1000 events if permitted; verify IDs, order where guaranteed, release/cleanup. Ordered baseline must show actual out-of-order worker completion with E0 hold. Unordered is not guaranteed to produce inversion in every run.

## 8. Experiment roadmap (gates in order)

All numbers below are proposed starting points, not verified device specifications. Adapt to supported values and approved budget. Save every run including failures. Do not proceed without correctness/cleanup.

### E0 — Setup and resource discovery

Save environment/capability snapshot; confirm generation and successful device open/query. Inspect/run bounded native example; record exact args/exit/cleanup. Make API map for domain, ports/links, ordered config, sequence group/slot allocation, credit/CQ/history, forward/release/token rules. Safe read-only free-resource before/after query if exposed; unrelated workload changes confound attribution. Prefer scheduled quiet window.

### E1 — Functional baseline

8–16 IDs, 2 workers, batch 1, single sink. No hold then E0 hold; compare ordered/unordered with matched output path and budgets. Require later worker forward before E0, full ID reconciliation, correct ownership and documented downstream order. Separate source order, worker delivery, completion, accepted forward, and sink arrival. Plot timeline. If mismatch, resolve API semantics before continuing.

### E2 — Delay and head-of-line blocking

Sweep requested E0 delay 0, 10, 100, 1000, 10000 us (adapt to noise); measure actual duration. Test early vs middle held ID, with identical work, resources and CPU wait behavior in unordered control. Acknowledge hold-start, keep Q1 draining. Count deliveries, accepted forwards and downstream arrivals during hold; identify whether accepted forwards wait for visibility. Record recovery after release. H1: ordered downstream constraint causes HOL delay. This validation alone is not novel.

### E3 — Effective ordering window / capacity

Hold earliest context using bounded controller/deadline, not solely a fixed delay (injection speed can masquerade as capacity). After hold acknowledgement, inject later work under finite budget. Sweep targets 1,2,4,8,16,32,64,128,256 and larger supported values only if authorized; refine near knee (N±1/4/8). For each point, count source accepted, worker delivered, forward accepted, sink visible during verified hold; locate first stalled transition. Report both including/excluding held event, distinguish later forwarded-but-not-visible from unscheduled source work. Predefine no-progress timeout and check robustness. Release and verify drain. Compare no-hold/unordered. If producer throttled by application outstanding cap based on sink completions, an ordered sink stall naturally hits that cap; use bounded total budget above suspected knee and observe actual API/worker progress to infer hardware limit. If no plateau, report lower bound. H2: ordering-specific resource may limit progress; not proof of physical entries.

### E4 — Critical configuration controls

Document every requested/actual resource and unit. Unsupported knobs are limitations, not guesses. Independently vary:

1. Credits (including forwarding/output accounting), fixed sequence allocation/CQ/history. Does knee shift or producer/forward stall?
2. CQ depth, legal token policy, worker count, history/inflight capacity, fixed credits and sequence allocation. Does delivery stall depend on these? Sink continuously drains.
3. Supported per-queue sequence allocations, other budgets sufficiently large within permission. Does measured boundary track, quantize or plateau relative to allocation? No global writes. If groups exist, inspect source/config to establish group/slot accounting and allowed combinations; do not assert unverified assignment. Allocation-time failure differs from runtime saturation.
4. Ordered vs matched unordered; optional independent-flow atomic if feasible. Keep destination and budgets comparable; document mode-specific differences.
5. Batch size and hold position after baseline. Verify partial batch accounting.

Interpretation: knee tracking sequence allocation = configured window, not novel physical capacity; tracking credits = enqueue resource; tracking CQ/history = delivery resource; fixed ordered-only plateau after controls = candidate internal ordering bound, still not proof of exact physical store. If output capacity affects knee, reconsider hypothesis. Record remaining confounders.

### E5 — Allocation and sharing (approval required)

Two small cooperating team-owned workloads with disjoint IDs/ports. Compare alone vs concurrent separate queues in one domain, then separate owned domains if supported/approved. Match total offered work, CPU/core budgets, resource allocations and sink behavior; measure per-workload and aggregate behavior. Distinguish configured partitions from runtime sharing; same shared-credit domain cannot establish cross-domain effect. If group assignments are verified, compare allowed configurations. Compare matched unordered and CPU-only controls, SMT siblings vs distinct cores and separate device/socket only if available. Account for cache/memory/NUMA/OS effects. No unrelated users. Conclude only tested scope: queue/group/domain/device path or unresolved.

### E6 — Recovery and performance

Plot cumulative forward acceptance and sink arrivals aligned to actual hold release; measure drain time and burstiness across sequence allocations, workers, batch sizes, output capacities. Compare no-hold/unordered. Software polling is not exact hardware issue width. Explain concurrent processing + ordered forwarding vs HOL tradeoff. Optional software ordered collector or serial-worker baseline with matched semantics, cores, offered rate, work, batching, throughput and latency. 96% Istio result out of primary scope. Seek a discriminating result: allocation-only vs extra shared limit, downstream visibility stall with continued forwards vs dispatch stall, or a reproducible recovery pattern not explained by configured resources.

### E7 — Optional controlled isolation/security study

Only after demonstrated sharing and authorization, evaluate latency isolation using consenting team-owned synthetic workloads with known inputs. Professor asks for victim/observer, private property, sharing boundary, timing signal and evidence of inference, not mere slowdown. Use synthetic data, held-out runs, matched CPU-only controls and uncertainty; never observe real tenants/private service activity, bypass permissions or recover real secrets. If unavailable or negative, report limits and focus on characterization. Do not expand to operational exploitation.

### E8 — Evidence and reporting

Reconcile all results, reproduce key runs, compare documentation/prior research, state bounds/alternative explanations and novelty. Produce figures and documented/observed/inferred pipeline. Report negative results and unsupported controls honestly.

## 9. Measurement and analysis

Use `clock_gettime(CLOCK_MONOTONIC_RAW)` where supported; calibrate overhead; no unverified cross-core TSC subtraction. Pin/check affinity; warm up and drain; preallocate thread-local logs; compare low-instrumentation throughput. Keep data/address/work constant except independent variable.

Long trace schema (one transition per row):

```csv
run_id,trial,event_id,role,thread_id,cpu_id,queue_id,port_id,mode,op,phase,t_ns,status,return_code,retry_count,batch_id
```

Ops: `source_submit` attempt/accepted/failed, `worker_recv`, `work_done`, `hold_start`, `hold_end`, `forward` attempt/accepted/failed, `sink_recv`, `cq_token_return`, `context_release`, `abort`, `drain_done`. Missing/unobserved != zero; if API combines actions, log actual semantics. Kit wide CSV may require converter; never silently mix schemas.

Manifest per run: ID, experiment, date, code hash, source/driver/device, CPU/NUMA, available/allocated resources, requested/actual queue sequence/credit/CQ/history, links/ports, mode, workers/affinity, batch, event budget/outstanding policy, hold ID/requested/actual duration, seed, clock/warmup/timeout, exact command, errors/retries, exit and validity. Unknown values explicit.

Metrics: submit-call time, total retry time, worker-delivery latency, work duration, forward acceptance latency, sink visibility latency, end-to-end latency from submit-start, throughput, post-release drain, stage counts during hold. API acceptance not hardware completion. Worker/sink may observe event before producer/forwarder logs return: flag negative deltas, do not clamp; use pre-call time as alternative with clear interpretation. At t, outstanding = accepted NEW minus sink deliveries for 1:1 forward; pending-forward proxy = accepted FORWARD minus sink deliveries; unfinished worker contexts according to verified lifecycle. Reconcile event IDs, partial batches, validity and causal timing; these are not physical occupancy counters.

Pilot then aim for 20–30 independent repeats per key config; randomized order/seeds. Use run-level stats, not treating correlated events as independent. Median, p95/p99 if enough samples, dispersion/intervals (bootstrap over runs where appropriate), sample counts, exclusions/failed runs. Predefine no-progress criterion and validation; label exploratory thresholds; no cherry-picking. Preserve raw logs and generate plots from data.

Required figures from actual data: (1) small event timeline ordered/unordered; (2) latency vs actual hold duration, tails/spread; (3) accepted/delivered/forwarded/visible counts vs offered target, annotated first stall/config cap; (4) measured knee vs allocation, credits, CQ/history/workers separately; (5) cumulative recovery aligned to hold release; (6) isolated vs concurrent owned configurations; (7) optional performance/isolation plots. Do not call proxies physical ROB occupancy. Hypothetical/mock data separately labeled and excluded from final results.

## 10. Hypothesis and competing-explanation matrix

Maintain this table in `docs/DECISIONS.md`, adding actual evidence and status. Do not resolve a hypothesis without measurement or directly applicable documentation.

| Candidate | Expected observation | Discriminating control |
|---|---|---|
| Configured ordered sequence window | Held-event boundary follows per-queue sequence allocation | Vary supported allocation with credits/CQ/history held fixed; inspect driver resource accounting |
| Reorder-specific additional capacity | Ordering-only plateau not explained by known allocations | Compare modes and multiple permitted configs; isolate accepted-forward vs delivery bottleneck |
| Enqueue credits/QE storage | Source/forward acceptance stalls, boundary follows credits or ordinary storage demand | Credit sweep; output capacity; equivalent unordered workload |
| CQ capacity / history | Worker delivery stalls even if producer accepts; depends on token/release/history policy | CQ/history/worker sweep; ensure sink and other workers continue |
| Output queue / sink | Forward pressure or downstream stall follows output capacity | Continuous sink drain; matched output queue and credit accounting |
| Application outstanding cap | Producer stops exactly at target without attempting more | Distinguish intentional cap from API backpressure; finite larger submission budget |
| CPU/OS or instrumentation | Boundary/latency moves with CPU placement, logging, hold strategy or polling rate | Pin threads; matched CPU-only and no-hold controls; low-instrumentation run |
| Shared ordering allocation | Capacity or recovery differs when two queues use a verifiable common allocation | Compare isolated vs concurrent owned configs and documented allocations; matched overall budgets |
| Shared non-ordering path | Ordered and unordered both show contention, possibly across domains | Mode controls and CPU/cache/memory/NUMA controls; avoid overattribution |

## 11. Report structure and novelty checks

Write the report with these sections: motivation and focused question; platform and permission scope; documented DLB pipeline and event lifecycle; patent-derived hypotheses; benchmark design and correctness; experiments and results; competing explanations; inferred hardware model; performance implications; security/isolation discussion if tested; limitations; reproducibility and citations.

Mark every box in architecture diagram as DOCUMENTED, OBSERVED, or INFERRED; an internal pipeline register or physical layout cannot be established by end-to-end latency alone. Include exact definitions and units of every capacity number. Quote the 96% claim only as its specific application result (Istio/Envoy ingress at same throughput, 4th Gen Xeon, 6 cores/12 threads per professor announcement), not as DLB's general latency reduction or proof of a reorder optimization.

Before asserting novelty, compare against local source, DLB 2.0 documentation, patent, tutorial, DPDK documentation, and related work available to the students. Record which facts are already documented versus what the experiment adds. A measured configured sequence-number limit is useful validation but not necessarily novel. A more compelling contribution can be a reproducible, controlled explanation of an unexpected boundary, allocation-dependent behavior, recovery pattern, or previously uncharacterized sharing scope. State "consistent with" or "evidence for" when alternative implementations remain possible. No exact physical entry count, byte size, tag width, or SRAM structure claim without independent supporting evidence. If a precise number cannot be inferred, give bounds and the reason.

## 12. Deliverables and collaboration

- Working native benchmark, build instructions and small smoke/correctness tests.
- Reproducible bounded runner and sweep scripts with configuration validation, timeout and unique run directories.
- Environment/capability snapshot and actual API/resource map.
- Raw traces, manifests, documented exclusion rules, processed summaries and plots; never overwrite prior raw results.
- Literature and evidence log, competing hypotheses, status and design decisions.
- Final research report and presentation-ready figures, including limitations and approved security discussion if applicable.

Partner A suggested focus: environment, native library and benchmark, lifecycle correctness, trace instrumentation. Partner B: literature, sequence allocation/source accounting, experiment design, statistics/plots. Both: analyze measurements and review alternative explanations. Adapt assignments to actual student preferences.

## 13. First action for Codex / current status

Initial state from chat: device ACL looks correct for `pgpatel`; actual READ/WRITE shell checks, native build, and hardware smoke success have not been confirmed in the conversation. No capacity or latency measurement has been collected here. External source links were supplied by the professor, not independently verified. Check repository state for newer evidence instead of treating this as permanent status.

On first invocation:

1. List repo and identify whether running locally or on artemisia. Inspect any existing vendor source, setup guide, professor announcement, kit, benchmark and traces.
2. Create/update status and API map without claiming unsupported facts. If remote hardware unavailable, tell the student which exact commands to execute and what outputs are needed. Do not install drivers or guess runtime results.
3. Complete E0 with bounded read-only discovery and approved example, then implement/validate E1. Return exact commands, expected checks and actual evidence. Stop at a blocker rather than proceeding on invalid assumptions.
4. Once E1 is valid, implement held-event protocol (E2/E3), then E4 controls. This is the primary research objective. Do not jump directly to a security extension or a massive sweep.

Every work session should end with: changes made, tests actually run, outcomes (or why unavailable), evidence/paths, limitations, and next smallest actionable step. Keep this AGENTS.md as the durable project charter; put changing experimental state in `docs/STATUS.md`.
