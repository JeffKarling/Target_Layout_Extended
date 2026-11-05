# Target Layout Extended

[![CI](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/ci.yml/badge.svg)](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/ci.yml)
[![Documentation](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/docs.yml/badge.svg)](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/docs.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/JeffKarling/Target_Layout_Extended/badge)](https://securityscorecards.dev/viewer/?uri=github.com/JeffKarling/Target_Layout_Extended)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/License-MIT--0-green.svg)](LICENSE)

*'Enterprise implementation on top of a strict component design.'*

This repository builds directly on **[Target Layout](https://github.com/JeffKarling/Target_Layout)**. Where the base project proves the core rule in its minimal form, **Target Layout Extended** shows how a physical component design scales in production.

This project is **opinionated** and only supports Linux OS and development environment. See the documentation for more details.

## Production Extensions

* **Robust error isolation.** Centralized Lippincott handlers automatically enforce boundary contracts across components, converting internal failures into external well defined return codes. Pre defined contract log detailes is also generated with structured information into systemd-journald.
* **Custom logging library.** A thin logging library, `journal`, delivers high performance structured logging and diagnostics features for a Lakos component architecture design.`journal` is thighly integrated with Linux systemd-journald providing component contractually defined logs.
* **Logg based post-mortem diagnostics.** Boundary failures is captured with exact throw points and call sequence directly into system journals with zero runtime performance penalty enabling automated back end debugging analysis.
* **Performance regression protection.** Built in component benchmarking automatization and performance regression detection. In addition static variables defines optmization cost to value tradeoff for self-driving component self-optimizing loops.
* **Cargo-style build consistency.** Centralized configuration templates establish workspace-wide compiler flags, analysis rules, and warnings while allowing individual components to define local overrides cleanly.
* **Centralized build presets.** Standardized CMake presets is configured to use GCC and Clang compilers automating local compilation. Presets includes unit tests, sanitizers, and static analysis.
* **Production ready continuous integration.** CMake presets translate directly into automated GitHub Actions workflows, guaranteeing identical verification between developer workstations and external hosted CI/CD runners.
* **GitHub project infrastructure.** Github Actions example exemplifies CI/CD workflows deliverning enterprise high quality best practizes for verification, automation, and integration.

## Detailed documentation

For deep architectural rationale and implementation walkthroughs, visit the [documentation site](https://jeffkarling.github.io/Target_Layout_Extended/).

## License

Licensed under [MIT-0](LICENSE).
