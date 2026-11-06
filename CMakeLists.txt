cmake_minimum_required(VERSION 3.16)

# Keep this version the same as VETTED_VERSION_* in include/vetted.hpp.
project(vetted VERSION 0.5.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# The library: one header, nothing to compile.
# Consumers write:  target_link_libraries(app PRIVATE vetted::vetted)
# ---------------------------------------------------------------------------
add_library(vetted INTERFACE)
add_library(vetted::vetted ALIAS vetted)
target_include_directories(vetted INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(vetted INTERFACE cxx_std_20)

install(FILES include/vetted.hpp DESTINATION include)
install(TARGETS vetted EXPORT vettedTargets)
install(EXPORT vettedTargets
    NAMESPACE vetted::
    FILE vettedConfig.cmake
    DESTINATION lib/cmake/vetted)

# The version file, so `find_package(vetted 0.5)` can check the version. A 0.x
# release can break the API in each minor version, so only the same minor matches.
include(CMakePackageConfigHelpers)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/vettedConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
    ARCH_INDEPENDENT)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vettedConfigVersion.cmake DESTINATION lib/cmake/vetted)

# ---------------------------------------------------------------------------
# The example. Built by default when this is the top-level project.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VETTED_BUILD_EXAMPLES_DEFAULT ON)
else()
    set(VETTED_BUILD_EXAMPLES_DEFAULT OFF)
endif()
option(VETTED_BUILD_EXAMPLES "Build the vetted example program" ${VETTED_BUILD_EXAMPLES_DEFAULT})

if(VETTED_BUILD_EXAMPLES)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    add_executable(vetted_example examples/main.cc examples/domain.hpp)
    target_link_libraries(vetted_example PRIVATE vetted::vetted)
    set_target_properties(vetted_example PROPERTIES CXX_EXTENSIONS OFF)

    # Strict warnings: -Wconversion in particular catches a raw int sneaking
    # into a place that should hold a validated type.
    target_compile_options(vetted_example PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

    # Turn this on to watch the compiler reject an invalid constexpr value:
    #     cmake -S . -B build -DDEMO_COMPILE_ERROR=ON && cmake --build build
    option(DEMO_COMPILE_ERROR
           "Compile a deliberately invalid constexpr Quantity to see the build fail"
           OFF)
    if(DEMO_COMPILE_ERROR)
        target_compile_definitions(vetted_example PRIVATE DEMO_COMPILE_ERROR)
    endif()
endif()

# ---------------------------------------------------------------------------
# The tests. Built by default when this is the top-level project.
#     cmake -S . -B build && cmake --build build && ctest --test-dir build
# Add -DCMAKE_CXX_STANDARD=23 to test the C++23 forms.
# ---------------------------------------------------------------------------
option(VETTED_BUILD_TESTS "Build the vetted tests" ${VETTED_BUILD_EXAMPLES_DEFAULT})

if(VETTED_BUILD_TESTS)
    enable_testing()
    # The warnings that a strict consumer uses. The header must give none.
    set(VETTED_STRICT_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        -Wdouble-promotion -Wold-style-cast -Wshadow -Wextra-semi -Werror)

    foreach(test vetted_test vetted_no_exceptions_test)
        add_executable(${test} tests/${test}.cc)
        target_link_libraries(${test} PRIVATE vetted::vetted)
        set_target_properties(${test} PROPERTIES CXX_EXTENSIONS OFF)
        target_compile_options(${test} PRIVATE ${VETTED_STRICT_FLAGS})
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    # The test checks that the version macros in the header match project().
    target_compile_definitions(vetted_test PRIVATE
        VETTED_PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        VETTED_PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        VETTED_PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH})
    # The non-throwing functions must compile without exceptions.
    target_compile_options(vetted_no_exceptions_test PRIVATE -fno-exceptions)
endif()
