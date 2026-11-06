# Mandatory test updates on code changes

## Core requirement

Every code modification, interface redesign or bug fix must include immediate updates to the corresponding unit tests. Engineers must never leave test suites out of sync with modified component implementations.

Engineers must update tests proactively without waiting for reminders after modifying a component.

## Update existing tests for signature changes

When a function signature or API contract changes, all call sites in unit tests must be updated in the same step. Test suites must reflect the latest interface design without depending on deprecated overloads.

## Add new tests for new behavior and edge cases

When introducing a new feature or refactoring, engineers must proactively add dedicated test cases. Tests must cover typical happy paths, custom configurations, boundary edge cases and fallback behavior.

Compile time assertions using `static_assert` should be added alongside runtime tests whenever an interface supports `constexpr` evaluation.

## Compile and execute verification

Engineers must compile the modified targets and execute the test suite to verify success. All tests in the project must pass before presenting the work as complete.

Running `ctest` or the component test binary confirms that no regressions were introduced.

## Test colocation under the one name rule

In this repository, each component possesses its own dedicated test target located inside `targets/<name>/<name>TEST/`. Engineers must place component specific tests directly inside the owning component test directory.

## Dual testing scopes

Testing in Target Layout Extended covers two distinct scopes.

**Public contract verification.** Tests primarily exercise the public interface in `<name>/<name>.hpp` from an external caller perspective. This validates interface contracts, input parameter validation, boundary firewall exceptions and expected return values without coupling tests to private implementation details.

**Internal invariant verification.** When components implement complex internal state machines, private helper translation units or `detail` namespaces, tests may verify internal mechanics directly. This ensures that internal algorithms operate correctly without exposing implementation details to public consumers.
