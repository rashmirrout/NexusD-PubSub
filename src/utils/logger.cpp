/**
 * @file logger.cpp
 * @brief Logger implementation (mostly header-only, this provides linkage).
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/utils/logger.hpp"

namespace nexusd {
namespace utils {

// The Logger class is mostly header-only, but we need this file
// to ensure the shared library has at least one symbol to export.
// The singleton instance is created in the header via static local.

}  // namespace utils
}  // namespace nexusd
