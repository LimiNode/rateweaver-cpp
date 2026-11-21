cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/run_checked.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/install_dependencies.cmake")
if(NOT CONSUMER_TYPE)
    set(CONSUMER_TYPE consumer)
endif()
if(NOT CONSUMER_TYPE MATCHES "^(consumer|components)$")
    message(FATAL_ERROR "Unknown installed consumer type")
endif()
get_filename_component(expected_work_dir "${BUILD_DIR}/install-${CONSUMER_TYPE}" ABSOLUTE)
get_filename_component(actual_work_dir "${WORK_DIR}" ABSOLUTE)
if(NOT actual_work_dir STREQUAL expected_work_dir)
    message(FATAL_ERROR "Consumer scratch directory must be inside its build tree")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")

# Dependencies and RateWeaver are installed to different, relocated prefixes.
# A package registry or build-tree include must not mask a broken export.
rateweaver_install_test_dependencies("${WORK_DIR}/dependency-stage")
run_checked("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --component RateWeaver
    --prefix "${WORK_DIR}/stage" --config "${CONFIG}")
file(REMOVE_RECURSE "${WORK_DIR}/prefix" "${WORK_DIR}/dependencies" "${WORK_DIR}/build"
    "${WORK_DIR}/missing-time-shield" "${WORK_DIR}/source")
file(RENAME "${WORK_DIR}/stage" "${WORK_DIR}/prefix")
if(EXISTS "${WORK_DIR}/dependency-stage")
    file(RENAME "${WORK_DIR}/dependency-stage" "${WORK_DIR}/dependencies")
endif()
file(COPY "${CONSUMER_DIR}/" DESTINATION "${WORK_DIR}/source")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/clean_runtime.cmake" DESTINATION "${WORK_DIR}/source")
file(GLOB_RECURSE exports "${WORK_DIR}/prefix/*.cmake" "${WORK_DIR}/dependencies/*.cmake")
if(NOT exports)
    message(FATAL_ERROR "No installed RateWeaver package exports were found")
endif()
foreach(export IN LISTS exports)
    file(READ "${export}" contents)
    string(FIND "${contents}" "${SOURCE_DIR}" source_reference)
    if(NOT source_reference EQUAL -1)
        message(FATAL_ERROR "Installed export references source tree: ${export}")
    endif()
endforeach()
set(platform_args)
if(GENERATOR_PLATFORM)
    list(APPEND platform_args -A "${GENERATOR_PLATFORM}")
endif()
set(component_args)
if(CONSUMER_TYPE STREQUAL "components")
    list(APPEND component_args "-DCONSUME_JSON=${CONSUME_JSON}" "-DCONSUME_ECB=${CONSUME_ECB}"
        "-DCONSUME_KURLYK=${CONSUME_KURLYK}")
else()
    list(APPEND component_args -DRATEWEAVER_TEST_OPTIONAL_DEPENDENCIES_UNAVAILABLE=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_kurlyk=ON -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_RateWeaverKurlykHttp=ON)
endif()
file(WRITE "${WORK_DIR}/prefixes.cmake"
    "set(CMAKE_PREFIX_PATH [==[${WORK_DIR}/prefix;${WORK_DIR}/dependencies;${DEPENDENCY_PREFIX_PATH}]==] CACHE STRING \"Consumer package prefixes\")\n")
run_checked("${CMAKE_COMMAND}" -S "${WORK_DIR}/source" -B "${WORK_DIR}/build"
    -C "${WORK_DIR}/prefixes.cmake" -G "${GENERATOR}" ${platform_args}
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF ${component_args})
run_checked("${CMAKE_COMMAND}" --build "${WORK_DIR}/build" --config "${CONFIG}")
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${WORK_DIR}/build" -C "${CONFIG}" --output-on-failure)
if(CONSUMER_TYPE STREQUAL "consumer")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK_DIR}/source" -B "${WORK_DIR}/missing-time-shield"
        -C "${WORK_DIR}/prefixes.cmake" -G "${GENERATOR}" ${platform_args}
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
        -DCMAKE_DISABLE_FIND_PACKAGE_TimeShield=ON
        RESULT_VARIABLE missing_result OUTPUT_VARIABLE missing_output ERROR_VARIABLE missing_error)
    if(missing_result EQUAL 0 OR NOT "${missing_output}${missing_error}" MATCHES "TimeShield")
        message(FATAL_ERROR "A core consumer must reject a missing mandatory TimeShield dependency")
    endif()
endif()
