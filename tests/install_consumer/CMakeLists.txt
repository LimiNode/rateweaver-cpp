cmake_minimum_required(VERSION 3.20)
project(rateweaver_installed_consumer LANGUAGES CXX)
find_package(rateweaver 0.1 CONFIG REQUIRED)
if(RATEWEAVER_TEST_OPTIONAL_DEPENDENCIES_UNAVAILABLE)
    # The CTest harness disables JSON/XML/HTTP discovery to verify that optional
    # component failure preserves the independently usable installed core.
    find_package(rateweaver 0.1 CONFIG REQUIRED OPTIONAL_COMPONENTS providers ecb kurlyk)
    if(rateweaver_providers_FOUND OR rateweaver_ecb_FOUND OR rateweaver_kurlyk_FOUND OR
        TARGET rateweaver::providers OR TARGET rateweaver::ecb OR TARGET rateweaver::kurlyk)
        message(FATAL_ERROR "Optional components ignored disabled dependency discovery")
    endif()
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE rateweaver::rateweaver)
set_target_properties(consumer PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
enable_testing()
add_test(NAME consumer COMMAND consumer)
