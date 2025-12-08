# =============================================================================
# NexusD - Test Suite Configuration
# =============================================================================
#
# Uses GoogleTest via FetchContent for unit and integration testing.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Option to enable/disable tests
option(NEXUSD_BUILD_TESTS "Build NexusD tests" ON)

if(NOT NEXUSD_BUILD_TESTS)
    return()
endif()

# ----------------------------------------------------------------------------
# Fetch GoogleTest
# ----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

# ----------------------------------------------------------------------------
# Test Helper Macro
# ----------------------------------------------------------------------------
macro(nexusd_add_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})
    target_link_libraries(${TEST_NAME} PRIVATE
        GTest::gtest_main
        GTest::gmock
    )
    target_include_directories(${TEST_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # Set output directory
    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
    )
endmacro()

# ----------------------------------------------------------------------------
# Unit Tests
# ----------------------------------------------------------------------------
add_subdirectory(unit)

# ----------------------------------------------------------------------------
# Integration Tests
# ----------------------------------------------------------------------------
add_subdirectory(integration)
