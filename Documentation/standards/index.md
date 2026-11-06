# Engineering standards

## Overview

Target Layout Extended enforces strict physical and logical design standards across all components. Every target is an isolated library governed by clear physical boundaries, consistent interface conventions and automated verification gates.

These standards apply to both human engineers and automated development workflows.

## Core principles

**Physical encapsulation.** Components expose public contracts solely through `<name>/<name>.hpp` headers. Internal implementation details remain private to the translation unit.

**Scott Meyers interface design.** Interfaces should be easy to use correctly and hard to use incorrectly. APIs rely on strong typing, explicit conversions and resource safety through RAII.

**Memoryless pipelines.** Algorithms prefer C++23 ranges and views over manual indexing and intermediate allocations. Bounds checks hoist cleanly out of hot loops under standard library hardening.

**Firewall exception handling.** Components enforce strict operational boundaries. Internal faults are caught at boundary firewalls and logged with binary stack traces before returning status values to callers.

**Generous structured logging.** Fast ring buffer logging allows abundant diagnostic traces across all Syslog severities without thread contention or heap allocation.

**Colocated verification.** Unit tests and hardware PMU benchmarks reside directly inside the component directory under `<name>TEST/` and `<name>BENCH/`.
