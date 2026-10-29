# Naming and directory reminder

## Planned naming conventions

* **Prose project titles.** Keep the display names **Target Layout** and **Target Layout Extended** strictly for prose, documentation titles, and repository headings where the project itself is named.
* **Lowercase identifiers in code and CMake.** Never use capital letters for project names or target aliases in code or CMake scripts. Use `target_layout` and `target_layout_extended` in `project()`, CMake target aliases such as `target_layout::text`, C++ namespaces, and binary names.
* **Target root directory.** Drop the `src` prefix and rename `srcTargets/` to `targets/` across both repositories, including `CMakeLists.txt`, `CMakePresets.json`, helper scripts, and documentation files.
* **Lowercase directory and file convention.** Keep CMake helper directories and target folders in clean lowercase such as `targets/` and `cmake/` rather than mixed case `srcTargets/` or `Cmake/`.
