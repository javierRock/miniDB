#pragma once

#include <cstddef>
#include <cstdint>

namespace minidb {

/// Every page in the database file is exactly this many bytes. The whole
/// physical design in ARCHITECTURE.md is derived from this constant.
inline constexpr std::size_t kPageSize = 4096;

/// Magic number stored at offset 0 of page 0: the ASCII bytes "MIND".
/// Page 0 is the only page that does not begin with a page_type byte, so that
/// `file` and `xxd` can identify the database at a glance.
inline constexpr std::uint32_t kMagicNumber = 0x4D494E44;

/// Bumped whenever the on-disk layout changes incompatibly.
inline constexpr std::uint16_t kFormatVersion = 1;

/// Page 0 holds the file header, page 1 holds the catalog. Both are created
/// when the database file is initialised.
inline constexpr std::uint32_t kFileHeaderPageId = 0;
inline constexpr std::uint32_t kCatalogPageId = 1;

/// Schema bounds. Together they prove that a valid record always fits in an
/// empty page: 8 * (2 + 255) = 2056 bytes, well under the 4080 bytes available
/// after the 12-byte page header and one 4-byte slot.
inline constexpr std::size_t kMaxColumns = 8;
inline constexpr std::size_t kMaxVarcharLength = 255;
inline constexpr std::size_t kMaxIdentifierLength = 32;

/// Default number of frames held in RAM by the buffer pool. Overridable from
/// minidb.conf. The worst-case operation (an UPDATE that relocates a record)
/// pins 6 pages simultaneously, so 8 leaves two frames of headroom.
inline constexpr std::size_t kDefaultBufferPoolFrames = 8;

/// Hash index geometry. A bucket page holds (4096 - 8) / 10 = 408 entries.
inline constexpr std::uint16_t kDefaultBucketCount = 16;

}  // namespace minidb
