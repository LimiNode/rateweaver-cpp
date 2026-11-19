cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/run_checked.cmake")
set(platform_args)
if(GENERATOR_PLATFORM)
    list(APPEND platform_args -A "${GENERATOR_PLATFORM}")
endif()
run_checked("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/superproject" -B "${BUILD_DIR}/superproject-test"
    -G "${GENERATOR}" ${platform_args} "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=Release" "-DRATEWEAVER_SOURCE_DIR=${SOURCE_DIR}"
    -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON)
run_checked("${CMAKE_COMMAND}" --build "${BUILD_DIR}/superproject-test" --config "${CONFIG}")
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD_DIR}/superproject-test" -C "${CONFIG}" --output-on-failure)
