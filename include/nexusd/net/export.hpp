/**
 * @file export.hpp
 * @brief Symbol visibility macros for nexusd_net shared library.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #if defined(NEXUSD_NET_BUILD)
        #define NEXUSD_NET_API __declspec(dllexport)
    #else
        #define NEXUSD_NET_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(NEXUSD_NET_BUILD)
        #define NEXUSD_NET_API __attribute__((visibility("default")))
    #else
        #define NEXUSD_NET_API
    #endif
#else
    #define NEXUSD_NET_API
#endif
