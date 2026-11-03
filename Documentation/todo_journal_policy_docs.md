# Todo plan for journal runtime policy documentation

This plan specifies how to document the runtime log policy mechanism exposed by `targets/journal/journal/journal.hpp`. Executing this plan updates `Documentation/journal_developer_guide.md` and creates a standalone technical deep dive in `Documentation/journal_policy_example.md`. It also aligns `.agents/rules/generous_structured_logging.md` while leaving `mkdocs.yml` untouched.

## Architectural points to emphasize

The documentation must clearly communicate four foundational design rules.

* **Out of the box readiness.** The `journal` component already exposes a complete public policy API in `targets/journal/journal/journal.hpp`. No changes to `journal` are needed when adding external runtime log control.
* **Strict last caller wins model.** The `journal` component enforces no authentication or session tracking. It treats `journal::policy` purely as plain data, and the most recent valid call to `journal::activate_policy` dictates consumer filtering behavior.
* **Physical levelization and separation of concerns.** The `journal` target sits at the bottom of the physical dependency graph and must never link networking or serialization libraries. External control belongs in a separate higher level component under `targets/` that links `${PROJECT_NAME}::journal` and calls its public header functions.
* **Protocol neutrality.** Any transport or trigger mechanism can drive the public policy API. A POSIX `SIGUSR1` and `SIGUSR2` signal listener is only one zero dependency example. Production systems can equally wrap the exact same `journal` functions inside a Varlink service or a D-Bus interface. They can also expose them through an HTTP REST endpoint or a Unix domain socket controller.

## Step 1. Update Documentation/journal_developer_guide.md

Replace the short `## Flight recorder and runtime policies` section in `Documentation/journal_developer_guide.md` with a concise overview that covers these elements.

* **Default flight recorder behavior.** Explain that `debug` records stay in the 2048 slot ring buffer with zero `systemd-journald` I/O until a record at or above `lookbehind_trigger` flushes recent breadcrumbs with `LOG_LOOKBEHIND=1`.
* **Public policy control surface.** Introduce `journal::policy` and `journal::component_override` alongside the five public control and telemetry functions in `targets/journal/journal/journal.hpp`. Those functions include `journal::is_valid_policy` and `journal::activate_policy` for applying changes. They also include `journal::active_policy`, `journal::reset_default_policy`, and `journal::statistics_snapshot` for inspection and reset.
* **External adapter capability.** State that `journal` uses a protocol agnostic last caller wins model. A higher level component can expose runtime policy changes over Varlink, D-Bus, or HTTP REST as well as POSIX signals.
* **Deep dive link.** Add a direct markdown link to `[journal_policy_example.md](journal_policy_example.md)` for the full struct reference and component adapter examples.

## Step 2. Create Documentation/journal_policy_example.md

Create `Documentation/journal_policy_example.md` as a standalone technical reference. Because it is omitted from `nav` in `mkdocs.yml`, include a return link to `[Journal developer guide](journal_developer_guide.md)` at both the top and bottom of the page.

Structure `Documentation/journal_policy_example.md` into the following sections.

### Navigation link and architectural overview

Include a Mermaid flowchart illustrating how an external client communicates with a higher level adapter component in `targets/`. Show the adapter calling the public policy functions on `targets/journal/` while the background consumer thread reads the atomic generation counter.

Explain why `journal` implements a strict last caller wins contract. It leaves transport protocols, wire parsing, and access control to higher level components.

### Pointer free policy and component_override reference

Show the complete definitions of `journal::component_override` and `journal::policy` from `targets/journal/journal/journal.hpp`.

* **Fixed size character storage.** Explain that `component_override` stores the target name in `std::array<char, 32>` rather than pointers or `std::string`. This pointer free layout lets an external IPC or HTTP handler populate a candidate struct directly from decoded wire buffers without heap allocations or dangling references.
* **Severity strides.** Document `stride_by_level` across all eight Syslog levels. A value of `0` keeps records in the ring only, while `1` emits every record and `N` emits every Nth record.
* **Congestion throttling.** Document `congestion_threshold_pct` and `congestion_info_stride` for automatic thinning of `notice` and `info` records when ring backlog rises.
* **Lookbehind window.** Document `lookbehind_trigger` and `lookbehind_slots` for automatic breadcrumb flushing on faults.
* **Per component microscope overrides.** Document the four slot `overrides` array built via `journal::make_component_override` to stream `debug` logs from isolated subsystems without flooding the journal.

### Public validation, activation, and telemetry functions

Document how an external control component uses the five public functions in `targets/journal/journal/journal.hpp`.

* **Pre activation validation.** Call `journal::is_valid_policy` to verify stride ceilings, percentage bounds, and severity values. It also checks lookbehind capacity and printable component names before applying untrusted external input.
* **Atomic swapping and reset.** Call `journal::activate_policy` to publish a validated candidate via the atomic generation counter, or call `journal::reset_default_policy` to restore compiled in flight recorder defaults.
* **Live state and health queries.** Call `journal::active_policy` and `journal::statistics_snapshot` to inspect current configuration alongside cumulative `submitted`, `emitted`, and `lost` counters.

### Example 1. Zero dependency POSIX signal adapter component

Emphasize upfront that `SIGUSR1` and `SIGUSR2` represent just one lightweight example chosen because POSIX signals require no third party libraries. Point out that the exact same `journal` calls work inside a Varlink, D-Bus, or HTTP REST component.

Show a self contained component example linking `PRIVATE ${PROJECT_NAME}::journal` that waits on `SIGUSR1` and `SIGUSR2` using `sigwait` on a dedicated background thread. When `SIGUSR1` arrives via `kill -USR1 <pid>`, the thread activates a verbose debug policy. When `SIGUSR2` arrives via `kill -USR2 <pid>`, the thread calls `journal::reset_default_policy`. Note why `sigwait` on a dedicated thread is used instead of an asynchronous signal handler to avoid spin lock reentrancy.

### Example 2. Structured key value adapter for Varlink, D-Bus, or HTTP REST

Provide a second example based on `PolicyTest.ExternalControllerCanDriveThePolicyFromPlainKeyValuePairs` in `targets/journal/journalTEST/journalTEST.cpp`. Show how a network or IPC component takes decoded key value fields from a Varlink, D-Bus, or HTTP REST request and populates a `journal::policy` struct. Then show it validating the struct with `journal::is_valid_policy` and activating it with `journal::activate_policy`.

## Step 3. Update .agents/rules/generous_structured_logging.md

Add a brief rule under `## Flight recorder mode and runtime policies` in `.agents/rules/generous_structured_logging.md` stating that `journal` follows a strict last caller wins model and must never embed IPC, networking, or authentication logic. Any external runtime policy interface such as Varlink, D-Bus, or HTTP REST must be implemented as a separate higher level component that links `${PROJECT_NAME}::journal` and calls its public policy API.

## Step 4. Keep mkdocs.yml unchanged and delete this todo file

Do not add `journal_policy_example.md` or this todo file to `mkdocs.yml` so the site navigation menu stays unchanged. Once Steps 1 through 3 are verified, remove `Documentation/todo_journal_policy_docs.md`.
