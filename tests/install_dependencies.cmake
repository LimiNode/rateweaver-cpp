include("${DEPENDENCY_CONFIG}")

function(rateweaver_install_test_dependencies prefix)
    foreach(dependency_build IN LISTS DEPENDENCY_BUILD_DIRS)
        run_checked("${CMAKE_COMMAND}" --install "${dependency_build}"
            --prefix "${prefix}" --config "${CONFIG}")
    endforeach()
    run_checked("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --component RateWeaverDependencies
        --prefix "${prefix}" --config "${CONFIG}")
endfunction()
