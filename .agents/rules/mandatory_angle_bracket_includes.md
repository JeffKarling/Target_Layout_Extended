# Mandatory angle bracket includes

## Baseline inclusion mandate

Every header include in this repository must use angle brackets `<...>`.

The use of quoted includes `#include "..."` is strictly prohibited. Quoted includes represent an architectural code smell across all components and translation units.

## Why quoted includes are an architectural smell

Target Layout Extended relies on CMake targets to define physical boundaries and compiler search paths. Every header access must be explicitly governed by registered target paths.

Quoted includes break this principle in three critical ways.

**Bypassing CMake search paths.** The preprocessor resolves quoted includes by checking the directory of the current file before consulting compiler search paths. This behavior allows headers to resolve by local disk proximity rather than target declarations.

**Masking missing dependencies.** Quoted includes allow translation units to compile even when target dependencies or file set declarations are missing from `CMakeLists.txt`. This hides architectural defects until downstream packaging or deployment.

**Silent header shadowing.** Quoted includes create lookup ambiguity where a local file can accidentally shadow a header from a dependency or the standard library.

## Target resolution mechanics

Both public interfaces and private internal helpers must resolve through angle brackets.

**Public target contracts.** Public headers expose target qualified paths through CMake file sets. All files including tests and downstream consumers must reference them using the target prefix.

```cpp
#include <journal/journal.hpp>
#include <database_manager/database_manager.hpp>
#include <item_proto/item_proto.pb.h>
```

**Private target helpers.** Private helpers colocated with the implementation file reside within the directory registered by CMake `FILE_SET` base paths. They must also be included using angle brackets.

```cpp
#include <database_helper.hpp>
```

**External dependencies and standard headers.** System libraries and standard library facilities always resolve through angle brackets.

```cpp
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
```

## Anti pattern of falling back to quotes

Agents must never introduce quoted includes to bypass compilation failures.

If an include fails to resolve with angle brackets, that failure indicates an incomplete CMake target configuration. The agent must investigate and correct the CMake target definition, file set base paths, or target link libraries.

## Agent verification checklist

Whenever creating or modifying C++ source or header files, agents must perform verification.

Agents must run a repository scan before concluding any task.

```bash
grep -rn '#include "' targets/
```

The scan must return zero occurrences. Any discovered instance must be investigated and converted to angle brackets immediately.
