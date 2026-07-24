# Contributing

Thanks for helping improve can-bus-analyzer. Bug reports, feature ideas and pull requests are welcome.

## Build and test

```bash
cmake -S . -B build -DCANBUS_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request also run the sanitizer build and the static analysis used in CI:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCANBUS_SANITIZE=ON -DCANBUS_WERROR=ON
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/*.cpp
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 --inline-suppr -I include -I src --std=c++17 src include
```

No CAN hardware is needed: tests use `tests/fixtures/` and the replay/generator sources.

## Style

- Modern C++17, RAII, no raw `new`/`delete`, `const`-correct, `[[nodiscard]]` on queries, `enum class`.
- Zero warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; `clang-tidy` and `cppcheck` must pass.
- Public headers carry `///` Doxygen comments; keep comments to the non-obvious "why".
- No TODOs, stubs or dead code. Hardware-dependent code needs a simulation path so it can be tested.
- Every behaviour change comes with a test in `tests/`.

## Commits

- Small, logical commits with plain imperative subjects ("Add DBC value tables", not "Added" or "Adds").
- Keep the subject under about 72 characters; explain the reason in the body when it is not obvious.
- Update `CHANGELOG.md` under `Unreleased` for user-visible changes.

## Pull requests

Describe what changed and how you tested it, and keep each pull request focused on one topic.
