# Target Layout Extended

*'The component is the CMake target, extended for production.'*

This repository builds directly on **[Target Layout](https://github.com/JeffKarling/Target_Layout)**. Where the base project demonstrates the core rule in its smallest possible form, **Target Layout Extended** shows how a physical component scales in a Linux C++ project with boundary exception handling, structured `systemd-journald` catalogs, isolated hardware counter benchmarking, score driven optimization, multi compiler presets, static analysis, and runtime sanitizers.

```text
targets/
├── CMakeLists.txt                <-- Auto-registers every subdirectory as a component
├── journal/                      <-- systemd-journald structured logging & Lippincott helper
├── perf_scope/                   <-- Linux perf_event_open counters & score curve evaluator
├── item_schema/                  <-- Top-level Protobuf code generation component
├── database_manager/             <-- Canonical extended component
│   ├── CMakeLists.txt
│   ├── database_manager.cpp      <-- Private implementation + Lippincott boundary catch
│   ├── database_helper.hpp       <-- Private helper header hidden from consumers
│   ├── database_helper.cpp       <-- Private helper translation unit
│   ├── database_manager.catalog  <-- systemd message catalog documenting boundary IDs
│   ├── database_manager/
│   │   └── database_manager.hpp  <-- Public contract (#include <database_manager/database_manager.hpp>)
│   └── database_managerTEST/     <-- Colocated GoogleTest suite
├── tbb_algos/
│   ├── ...
│   ├── tbb_algosTEST/            <-- Colocated GoogleTest suite
│   └── tbb_algosBENCH/           <-- Hardware counter benchmark & birth baseline.json
├── db_export/
└── app/                          <-- Composition executable following the exact same rule
```

### Quick verification across GNU GCC and LLVM Clang

```bash
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
```

### Run an isolated component benchmark and score evaluation

```bash
cmake --build --preset GNU_Custom_Release --target tbb_algosBENCH_run
```

Licensed under [MIT-0](LICENSE).
