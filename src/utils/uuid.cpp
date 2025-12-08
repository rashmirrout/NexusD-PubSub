/**
 * @file uuid.cpp
 * @brief UUID implementation (provides linkage for shared library).
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/utils/uuid.hpp"

namespace nexusd {
namespace utils {

// UUIDGenerator is header-only with static methods.
// This file ensures the shared library exports the symbols properly.

}  // namespace utils
}  // namespace nexusd
