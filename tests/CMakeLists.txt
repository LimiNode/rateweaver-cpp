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
rateweaver_test(rateweaver_runtime_edge_tests runtime_edge_tests.cpp)

add_test(NAME rateweaver_superproject
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/superproject.cmake")
set_tests_properties(rateweaver_superproject PROPERTIES TIMEOUT 120)

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

if(RATEWEAVER_INSTALL)
    add_test(NAME rateweaver_installed_consumer
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install_consumer"
            "-DWORK_DIR=${PROJECT_BINARY_DIR}/install-consumer"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
    set_tests_properties(rateweaver_installed_consumer PROPERTIES TIMEOUT 120)
    if(RATEWEAVER_BUILD_PROVIDERS OR RATEWEAVER_ENABLE_ECB)
        add_test(NAME rateweaver_installed_components
            COMMAND ${CMAKE_COMMAND}
                "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
                "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install_components"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/install-components"
                "-DCONSUMER_TYPE=components"
                "-DCONSUME_JSON=${RATEWEAVER_BUILD_PROVIDERS}" "-DCONSUME_ECB=${RATEWEAVER_ENABLE_ECB}"
                "-DJSON_DIR=${nlohmann_json_DIR}" "-DXML_DIR=${pugixml_DIR}"
                "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
                "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCONFIG=$<CONFIG>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
        set_tests_properties(rateweaver_installed_components PROPERTIES TIMEOUT 120)
    endif()
endif()
