# Dependency registry and build ownership

Direct dependencies are Git submodules pinned to published releases. Run
`git submodule update --init` at the recorded superproject commit. Do not use
`--remote`; the gitlinks, not a dependency's default branch, are authoritative.
The HTTP-only standalone profile does not require recursively initializing
kurlyk's optional WebSocket/test submodules.

| Submodule | Release | Exact commit | Used by |
| --- | --- | --- | --- |
| `external/time-shield-cpp` | v2.0.0 | `d5f1203f24341e4146af35d5bb36e762056d181f` | Required Core types, UTC/monotonic clocks, calendar conversion |
| `external/kurlyk` | v1.1.1 | `b62cd53f672c47b74bb14d762ea9dc59a9df19ce` | Optional native async refresh |
| `external/json` | v3.12.0 | `55f93686c01528224f448c19128836e7df245f72` | Optional CDN/CBR JSON parsers |
| `external/pugixml` | v1.16 | `c8033ce9d039e7f9d134877c363397b3cfe20816` | Optional ECB XML parser |
| `external/curl` | curl-8_23_0 | `3163ec065643ba20709f190111f42c3e1fe95273` | Optional libcurl source fallback for HTTP-only kurlyk |

The pinned curl release tag's source header reports `8.23.0-DEV`. Validation
records that observed version string alongside the exact gitlink; it does not
substitute a different revision to alter the display string.

Time Shield 1.x is not supported. Core uses canonical v2 domain headers and
links `time_shield::time_shield` transitively. Installed Core consumers discover
`TimeShield >= 2.0` unless the host already supplies that target. No header-path
probing or alternate clock/calendar implementation chooses a different major.

## Bundled standalone

`RATEWEAVER_DEPENDENCY_MODE=BUNDLED` is the standalone default. Required enabled
dependencies are configured from the checked-out submodules. Existing compatible
targets take precedence even in this mode. Configure all provider examples and
offline tests with:

```sh
git submodule update --init
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DRATEWEAVER_DEPENDENCY_MODE=BUNDLED -DRATEWEAVER_BUILD_EXAMPLES=ON -DRATEWEAVER_BUILD_PROVIDERS=ON -DRATEWEAVER_ENABLE_ECB=ON -DRATEWEAVER_ENABLE_KURLYK=ON
cmake --build build-mingw
ctest --test-dir build-mingw -C Release --output-on-failure
```

The default `RATEWEAVER_CURL_MODE=AUTO` first accepts `CURL::libcurl` or a system
CURL package, then uses the pinned curl source if needed. `SYSTEM` requires a
provided/system CURL target. `BUNDLED` selects the pinned fallback unless a target
already exists. Windows fallback builds static HTTP/HTTPS-only libcurl using
Schannel, without an OpenSSL, Asio or WebSocket download. Linux fallback requires
the system OpenSSL development package and normal C/C++ build tools. Python 3 is
needed only for local HTTP integration tests.

The standalone dependency shim is explicitly named
`RateWeaverKurlykHttp::client`: native kurlyk headers plus CURL/Threads and
HTTP-only compile definitions (`HTTP=1`, WebSocket/auth/OAuth=0, auto-init=0).
It is not an imitation upstream `kurlyk::kurlyk` target. Upstream kurlyk's full
CMake configuration also configures OpenSSL, Asio and Simple-WebSocket-Server
even when only HTTP is needed. The separate shim avoids acquiring those unused
features; its package can be installed for independent HTTP-only consumers.

## Embedded shared dependencies

`RATEWEAVER_DEPENDENCY_MODE=EXISTING` is the embedded default. Provide compatible
targets before `add_subdirectory`, or installed packages through
`CMAKE_PREFIX_PATH`. RateWeaver neither adds duplicate dependencies nor changes
global native feature/startup macros:

```cmake
# The host has configured Time Shield and its chosen optional dependencies.
set(RATEWEAVER_DEPENDENCY_MODE EXISTING CACHE STRING "" FORCE)
set(RATEWEAVER_BUILD_PROVIDERS ON CACHE BOOL "" FORCE)
set(RATEWEAVER_ENABLE_KURLYK ON CACHE BOOL "" FORCE)
add_subdirectory(external/rateweaver-cpp)
target_link_libraries(my_application PRIVATE rateweaver::providers rateweaver::kurlyk)
```

Accepted existing targets are `time_shield::time_shield`,
`nlohmann_json::nlohmann_json`, `pugixml::pugixml` and native `kurlyk::kurlyk`
(or its upstream build target `kurlyk`). A host's full kurlyk target retains
its OpenSSL/CURL, standalone-Asio or Boost.Asio, Simple-WebSocket-Server and
authentication feature ownership. RateWeaver does not configure a second
runtime. Hosts must ensure a single compatible dependency/version/macro graph;
an existing target name alone cannot validate every ABI/feature combination.

## Installed consumers

Use `find_package(rateweaver CONFIG REQUIRED)` for Core; select components
`providers`, `ecb`, `kurlyk` as needed. The consumer discovers dependencies in
their own prefixes; RateWeaver exports no absolute source include directory.
The optional HTTP component accepts an existing native kurlyk target/package or
the separately named `RateWeaverKurlykHttp` package. Installing the complete
bundled build also installs its enabled dependencies; the `RateWeaver` install
component isolates the library so dependencies can occupy separate prefixes.

See [validation](validation.md) for bundled, existing-target and relocated
cross-prefix consumer checks, and the [kurlyk v1.1.1 audit](kurlyk-limitations.md)
for remaining runtime resource/cancellation constraints.
