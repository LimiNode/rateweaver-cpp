cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/run_checked.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/install_dependencies.cmake")
rateweaver_install_test_dependencies("${BUILD_DIR}/superproject-dependencies")
file(WRITE "${BUILD_DIR}/superproject-prefixes.cmake"
    "set(CMAKE_PREFIX_PATH [==[${BUILD_DIR}/superproject-dependencies;${DEPENDENCY_PREFIX_PATH}]==] CACHE STRING \"Host dependency prefixes\")\n")
set(platform_args)
if(GENERATOR_PLATFORM)
    list(APPEND platform_args -A "${GENERATOR_PLATFORM}")
endif()
run_checked("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/superproject" -B "${BUILD_DIR}/superproject-test"
    -C "${BUILD_DIR}/superproject-prefixes.cmake"
    -G "${GENERATOR}" ${platform_args} "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DRATEWEAVER_SOURCE_DIR=${SOURCE_DIR}"
    "-DHOST_JSON=${HOST_JSON}" "-DHOST_ECB=${HOST_ECB}" "-DHOST_KURLYK=${HOST_KURLYK}")
run_checked("${CMAKE_COMMAND}" --build "${BUILD_DIR}/superproject-test" --config "${CONFIG}")
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD_DIR}/superproject-test" -C "${CONFIG}" --output-on-failure)
if(HOST_KURLYK)
    run_checked("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/superproject" -B "${BUILD_DIR}/host-curl-test"
        -C "${BUILD_DIR}/superproject-prefixes.cmake"
        -G "${GENERATOR}" ${platform_args} "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
        "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DRATEWEAVER_SOURCE_DIR=${SOURCE_DIR}"
        "-DHOST_JSON=${HOST_JSON}" "-DHOST_ECB=${HOST_ECB}" -DHOST_KURLYK=ON -DHOST_CURL_ONLY=ON)
    run_checked("${CMAKE_COMMAND}" --build "${BUILD_DIR}/host-curl-test" --config "${CONFIG}")
    run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD_DIR}/host-curl-test" -C "${CONFIG}" --output-on-failure)
endif()
