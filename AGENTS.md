# Agent guidance

- Keep RateWeaver a portable C++17 header-only library. Preserve the existing
  core, graph, aggregation, policy, routing, provider and runtime boundaries.
- Core conversion must work without HTTP, JSON/XML libraries or a background
  thread. Never invent a parity or silently accept stale/rejected rates.
- Preserve quote provenance and publication/retrieval times. Failures have no
  converted amount; zero and negative finite amounts are valid inputs.
- Develop in a dedicated branch and worktree under
  `rateweaver-cpp/.worktree/<task-name>/`. Use a separate build tree per worktree;
  do not edit another agent's checkout or a consumer's vendor/submodule copy.
- Put temporary validation artifacts under `tmp/agent-work/`. On Windows use
  CMake with `MinGW Makefiles`, `gcc` and `g++` for routine validation; use the
  Visual Studio generator for the additional MSVC portability check. Do not use
  Ninja or NMake. Read a host's local machine settings before parallel builds;
  otherwise use at most 8 jobs.
- Tests must remain active in Release builds and use deterministic timestamps.
  Run CTest, individual public-header compilation, and the independent installed
  consumer before handing off a package change. Network access is not a test
  prerequisite.
- Keep code, comments, documentation and Conventional Commit messages in English.
  Document public contracts and material limitations. Do not edit vendored code,
  merge PRs or publish release tags without explicit authorization.
