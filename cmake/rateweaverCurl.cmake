# kurlyk imports the same pinned shared package through its full CMake project.
# Resolve only curl here so its unrelated WebSocket dependencies stay disabled.
function(rateweaver_add_prebuilt_curl)
    if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
       NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$")
        message(FATAL_ERROR "RATEWEAVER_CURL_MODE=PREBUILT requires Windows MinGW x64; use SYSTEM or SOURCE on this platform")
    endif()
    include(FetchContent)
    # Keep FetchContent paths short for Windows generators with MAX_PATH limits.
    cmake_policy(PUSH)
    if(POLICY CMP0168)
        cmake_policy(SET CMP0168 NEW)
    endif()
    FetchContent_Declare(rw_curl
        GIT_REPOSITORY https://github.com/LimiNode/curl-8.22.0_1-win64-mingw.git
        GIT_TAG 5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d)
    FetchContent_MakeAvailable(rw_curl)
    cmake_policy(POP)
    foreach(artifact include/curl/curl.h lib/libcurl.dll.a bin/libcurl-x64.dll)
        if(NOT EXISTS "${rw_curl_SOURCE_DIR}/${artifact}")
            message(FATAL_ERROR "The pinned MinGW curl package is missing ${artifact}")
        endif()
    endforeach()
    add_library(CURL::libcurl SHARED IMPORTED GLOBAL)
    set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_IMPLIB "${rw_curl_SOURCE_DIR}/lib/libcurl.dll.a"
        IMPORTED_LOCATION "${rw_curl_SOURCE_DIR}/bin/libcurl-x64.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${rw_curl_SOURCE_DIR}/include")
    if(RATEWEAVER_INSTALL)
        install(FILES "${rw_curl_SOURCE_DIR}/bin/libcurl-x64.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT RateWeaverDependencies)
        install(FILES "${rw_curl_SOURCE_DIR}/lib/libcurl.dll.a"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT RateWeaverDependencies)
        install(DIRECTORY "${rw_curl_SOURCE_DIR}/include/curl"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT RateWeaverDependencies)
        install(FILES "${rw_curl_SOURCE_DIR}/COPYING.txt"
            "${rw_curl_SOURCE_DIR}/README.md"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/RateWeaverCurl" COMPONENT RateWeaverDependencies)
        install(DIRECTORY "${rw_curl_SOURCE_DIR}/licenses/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/RateWeaverCurl" COMPONENT RateWeaverDependencies)
        configure_package_config_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RateWeaverCurlConfig.cmake.in"
            "${PROJECT_BINARY_DIR}/CURLConfig.cmake"
            INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CURL"
            PATH_VARS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR)
        write_basic_package_version_file("${PROJECT_BINARY_DIR}/CURLConfigVersion.cmake"
            VERSION 8.22.0 COMPATIBILITY SameMajorVersion)
        install(FILES "${PROJECT_BINARY_DIR}/CURLConfig.cmake"
            "${PROJECT_BINARY_DIR}/CURLConfigVersion.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CURL" COMPONENT RateWeaverDependencies)
    endif()
endfunction()

function(rateweaver_resolve_curl)
    # A host target is authoritative in every mode, even on a platform where
    # RateWeaver's own binary fallback cannot be used.
    if(TARGET CURL::libcurl)
        set(RATEWEAVER_CURL_PROVIDER HOST CACHE INTERNAL "Resolved libcurl provider" FORCE)
        message(STATUS "RateWeaver CURL: HOST target CURL::libcurl")
        return()
    endif()
    set(mode "${RATEWEAVER_CURL_MODE}")
    if(mode STREQUAL "BUNDLED")
        message(DEPRECATION "RATEWEAVER_CURL_MODE=BUNDLED is deprecated; use SOURCE")
        set(mode SOURCE)
    endif()
    if(mode STREQUAL "AUTO" OR mode STREQUAL "SYSTEM")
        if(WIN32)
            find_package(CURL 8.11.0 QUIET)
        else()
            find_package(CURL QUIET)
        endif()
        if(TARGET CURL::libcurl)
            set(RATEWEAVER_CURL_PROVIDER SYSTEM CACHE INTERNAL "Resolved libcurl provider" FORCE)
            message(STATUS "RateWeaver CURL: SYSTEM package")
            return()
        endif()
    endif()
    if(mode STREQUAL "SYSTEM")
        message(FATAL_ERROR "CURL::libcurl is required in SYSTEM curl mode; provide a compatible installed package or host target")
    endif()
    if(mode STREQUAL "PREBUILT" OR (mode STREQUAL "AUTO" AND WIN32 AND MINGW
       AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$"))
        rateweaver_add_prebuilt_curl()
        set(RATEWEAVER_CURL_PROVIDER PREBUILT CACHE INTERNAL "Resolved libcurl provider" FORCE)
        message(STATUS "RateWeaver CURL: PREBUILT 8.22.0 (5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d)")
    elseif(mode STREQUAL "SOURCE" OR mode STREQUAL "AUTO")
        rateweaver_add_source_curl()
        set(RATEWEAVER_CURL_PROVIDER SOURCE CACHE INTERNAL "Resolved libcurl provider" FORCE)
        message(STATUS "RateWeaver CURL: SOURCE from external/curl")
    else()
        message(FATAL_ERROR "Unknown RATEWEAVER_CURL_MODE=${mode}; choose AUTO, SYSTEM, PREBUILT or SOURCE")
    endif()
endfunction()
