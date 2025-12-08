/**
 * @file crc64.cpp
 * @brief CRC64 implementation (provides linkage for shared library).
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/utils/crc64.hpp"

namespace nexusd {
namespace utils {

// CRC64 is header-only but we need this for proper symbol export.
// The lookup table is generated on first use.

}  // namespace utils
}  // namespace nexusd
