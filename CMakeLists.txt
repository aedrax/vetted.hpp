cmake_minimum_required(VERSION 3.16)

project(vetted VERSION 0.1.0 LANGUAGES CXX)

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
           "Compile a deliberately invalid constexpr ChannelID to see the build fail"
           OFF)
    if(DEMO_COMPILE_ERROR)
        target_compile_definitions(vetted_example PRIVATE DEMO_COMPILE_ERROR)
    endif()
endif()
