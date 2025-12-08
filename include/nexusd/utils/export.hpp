/**
 * @file export.hpp
 * @brief Symbol visibility macros for nexusd_utils shared library.
 *
 * This header provides the NEXUSD_UTILS_API macro for cross-platform
 * shared library symbol export/import.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    // Windows platform
    #if defined(NEXUSD_UTILS_BUILD)
        // Building the library - export symbols
        #define NEXUSD_UTILS_API __declspec(dllexport)
    #else
        // Using the library - import symbols
        #define NEXUSD_UTILS_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang on Unix-like systems
    #if defined(NEXUSD_UTILS_BUILD)
        #define NEXUSD_UTILS_API __attribute__((visibility("default")))
    #else
        #define NEXUSD_UTILS_API
    #endif
#else
    // Unknown compiler - no special handling
    #define NEXUSD_UTILS_API
#endif
