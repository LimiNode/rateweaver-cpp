# Dependency registry and build ownership

Source dependencies are Git submodules pinned to published releases. Initialize
only those required by the chosen features at the recorded superproject commit.
Do not use `--remote`; the gitlinks, not a dependency's default branch, are authoritative.
The HTTP-only standalone profile does not require recursively initializing
kurlyk's optional WebSocket/test submodules.

| Submodule | Release | Exact commit | Used by |
| --- | --- | --- | --- |
| `external/time-shield-cpp` | v2.0.0 | `d5f1203f24341e4146af35d5bb36e762056d181f` | Required Core types, UTC/monotonic clocks, calendar conversion |
| `external/kurlyk` | v1.1.1 | `b62cd53f672c47b74bb14d762ea9dc59a9df19ce` | Optional native async refresh |
| `external/json` | v3.12.0 | `55f93686c01528224f448c19128836e7df245f72` | Optional CDN/CBR JSON parsers |
| `external/pugixml` | v1.16 | `c8033ce9d039e7f9d134877c363397b3cfe20816` | Optional ECB XML parser |
| `external/curl` | curl-8_23_0 | `3163ec065643ba20709f190111f42c3e1fe95273` | Optional `SOURCE` libcurl build; not needed for MinGW `PREBUILT` |

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
git submodule update --init external/time-shield-cpp external/kurlyk external/json external/pugixml
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DRATEWEAVER_DEPENDENCY_MODE=BUNDLED -DRATEWEAVER_CURL_MODE=PREBUILT -DRATEWEAVER_BUILD_EXAMPLES=ON -DRATEWEAVER_BUILD_PROVIDERS=ON -DRATEWEAVER_ENABLE_ECB=ON -DRATEWEAVER_ENABLE_KURLYK=ON
cmake --build build-mingw
ctest --test-dir build-mingw -C Release --output-on-failure
```

### CURL resolution modes

An existing `CURL::libcurl` always takes precedence in every mode, including
`PREBUILT` and `SOURCE`. RateWeaver then neither downloads nor configures curl.
Without that host target, `RATEWEAVER_CURL_MODE` selects:

| Mode | Windows MinGW x64 | MSVC and Linux |
| --- | --- | --- |
| `AUTO` | Installed package, then pinned shared binary | Installed package, then pinned source build |
| `SYSTEM` | Installed package only; error if absent | Installed package only; error if absent |
| `PREBUILT` | Pinned shared binary; skips system discovery | Clear unsupported-platform error |
| `SOURCE` | Explicit pinned static build with Schannel | Explicit pinned source build; Schannel on MSVC, system OpenSSL on Linux |

The source build requires initialized `external/curl`; MinGW `PREBUILT` and
its `AUTO` binary fallback do not. Other unsupported prebuilt architectures
also fail configuration. `BUNDLED` is a deprecated alias for curl mode `SOURCE`,
not a synonym for the independently named dependency mode `BUNDLED`.
Windows system discovery requires curl 8.11.0 or newer. MSVC never receives the
MinGW `.dll.a` fallback. Python 3 is needed only for local integration tests.

### Pinned MinGW binary

The binary comes from
[`LimiNode/curl-8.22.0_1-win64-mingw`](https://github.com/LimiNode/curl-8.22.0_1-win64-mingw)
at `5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d`, the same immutable pin used
by kurlyk v1.1.1. It supplies curl 8.22.0 headers, `lib/libcurl.dll.a` and
`bin/libcurl-x64.dll`. FetchContent downloads it only when selected; an offline
build can supply its checkout through `FETCHCONTENT_SOURCE_DIR_RW_CURL`.

The original curl-for-win archive is
[`curl-8.22.0_1-win64-mingw.zip`](https://curl.se/windows/dl-8.22.0_1/curl-8.22.0_1-win64-mingw.zip),
SHA-256 `7f23b039f6ea4197362d4468e1a0e71428201222e1bef3b680d5ef7b2aefb714`.
The pinned DLL, import library, license and twelve headers match that archive
byte for byte. DLL SHA-256 is
`10c8871b2188095c26b28767b4f99dc97d0f6fab58a70be47f6679cd9126b13f`.

Both DLL and import objects are x86-64. Runtime reports `LibreSSL/4.3.2`,
not Schannel. The DLL imports Windows system libraries and UCRT API sets;
its third-party libraries are linked internally. This does not establish
support for obsolete Windows versions or arbitrary cross-CRT object exchange.
The runtime regression checks matching header/binary versions, the actual
adjacent loaded DLL, TLS success and certificate/hostname rejection.

### Curl version and security limit

The binary pin deliberately remains 8.22.0; it is not interchangeable with the
8.23.0 source pin or its headers. As of 2026-11-21,
[upstream lists 21 advisories for 8.22.0](https://curl.se/docs/vuln-8.22.0.html)
(one High, twenty Low), fixed in 8.23.0. High
[CVE-2026-92392](https://curl.se/docs/CVE-2026-92392.html) concerns an HTTP/2
upload use-after-free. Built-in feeds use GET requests, but that does not make
the package generally free of vulnerabilities or establish that every advisory
is reachable through RateWeaver.

These fixes justify refreshing the shared binary package. A verified newer
LimiNode binary package was not available during this pass, so the specified
provenance-checked pin is retained with this explicit limitation rather than
substituting an unreviewed DLL. Select an appropriate installed curl or the
pinned `SOURCE` build when the older binary is unsuitable. A separately
verified binary update remains recommended.

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
