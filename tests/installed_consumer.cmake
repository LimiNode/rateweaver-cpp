cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/run_checked.cmake")

# Relocate the install before configuring a consumer which has no source-tree
# include path. Disable package registries so another local build cannot mask
# a broken install export. Reuse this test's own scratch directory on reruns.
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
run_checked("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${WORK_DIR}/stage" --config "${CONFIG}")
file(REMOVE_RECURSE "${WORK_DIR}/prefix")
file(RENAME "${WORK_DIR}/stage" "${WORK_DIR}/prefix")
file(REMOVE_RECURSE "${WORK_DIR}/build" "${WORK_DIR}/source")
file(COPY "${CONSUMER_DIR}/" DESTINATION "${WORK_DIR}/source")
set(platform_args)
if(GENERATOR_PLATFORM)
    list(APPEND platform_args -A "${GENERATOR_PLATFORM}")
endif()
set(component_args)
if(CONSUMER_TYPE STREQUAL "components")
    list(APPEND component_args "-DCONSUME_JSON=${CONSUME_JSON}" "-DCONSUME_ECB=${CONSUME_ECB}"
        "-DCONSUME_KURLYK=${CONSUME_KURLYK}" "-DRATEWEAVER_KURLYK_INCLUDE_DIR=${KURLYK_INCLUDE_DIR}"
        "-DCURL_INCLUDE_DIR=${CURL_INCLUDE_DIR}" "-DCURL_LIBRARY_RELEASE=${CURL_LIBRARY}"
        "-DCURL_DIR=${CURL_DIR}"
        "-Dnlohmann_json_DIR=${JSON_DIR}" "-Dpugixml_DIR=${XML_DIR}")
else()
    list(APPEND component_args -DRATEWEAVER_TEST_OPTIONAL_DEPENDENCIES_UNAVAILABLE=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_kurlyk=ON -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON)
endif()
run_checked("${CMAKE_COMMAND}" -S "${WORK_DIR}/source" -B "${WORK_DIR}/build"
    -G "${GENERATOR}" ${platform_args} "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_PREFIX_PATH=${WORK_DIR}/prefix"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
    ${component_args})
run_checked("${CMAKE_COMMAND}" --build "${WORK_DIR}/build" --config "${CONFIG}")
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${WORK_DIR}/build" -C "${CONFIG}" --output-on-failure)
