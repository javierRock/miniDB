#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "minidb/buffer/buffer_pool_manager.hpp"
#include "minidb/common/types.hpp"

namespace minidb {

/// Maps a primary key to the record's location, stored entirely in database
/// pages.
///
/// This class deliberately holds no container of entries. Its only members are
/// a reference to the buffer pool and the page id of its header, so every
/// lookup, insert and delete reads and writes real pages through the pool. An
/// index kept in a std::unordered_map and serialised on shutdown would behave
/// the same from the outside while bypassing the page manager entirely, which
/// is exactly what this design avoids.
///
/// Collisions are resolved by separate chaining: all keys landing in the same
/// bucket share its page, and when that page fills, an overflow page is linked
/// behind it.
///
/// Header page:
///
///   offset  size          field
///   ------  ------------  ------------------------------------------------
///        0             1  page_type          kHashIndexHeader
///        1             1  reserved
///        2             2  bucket_count       16
///        4             2  bucket_capacity    entries per page (default 408)
///        6             2  reserved
///        8             4  entry_count
///       12  4 x buckets   bucket_page_ids
///
/// At 4 bytes per bucket the header holds up to (4096 - 12) / 4 = 1021 buckets,
/// so 16 uses 76 bytes of it.
///
/// Bucket and overflow pages share one layout:
///
///   offset  size          field
///   ------  ------------  ------------------------------------------------
///        0             1  page_type          kHashBucket or kHashOverflow
///        1             1  reserved
///        2             2  entry_count
///        4             4  overflow_page_id   kInvalidPageId when none
///        8  10 x entries  entries
///
/// An entry is 10 bytes: int32 key, uint32 page_id, uint16 slot_id. It carries
/// no state byte, because deleting swaps the last entry into the gap instead of
/// leaving a tombstone. A page therefore holds (4096 - 8) / 10 = 408 entries.
class HashIndex {
public:
    static constexpr std::size_t kHeaderFixedSize = 12;
    static constexpr std::size_t kBucketHeaderSize = 8;
    static constexpr std::size_t kEntrySize = 10;

    /// Entries that fit in one bucket page when it is used to the full.
    static constexpr std::uint16_t kMaxBucketCapacity =
        static_cast<std::uint16_t>((kPageSize - kBucketHeaderSize) / kEntrySize);

    /// Largest number of buckets whose page ids fit in the header page.
    static constexpr std::uint16_t kMaxBucketCount =
        static_cast<std::uint16_t>((kPageSize - kHeaderFixedSize) / sizeof(PageId));

    /// Opens an existing index whose header lives on `header_page_id`.
    HashIndex(BufferPoolManager& pool, PageId header_page_id);

    /// Builds a new index: allocates the header and one page per bucket, and
    /// returns the header's page id.
    ///
    /// `bucket_capacity` exists so tests can create a deliberately tiny index
    /// and exercise deep overflow chains in milliseconds instead of needing
    /// thousands of inserts. It is persisted, so an index always reads back with
    /// the geometry it was created with.
    [[nodiscard]] static PageId Create(BufferPoolManager& pool,
                                       std::uint16_t bucket_count = kDefaultBucketCount,
                                       std::uint16_t bucket_capacity = kMaxBucketCapacity);

    /// Adds a key. Throws QueryError when the key is already present.
    void Insert(std::int32_t key, RecordId rid);

    /// Looks up a key, or std::nullopt when it is absent.
    [[nodiscard]] std::optional<RecordId> Search(std::int32_t key) const;

    [[nodiscard]] bool Contains(std::int32_t key) const { return Search(key).has_value(); }

    /// Removes a key. Returns false when it was not there.
    bool Remove(std::int32_t key);

    /// Repoints an existing key at a new location, after the record moved.
    /// Throws QueryError when the key is absent.
    void UpdateRecordId(std::int32_t key, RecordId rid);

    [[nodiscard]] std::uint32_t EntryCount() const;
    [[nodiscard]] std::uint16_t BucketCount() const;
    [[nodiscard]] std::uint16_t BucketCapacity() const;
    [[nodiscard]] PageId HeaderPageId() const { return header_page_id_; }

    /// Bucket a key belongs to. Public so tests can construct collisions
    /// without depending on the hash function's internals.
    [[nodiscard]] std::uint16_t BucketOf(std::int32_t key) const;

    /// Number of pages making up one bucket's chain, including the primary
    /// page. Used by tests and by the `.pages` command.
    [[nodiscard]] std::uint32_t ChainLength(std::uint16_t bucket) const;

    /// Total pages the index occupies, for reporting.
    [[nodiscard]] std::uint32_t TotalPageCount() const;

private:
    /// Deterministic hash. Knuth's multiplicative constant mixes the key so
    /// that regular id patterns do not all land in the same bucket.
    ///
    /// std::hash is deliberately not used: it is implementation-defined (the
    /// identity on libstdc++), so a database built with one standard library
    /// could not be read by another.
    [[nodiscard]] static std::uint32_t HashKey(std::int32_t key);

    [[nodiscard]] PageId BucketPageId(std::uint16_t bucket) const;
    void SetEntryCount(std::uint32_t value);

    BufferPoolManager& pool_;
    PageId header_page_id_;
};

}  // namespace minidb
