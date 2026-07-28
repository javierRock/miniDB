#include "minidb/index/hash_index.hpp"

#include <string>

#include "minidb/common/serialization.hpp"

namespace minidb {
namespace {

// Header page field offsets.
constexpr std::size_t kOffsetHeaderType = 0;
constexpr std::size_t kOffsetBucketCount = 2;
constexpr std::size_t kOffsetBucketCapacity = 4;
constexpr std::size_t kOffsetEntryCount = 8;
constexpr std::size_t kOffsetBucketIds = 12;

// Bucket / overflow page field offsets.
constexpr std::size_t kOffsetBucketType = 0;
constexpr std::size_t kOffsetBucketEntryCount = 2;
constexpr std::size_t kOffsetOverflowPageId = 4;
constexpr std::size_t kOffsetEntries = 8;

// Field offsets inside one 10-byte entry.
constexpr std::size_t kEntryOffsetKey = 0;
constexpr std::size_t kEntryOffsetPageId = 4;
constexpr std::size_t kEntryOffsetSlotId = 8;

[[nodiscard]] std::size_t EntryOffset(std::uint16_t index) {
    return kOffsetEntries + HashIndex::kEntrySize * index;
}

/// Initialises a freshly allocated page as an empty bucket or overflow page.
void InitialiseBucketPage(std::span<std::byte, kPageSize> page, PageType type) {
    serialization::WriteU8(page, kOffsetBucketType, static_cast<std::uint8_t>(type));
    serialization::WriteU16(page, kOffsetBucketEntryCount, 0);
    serialization::WriteU32(page, kOffsetOverflowPageId, kInvalidPageId);
}

[[nodiscard]] std::uint16_t EntryCountOf(std::span<const std::byte> page) {
    return serialization::ReadU16(page, kOffsetBucketEntryCount);
}

[[nodiscard]] PageId OverflowOf(std::span<const std::byte> page) {
    return serialization::ReadU32(page, kOffsetOverflowPageId);
}

[[nodiscard]] std::int32_t KeyAt(std::span<const std::byte> page, std::uint16_t index) {
    return serialization::ReadI32(page, EntryOffset(index) + kEntryOffsetKey);
}

[[nodiscard]] RecordId RecordIdAt(std::span<const std::byte> page, std::uint16_t index) {
    return RecordId{serialization::ReadU32(page, EntryOffset(index) + kEntryOffsetPageId),
                    serialization::ReadU16(page, EntryOffset(index) + kEntryOffsetSlotId)};
}

void WriteEntry(std::span<std::byte> page, std::uint16_t index, std::int32_t key, RecordId rid) {
    serialization::WriteI32(page, EntryOffset(index) + kEntryOffsetKey, key);
    serialization::WriteU32(page, EntryOffset(index) + kEntryOffsetPageId, rid.page_id);
    serialization::WriteU16(page, EntryOffset(index) + kEntryOffsetSlotId, rid.slot_id);
}

}  // namespace

std::uint32_t HashIndex::HashKey(std::int32_t key) {
    return static_cast<std::uint32_t>(key) * 2654435761u;
}

HashIndex::HashIndex(BufferPoolManager& pool, PageId header_page_id)
    : pool_(pool), header_page_id_(header_page_id) {
    if (header_page_id_ == kInvalidPageId) {
        throw StorageError("El índice no tiene página de cabecera");
    }

    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    const auto page = std::span<const std::byte>(guard.Data());
    if (static_cast<PageType>(serialization::ReadU8(page, kOffsetHeaderType)) !=
        PageType::kHashIndexHeader) {
        throw StorageError("La página " + std::to_string(header_page_id_) +
                           " no es la cabecera de un índice hash");
    }
}

PageId HashIndex::Create(BufferPoolManager& pool, std::uint16_t bucket_count,
                         std::uint16_t bucket_capacity) {
    if (bucket_count == 0 || bucket_count > kMaxBucketCount) {
        throw StorageError("Número de buckets fuera de rango: " + std::to_string(bucket_count) +
                           " (máximo " + std::to_string(kMaxBucketCount) + ")");
    }
    if (bucket_capacity == 0 || bucket_capacity > kMaxBucketCapacity) {
        throw StorageError("Capacidad de bucket fuera de rango: " +
                           std::to_string(bucket_capacity) + " (máximo " +
                           std::to_string(kMaxBucketCapacity) + ")");
    }

    PageId header_page_id = kInvalidPageId;
    PageGuard header = NewGuarded(pool, header_page_id);

    serialization::WriteU8(header.Data(), kOffsetHeaderType,
                           static_cast<std::uint8_t>(PageType::kHashIndexHeader));
    serialization::WriteU16(header.Data(), kOffsetBucketCount, bucket_count);
    serialization::WriteU16(header.Data(), kOffsetBucketCapacity, bucket_capacity);
    serialization::WriteU32(header.Data(), kOffsetEntryCount, 0);

    // Bucket pages are allocated up front so a lookup never has to create one,
    // and so the layout on disk is predictable.
    for (std::uint16_t i = 0; i < bucket_count; ++i) {
        PageId bucket_page_id = kInvalidPageId;
        PageGuard bucket = NewGuarded(pool, bucket_page_id);
        InitialiseBucketPage(bucket.Data(), PageType::kHashBucket);
        bucket.MarkDirty();

        serialization::WriteU32(header.Data(), kOffsetBucketIds + sizeof(PageId) * i,
                                bucket_page_id);
    }

    header.MarkDirty();
    return header_page_id;
}

std::uint16_t HashIndex::BucketCount() const {
    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    return serialization::ReadU16(guard.Data(), kOffsetBucketCount);
}

std::uint16_t HashIndex::BucketCapacity() const {
    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    return serialization::ReadU16(guard.Data(), kOffsetBucketCapacity);
}

std::uint32_t HashIndex::EntryCount() const {
    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    return serialization::ReadU32(guard.Data(), kOffsetEntryCount);
}

void HashIndex::SetEntryCount(std::uint32_t value) {
    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    serialization::WriteU32(guard.Data(), kOffsetEntryCount, value);
    guard.MarkDirty();
}

std::uint16_t HashIndex::BucketOf(std::int32_t key) const {
    return static_cast<std::uint16_t>(HashKey(key) % BucketCount());
}

PageId HashIndex::BucketPageId(std::uint16_t bucket) const {
    PageGuard guard = FetchGuarded(pool_, header_page_id_);
    return serialization::ReadU32(guard.Data(), kOffsetBucketIds + sizeof(PageId) * bucket);
}

std::optional<RecordId> HashIndex::Search(std::int32_t key) const {
    PageId page_id = BucketPageId(BucketOf(key));

    // Walk the bucket's chain, one page at a time. Only one page is pinned at
    // any moment, so a long chain cannot exhaust the pool.
    while (page_id != kInvalidPageId) {
        PageGuard guard = FetchGuarded(pool_, page_id);
        const auto page = std::span<const std::byte>(guard.Data());

        const std::uint16_t entries = EntryCountOf(page);
        for (std::uint16_t i = 0; i < entries; ++i) {
            if (KeyAt(page, i) == key) {
                return RecordIdAt(page, i);
            }
        }
        page_id = OverflowOf(page);
    }
    return std::nullopt;
}

void HashIndex::Insert(std::int32_t key, RecordId rid) {
    if (!rid.IsValid()) {
        throw StorageError("El índice no admite un RecordId inválido");
    }

    // Reject duplicates in a separate pass. Folding this into the search for a
    // free slot would mean scanning the rest of the chain anyway, since the key
    // may live past the first page with room, and the merged version is much
    // harder to read for no measurable gain on buffered pages.
    if (Search(key).has_value()) {
        throw QueryError("Ya existe un registro con la clave primaria " + std::to_string(key));
    }

    const std::uint16_t capacity = BucketCapacity();
    PageId page_id = BucketPageId(BucketOf(key));
    PageId last_page_id = kInvalidPageId;

    while (page_id != kInvalidPageId) {
        PageGuard guard = FetchGuarded(pool_, page_id);
        auto page = guard.Data();

        const std::uint16_t entries = EntryCountOf(page);
        if (entries < capacity) {
            WriteEntry(page, entries, key, rid);
            serialization::WriteU16(page, kOffsetBucketEntryCount,
                                    static_cast<std::uint16_t>(entries + 1));
            guard.MarkDirty();
            SetEntryCount(EntryCount() + 1);
            return;
        }

        last_page_id = page_id;
        page_id = OverflowOf(std::span<const std::byte>(page));
    }

    // Every page in the chain is full: link a new overflow page behind the last.
    PageId overflow_page_id = kInvalidPageId;
    {
        PageGuard overflow = NewGuarded(pool_, overflow_page_id);
        InitialiseBucketPage(overflow.Data(), PageType::kHashOverflow);
        WriteEntry(overflow.Data(), 0, key, rid);
        serialization::WriteU16(overflow.Data(), kOffsetBucketEntryCount, 1);
        overflow.MarkDirty();
    }
    {
        PageGuard tail = FetchGuarded(pool_, last_page_id);
        serialization::WriteU32(tail.Data(), kOffsetOverflowPageId, overflow_page_id);
        tail.MarkDirty();
    }
    SetEntryCount(EntryCount() + 1);
}

bool HashIndex::Remove(std::int32_t key) {
    PageId page_id = BucketPageId(BucketOf(key));
    PageId previous_page_id = kInvalidPageId;

    while (page_id != kInvalidPageId) {
        PageId next_page_id = kInvalidPageId;
        bool erased = false;
        bool became_empty = false;

        {
            PageGuard guard = FetchGuarded(pool_, page_id);
            auto page = guard.Data();
            const std::uint16_t entries = EntryCountOf(page);
            next_page_id = OverflowOf(page);

            for (std::uint16_t i = 0; i < entries; ++i) {
                if (KeyAt(page, i) != key) {
                    continue;
                }
                // Move the last entry into the gap instead of leaving a
                // tombstone, so pages never degrade with holes.
                if (i != entries - 1) {
                    WriteEntry(page, i, KeyAt(page, static_cast<std::uint16_t>(entries - 1)),
                               RecordIdAt(page, static_cast<std::uint16_t>(entries - 1)));
                }
                serialization::WriteU16(page, kOffsetBucketEntryCount,
                                        static_cast<std::uint16_t>(entries - 1));
                guard.MarkDirty();
                erased = true;
                became_empty = (entries == 1);
                break;
            }
        }

        if (erased) {
            SetEntryCount(EntryCount() - 1);
            // An emptied overflow page is unlinked and returned to the file's
            // free list, so a burst of deletes actually gives space back. The
            // primary bucket page always stays.
            if (became_empty && previous_page_id != kInvalidPageId) {
                {
                    PageGuard previous = FetchGuarded(pool_, previous_page_id);
                    serialization::WriteU32(previous.Data(), kOffsetOverflowPageId, next_page_id);
                    previous.MarkDirty();
                }
                pool_.DeletePage(page_id);
            }
            return true;
        }

        previous_page_id = page_id;
        page_id = next_page_id;
    }
    return false;
}

void HashIndex::UpdateRecordId(std::int32_t key, RecordId rid) {
    if (!rid.IsValid()) {
        throw StorageError("El índice no admite un RecordId inválido");
    }

    PageId page_id = BucketPageId(BucketOf(key));
    while (page_id != kInvalidPageId) {
        PageGuard guard = FetchGuarded(pool_, page_id);
        auto page = guard.Data();

        const std::uint16_t entries = EntryCountOf(page);
        for (std::uint16_t i = 0; i < entries; ++i) {
            if (KeyAt(page, i) == key) {
                WriteEntry(page, i, key, rid);
                guard.MarkDirty();
                return;
            }
        }
        page_id = OverflowOf(page);
    }
    throw QueryError("No existe en el índice la clave primaria " + std::to_string(key));
}

std::uint32_t HashIndex::ChainLength(std::uint16_t bucket) const {
    std::uint32_t pages = 0;
    PageId page_id = BucketPageId(bucket);
    while (page_id != kInvalidPageId) {
        ++pages;
        PageGuard guard = FetchGuarded(pool_, page_id);
        page_id = OverflowOf(std::span<const std::byte>(guard.Data()));
    }
    return pages;
}

std::uint32_t HashIndex::TotalPageCount() const {
    std::uint32_t pages = 1;  // the header
    const std::uint16_t buckets = BucketCount();
    for (std::uint16_t i = 0; i < buckets; ++i) {
        pages += ChainLength(i);
    }
    return pages;
}

}  // namespace minidb
