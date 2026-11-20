cmake_minimum_required(VERSION 3.20)
project(rateweaver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# An embedded build must not acquire tests, examples or provider dependencies.
set(RATEWEAVER_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(RATEWEAVER_IS_TOP_LEVEL ON)
endif()
option(RATEWEAVER_BUILD_TESTS "Build deterministic offline tests" ${RATEWEAVER_IS_TOP_LEVEL})
option(RATEWEAVER_BUILD_EXAMPLES "Build the offline conversion example" OFF)
option(RATEWEAVER_BUILD_PROVIDERS "Enable JSON rate providers" OFF)
option(RATEWEAVER_ENABLE_ECB "Enable the optional ECB XML provider" OFF)
option(RATEWEAVER_ENABLE_KURLYK "Enable native asynchronous kurlyk refresh" OFF)
option(RATEWEAVER_BUILD_LIVE_SMOKE "Build and register opt-in live HTTPS provider smoke" OFF)
set(RATEWEAVER_LIVE_CA_FILE "" CACHE FILEPATH "Optional CA bundle for the opt-in HTTPS smoke")
option(RATEWEAVER_INSTALL "Generate install rules and CMake package" ON)

find_package(Threads REQUIRED)
add_library(rateweaver INTERFACE)
add_library(rateweaver::rateweaver ALIAS rateweaver)
target_compile_features(rateweaver INTERFACE cxx_std_17)
target_include_directories(rateweaver INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(rateweaver INTERFACE Threads::Threads)

if(RATEWEAVER_BUILD_PROVIDERS)
    if(NOT TARGET nlohmann_json::nlohmann_json)
        find_package(nlohmann_json 3.11 CONFIG REQUIRED)
    endif()
    add_library(rateweaver_providers INTERFACE)
    add_library(rateweaver::providers ALIAS rateweaver_providers)
    set_target_properties(rateweaver_providers PROPERTIES EXPORT_NAME providers)
    target_link_libraries(rateweaver_providers INTERFACE
        rateweaver::rateweaver nlohmann_json::nlohmann_json)
endif()

if(RATEWEAVER_ENABLE_ECB)
    if(NOT TARGET pugixml::pugixml)
        find_package(pugixml 1.13 CONFIG REQUIRED)
    endif()
    add_library(rateweaver_ecb INTERFACE)
    add_library(rateweaver::ecb ALIAS rateweaver_ecb)
    set_target_properties(rateweaver_ecb PROPERTIES EXPORT_NAME ecb)
    target_link_libraries(rateweaver_ecb INTERFACE rateweaver::rateweaver pugixml::pugixml)
endif()

if(RATEWEAVER_ENABLE_KURLYK)
    include(cmake/rateweaverKurlykDependency.cmake)
    rateweaver_resolve_kurlyk(TRUE)
    add_library(rateweaver_kurlyk INTERFACE)
    add_library(rateweaver::kurlyk ALIAS rateweaver_kurlyk)
    set_target_properties(rateweaver_kurlyk PROPERTIES EXPORT_NAME kurlyk)
    target_link_libraries(rateweaver_kurlyk INTERFACE
        rateweaver::rateweaver rateweaver_deps::kurlyk)
endif()

if(RATEWEAVER_INSTALL)
    set(RATEWEAVER_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/rateweaver")
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/rateweaver")
    install(TARGETS rateweaver EXPORT rateweaverTargets)
    install(EXPORT rateweaverTargets NAMESPACE rateweaver::
        DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
    if(RATEWEAVER_BUILD_PROVIDERS)
        install(TARGETS rateweaver_providers EXPORT rateweaverProviderTargets)
        install(EXPORT rateweaverProviderTargets NAMESPACE rateweaver::
            DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
    endif()
    if(RATEWEAVER_ENABLE_ECB)
        install(TARGETS rateweaver_ecb EXPORT rateweaverEcbTargets)
        install(EXPORT rateweaverEcbTargets NAMESPACE rateweaver::
            DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
    endif()
    if(RATEWEAVER_ENABLE_KURLYK)
        install(TARGETS rateweaver_kurlyk EXPORT rateweaverKurlykTargets)
        install(EXPORT rateweaverKurlykTargets NAMESPACE rateweaver::
            DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
        install(FILES cmake/rateweaverKurlykDependency.cmake
            DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
    endif()
    configure_package_config_file(cmake/rateweaverConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/rateweaverConfig.cmake"
        INSTALL_DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/rateweaverConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rateweaverConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/rateweaverConfigVersion.cmake"
        DESTINATION "${RATEWEAVER_CMAKE_INSTALL_DIR}")
endif()
if(RATEWEAVER_BUILD_LIVE_SMOKE)
    if(NOT RATEWEAVER_ENABLE_KURLYK OR NOT RATEWEAVER_BUILD_PROVIDERS OR NOT RATEWEAVER_ENABLE_ECB)
        message(FATAL_ERROR "Live smoke requires RATEWEAVER_ENABLE_KURLYK, RATEWEAVER_BUILD_PROVIDERS and RATEWEAVER_ENABLE_ECB")
    endif()
    add_executable(rateweaver_live_providers examples/live_providers.cpp)
    target_link_libraries(rateweaver_live_providers PRIVATE
        rateweaver::kurlyk rateweaver::providers rateweaver::ecb)
    target_compile_definitions(rateweaver_live_providers PRIVATE
        KURLYK_AUTO_INIT=0 KURLYK_WEBSOCKET_SUPPORT=0 KURLYK_AUTH_SUPPORT=0 KURLYK_OAUTH_SUPPORT=0)
    enable_testing()
    add_test(NAME rateweaver_live_providers COMMAND rateweaver_live_providers "${RATEWEAVER_LIVE_CA_FILE}")
    set_tests_properties(rateweaver_live_providers PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS live)
endif()

if(RATEWEAVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(RATEWEAVER_BUILD_EXAMPLES)
    add_executable(rateweaver_convert examples/convert.cpp)
    target_link_libraries(rateweaver_convert PRIVATE rateweaver::rateweaver)
endif()
