function(rateweaver_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE rateweaver::rateweaver)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 30)
endfunction()

rateweaver_test(rateweaver_core_tests core_tests.cpp)
rateweaver_test(rateweaver_odr_test "odr_main.cpp;odr_peer.cpp")

add_test(NAME rateweaver_superproject
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCONFIG=$<CONFIG>"
        "-DDEPENDENCY_CONFIG=${PROJECT_BINARY_DIR}/dependency-test-config.cmake"
        "-DHOST_JSON=${RATEWEAVER_BUILD_PROVIDERS}" "-DHOST_ECB=${RATEWEAVER_ENABLE_ECB}"
        "-DHOST_KURLYK=${RATEWEAVER_ENABLE_KURLYK}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/superproject.cmake")
# These checks install dependencies and compile independent consumers as well.
set_tests_properties(rateweaver_superproject PROPERTIES TIMEOUT 300)

rateweaver_test(rateweaver_provider_tests provider_tests.cpp)
target_compile_definitions(rateweaver_provider_tests PRIVATE
    RATEWEAVER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(RATEWEAVER_BUILD_PROVIDERS)
    target_link_libraries(rateweaver_provider_tests PRIVATE rateweaver::providers)
    target_compile_definitions(rateweaver_provider_tests PRIVATE RATEWEAVER_TEST_JSON=1)
endif()
if(RATEWEAVER_ENABLE_ECB)
    target_link_libraries(rateweaver_provider_tests PRIVATE rateweaver::ecb)
    target_compile_definitions(rateweaver_provider_tests PRIVATE RATEWEAVER_TEST_ECB=1)
endif()

if(RATEWEAVER_ENABLE_KURLYK AND RATEWEAVER_BUILD_PROVIDERS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(rateweaver_kurlyk_http_tests kurlyk_http_tests.cpp)
    target_link_libraries(rateweaver_kurlyk_http_tests PRIVATE rateweaver::kurlyk rateweaver::providers)
    target_compile_definitions(rateweaver_kurlyk_http_tests PRIVATE
        KURLYK_AUTO_INIT=0 KURLYK_WEBSOCKET_SUPPORT=0 KURLYK_AUTH_SUPPORT=0 KURLYK_OAUTH_SUPPORT=0)
    if(RATEWEAVER_ENABLE_ECB)
        target_link_libraries(rateweaver_kurlyk_http_tests PRIVATE rateweaver::ecb)
        target_compile_definitions(rateweaver_kurlyk_http_tests PRIVATE RATEWEAVER_TEST_ECB=1)
    endif()
    set_target_properties(rateweaver_kurlyk_http_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    add_test(NAME rateweaver_kurlyk_http_tests COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/http_server.py"
        --exe $<TARGET_FILE:rateweaver_kurlyk_http_tests> --timeout 90)
    set_tests_properties(rateweaver_kurlyk_http_tests PROPERTIES TIMEOUT 100)
    add_test(NAME rateweaver_native_http_manual COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/http_server.py"
        --exe $<TARGET_FILE:rateweaver_kurlyk_http_tests> --timeout 90 --manual)
    set_tests_properties(rateweaver_native_http_manual PROPERTIES TIMEOUT 100)
    if(TARGET rateweaver_provider_refresh)
        add_test(NAME rateweaver_provider_refresh_example COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/http_server.py"
            --exe $<TARGET_FILE:rateweaver_provider_refresh> --url-path /etag --timeout 30)
        set_tests_properties(rateweaver_provider_refresh_example PROPERTIES TIMEOUT 40)
    endif()
endif()

# Every public header is the first and only include in its own translation
# unit. This also catches stale domain umbrellas and accidental include order.
file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/*.hpp")
set(header_sources)
foreach(header IN LISTS public_headers)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}/include" "${header}")
    if(relative MATCHES "providers/(cdn|cbr|json_detail)" OR relative STREQUAL "rateweaver/providers.hpp")
        if(NOT RATEWEAVER_BUILD_PROVIDERS)
            continue()
        endif()
    endif()
    if(relative MATCHES "providers/ecb" AND NOT RATEWEAVER_ENABLE_ECB)
        continue()
    endif()
    if((relative STREQUAL "rateweaver/runtime.hpp" OR relative MATCHES "runtime/kurlyk")
       AND NOT RATEWEAVER_ENABLE_KURLYK)
        continue()
    endif()
    string(MAKE_C_IDENTIFIER "${relative}" identifier)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${identifier}.cpp")
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include <${relative}>\n" @ONLY)
    list(APPEND header_sources "${source}")
endforeach()
add_library(rateweaver_public_headers OBJECT ${header_sources})
target_link_libraries(rateweaver_public_headers PRIVATE rateweaver::rateweaver)
set_target_properties(rateweaver_public_headers PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(RATEWEAVER_BUILD_PROVIDERS)
    target_link_libraries(rateweaver_public_headers PRIVATE rateweaver::providers)
endif()
if(RATEWEAVER_ENABLE_ECB)
    target_link_libraries(rateweaver_public_headers PRIVATE rateweaver::ecb)
endif()
if(RATEWEAVER_ENABLE_KURLYK)
    target_link_libraries(rateweaver_public_headers PRIVATE rateweaver::kurlyk)
    target_compile_definitions(rateweaver_public_headers PRIVATE
        KURLYK_AUTO_INIT=0 KURLYK_WEBSOCKET_SUPPORT=0 KURLYK_AUTH_SUPPORT=0 KURLYK_OAUTH_SUPPORT=0)
endif()

if(RATEWEAVER_INSTALL)
    add_test(NAME rateweaver_installed_consumer
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DDEPENDENCY_CONFIG=${PROJECT_BINARY_DIR}/dependency-test-config.cmake"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install_consumer"
            "-DWORK_DIR=${PROJECT_BINARY_DIR}/install-consumer"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
    set_tests_properties(rateweaver_installed_consumer PROPERTIES TIMEOUT 300)
    if(RATEWEAVER_BUILD_PROVIDERS OR RATEWEAVER_ENABLE_ECB OR RATEWEAVER_ENABLE_KURLYK)
        add_test(NAME rateweaver_installed_components
            COMMAND ${CMAKE_COMMAND}
                "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
                "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DDEPENDENCY_CONFIG=${PROJECT_BINARY_DIR}/dependency-test-config.cmake"
                "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install_components"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/install-components"
                "-DCONSUMER_TYPE=components"
                "-DCONSUME_JSON=${RATEWEAVER_BUILD_PROVIDERS}" "-DCONSUME_ECB=${RATEWEAVER_ENABLE_ECB}"
                "-DCONSUME_KURLYK=${RATEWEAVER_ENABLE_KURLYK}"
                "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
                "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCONFIG=$<CONFIG>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
        set_tests_properties(rateweaver_installed_components PROPERTIES TIMEOUT 300)
    endif()
endif()

# Persist lists as CMake data so semicolons survive the CTest subprocess boundary.
set(dependency_build_dirs)
get_directory_property(dependency_source_dirs DIRECTORY "${PROJECT_SOURCE_DIR}" SUBDIRECTORIES)
foreach(dependency time-shield-cpp json pugixml curl)
    set(dependency_source "${PROJECT_SOURCE_DIR}/external/${dependency}")
    if(dependency_source IN_LIST dependency_source_dirs)
        get_directory_property(dependency_build DIRECTORY "${dependency_source}" BINARY_DIR)
        list(APPEND dependency_build_dirs "${dependency_build}")
    endif()
endforeach()
file(WRITE "${PROJECT_BINARY_DIR}/dependency-test-config.cmake"
    "set(DEPENDENCY_BUILD_DIRS [==[${dependency_build_dirs}]==])\n"
    "set(DEPENDENCY_PREFIX_PATH [==[${CMAKE_PREFIX_PATH}]==])\n")
