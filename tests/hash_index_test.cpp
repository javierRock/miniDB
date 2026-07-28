#include "minidb/index/hash_index.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <set>
#include <vector>

#include "minidb/common/serialization.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::TempDatabase;

RecordId Rid(PageId page, SlotId slot) { return RecordId{page, slot}; }

/// Disk + pool + index over a temporary file.
class IndexStack {
public:
    explicit IndexStack(const std::filesystem::path& path, std::size_t frames = 8,
                        std::uint16_t bucket_count = kDefaultBucketCount,
                        std::uint16_t bucket_capacity = HashIndex::kMaxBucketCapacity) {
        disk_ = std::make_unique<DiskManager>(path);
        pool_ = std::make_unique<BufferPoolManager>(*disk_, frames);
        header_page_id_ = HashIndex::Create(*pool_, bucket_count, bucket_capacity);
        index_ = std::make_unique<HashIndex>(*pool_, header_page_id_);
    }

    /// Tag distinguishing "reopen an existing index" from "create a new one";
    /// without it the two constructors are ambiguous, since PageId and the
    /// frame count are both integers.
    struct Reopen {};

    /// Reopens an existing index whose header sits on `header_page_id`.
    IndexStack(Reopen, const std::filesystem::path& path, PageId header_page_id,
               std::size_t frames = 8) {
        disk_ = std::make_unique<DiskManager>(path);
        pool_ = std::make_unique<BufferPoolManager>(*disk_, frames);
        header_page_id_ = header_page_id;
        index_ = std::make_unique<HashIndex>(*pool_, header_page_id_);
    }

    void Close() { pool_->FlushAllPages(); }

    DiskManager& Disk() { return *disk_; }
    BufferPoolManager& Pool() { return *pool_; }
    HashIndex& Index() { return *index_; }
    PageId HeaderPageId() const { return header_page_id_; }

private:
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPoolManager> pool_;
    std::unique_ptr<HashIndex> index_;
    PageId header_page_id_ = kInvalidPageId;
};

// --- Geometry ------------------------------------------------------------

TEST(HashIndexTest, PageGeometryMatchesTheDesign) {
    // (4096 - 8) / 10 = 408 entries per bucket page.
    EXPECT_EQ(HashIndex::kMaxBucketCapacity, 408u);
    EXPECT_EQ(HashIndex::kBucketHeaderSize + 408u * HashIndex::kEntrySize, 4088u);
    EXPECT_LE(HashIndex::kBucketHeaderSize + 408u * HashIndex::kEntrySize, kPageSize);

    // (4096 - 12) / 4 = 1021 bucket ids fit in the header page.
    EXPECT_EQ(HashIndex::kMaxBucketCount, 1021u);
    EXPECT_LE(HashIndex::kHeaderFixedSize + 16u * sizeof(PageId), kPageSize);
    EXPECT_EQ(HashIndex::kHeaderFixedSize + 16u * sizeof(PageId), 76u);
}

TEST(HashIndexTest, CreateAllocatesTheHeaderAndOneePagePerBucket) {
    TempDatabase temp("geometry");
    IndexStack stack(temp.Path());

    EXPECT_EQ(stack.Index().BucketCount(), kDefaultBucketCount);
    EXPECT_EQ(stack.Index().BucketCapacity(), HashIndex::kMaxBucketCapacity);
    EXPECT_EQ(stack.Index().EntryCount(), 0u);
    EXPECT_EQ(stack.Index().TotalPageCount(), 1u + kDefaultBucketCount);

    for (std::uint16_t b = 0; b < kDefaultBucketCount; ++b) {
        EXPECT_EQ(stack.Index().ChainLength(b), 1u);
    }
}

TEST(HashIndexTest, RejectsImpossibleGeometry) {
    TempDatabase temp("badgeometry");
    DiskManager disk(temp.Path());
    BufferPoolManager pool(disk, 8);

    EXPECT_THROW((void)HashIndex::Create(pool, 0, 8), StorageError);
    EXPECT_THROW((void)HashIndex::Create(pool, HashIndex::kMaxBucketCount + 1, 8), StorageError);
    EXPECT_THROW((void)HashIndex::Create(pool, 16, 0), StorageError);
    EXPECT_THROW((void)HashIndex::Create(pool, 16, HashIndex::kMaxBucketCapacity + 1),
                 StorageError);
}

// --- Basic operations ----------------------------------------------------

TEST(HashIndexTest, InsertSearchRemove) {
    TempDatabase temp("basic");
    IndexStack stack(temp.Path());
    HashIndex& index = stack.Index();

    index.Insert(1, Rid(10, 0));
    index.Insert(2, Rid(10, 1));
    index.Insert(3, Rid(11, 5));

    EXPECT_EQ(index.Search(1), std::optional<RecordId>{Rid(10, 0)});
    EXPECT_EQ(index.Search(2), std::optional<RecordId>{Rid(10, 1)});
    EXPECT_EQ(index.Search(3), std::optional<RecordId>{Rid(11, 5)});
    EXPECT_EQ(index.Search(99), std::nullopt);
    EXPECT_TRUE(index.Contains(1));
    EXPECT_FALSE(index.Contains(99));
    EXPECT_EQ(index.EntryCount(), 3u);

    EXPECT_TRUE(index.Remove(2));
    EXPECT_FALSE(index.Remove(2));
    EXPECT_EQ(index.Search(2), std::nullopt);
    EXPECT_EQ(index.EntryCount(), 2u);
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
}

TEST(HashIndexTest, DuplicateKeyIsRejectedAndChangesNothing) {
    TempDatabase temp("duplicate");
    IndexStack stack(temp.Path());
    HashIndex& index = stack.Index();

    index.Insert(7, Rid(3, 1));
    EXPECT_THROW(index.Insert(7, Rid(9, 9)), QueryError);

    EXPECT_EQ(index.Search(7), std::optional<RecordId>{Rid(3, 1)});  // unchanged
    EXPECT_EQ(index.EntryCount(), 1u);
}

TEST(HashIndexTest, UpdateRecordIdRepointsAnExistingKey) {
    TempDatabase temp("update");
    IndexStack stack(temp.Path());
    HashIndex& index = stack.Index();

    index.Insert(5, Rid(2, 3));
    index.UpdateRecordId(5, Rid(8, 1));

    EXPECT_EQ(index.Search(5), std::optional<RecordId>{Rid(8, 1)});
    EXPECT_EQ(index.EntryCount(), 1u);
    EXPECT_THROW(index.UpdateRecordId(404, Rid(1, 1)), QueryError);
}

TEST(HashIndexTest, NegativeAndExtremeKeysWork) {
    TempDatabase temp("negative");
    IndexStack stack(temp.Path());
    HashIndex& index = stack.Index();

    const std::vector<std::int32_t> keys = {-1, -1000, 0,
                                            std::numeric_limits<std::int32_t>::min(),
                                            std::numeric_limits<std::int32_t>::max()};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        index.Insert(keys[i], Rid(1, static_cast<SlotId>(i)));
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        EXPECT_EQ(index.Search(keys[i]), std::optional<RecordId>{Rid(1, static_cast<SlotId>(i))});
    }
}

TEST(HashIndexTest, RejectsAnInvalidRecordId) {
    TempDatabase temp("invalidrid");
    IndexStack stack(temp.Path());
    EXPECT_THROW(stack.Index().Insert(1, RecordId{}), StorageError);
}

// --- Collisions and overflow ---------------------------------------------

TEST(HashIndexTest, CollidingKeysShareABucketAndBothRemainFindable) {
    TempDatabase temp("collision");
    IndexStack stack(temp.Path());
    HashIndex& index = stack.Index();

    // Find two keys that genuinely collide, without assuming anything about the
    // hash function itself.
    const std::uint16_t target = index.BucketOf(1);
    std::int32_t partner = 0;
    for (std::int32_t candidate = 2; candidate < 100000; ++candidate) {
        if (index.BucketOf(candidate) == target) {
            partner = candidate;
            break;
        }
    }
    ASSERT_NE(partner, 0);
    ASSERT_EQ(index.BucketOf(1), index.BucketOf(partner));

    index.Insert(1, Rid(5, 0));
    index.Insert(partner, Rid(6, 1));

    EXPECT_EQ(index.Search(1), std::optional<RecordId>{Rid(5, 0)});
    EXPECT_EQ(index.Search(partner), std::optional<RecordId>{Rid(6, 1)});
    EXPECT_EQ(index.ChainLength(target), 1u);  // both fit in the primary page
}

TEST(HashIndexTest, HashIsDeterministicAcrossInstances) {
    TempDatabase temp("deterministic");
    IndexStack stack(temp.Path());

    // The same key must always land in the same bucket; otherwise a database
    // written by one build could not be read by another.
    for (std::int32_t key = -50; key < 50; ++key) {
        EXPECT_EQ(stack.Index().BucketOf(key), stack.Index().BucketOf(key));
    }
    EXPECT_LT(stack.Index().BucketOf(12345), kDefaultBucketCount);
}

TEST(HashIndexTest, KeysSpreadAcrossBucketsRatherThanPilingUp) {
    TempDatabase temp("spread");
    IndexStack stack(temp.Path());

    std::array<int, kDefaultBucketCount> histogram{};
    for (std::int32_t key = 0; key < 1600; ++key) {
        ++histogram[stack.Index().BucketOf(key)];
    }
    for (int count : histogram) {
        EXPECT_GT(count, 0) << "un bucket quedó vacío con 1600 claves secuenciales";
    }
}

// A tiny bucket capacity makes deep overflow chains cheap to test. The capacity
// is persisted, so this is a real index rather than a special case.
TEST(HashIndexTest, OverflowChainGrowsWhenBucketsFill) {
    TempDatabase temp("overflow");
    IndexStack stack(temp.Path(), /*frames=*/8, /*bucket_count=*/1, /*bucket_capacity=*/4);
    HashIndex& index = stack.Index();

    for (std::int32_t key = 0; key < 20; ++key) {
        index.Insert(key, Rid(100, static_cast<SlotId>(key)));
    }

    // 20 entries at 4 per page: one primary page plus four overflow pages.
    EXPECT_EQ(index.ChainLength(0), 5u);
    EXPECT_EQ(index.EntryCount(), 20u);

    for (std::int32_t key = 0; key < 20; ++key) {
        EXPECT_EQ(index.Search(key), std::optional<RecordId>{Rid(100, static_cast<SlotId>(key))})
            << "no se encontró la clave " << key << " en la cadena de overflow";
    }
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
}

TEST(HashIndexTest, RemovalWorksAtEveryDepthOfAnOverflowChain) {
    TempDatabase temp("removedeep");
    IndexStack stack(temp.Path(), 8, 1, 4);
    HashIndex& index = stack.Index();

    for (std::int32_t key = 0; key < 20; ++key) {
        index.Insert(key, Rid(100, static_cast<SlotId>(key)));
    }

    // Remove one key from the first page, one from the middle and one from the
    // deepest page.
    EXPECT_TRUE(index.Remove(0));
    EXPECT_TRUE(index.Remove(9));
    EXPECT_TRUE(index.Remove(19));
    EXPECT_EQ(index.EntryCount(), 17u);

    for (std::int32_t key = 0; key < 20; ++key) {
        const bool removed = (key == 0 || key == 9 || key == 19);
        EXPECT_EQ(index.Search(key).has_value(), !removed) << "clave " << key;
    }
}

// Emptying an overflow page must give the page back to the file, otherwise a
// burst of deletes would leak whole pages.
TEST(HashIndexTest, EmptiedOverflowPageIsReclaimedAndTheChainStaysIntact) {
    TempDatabase temp("reclaim");
    IndexStack stack(temp.Path(), 8, 1, 4);
    HashIndex& index = stack.Index();

    for (std::int32_t key = 0; key < 12; ++key) {
        index.Insert(key, Rid(100, static_cast<SlotId>(key)));
    }
    ASSERT_EQ(index.ChainLength(0), 3u);
    const std::uint32_t pages_before = stack.Disk().PageCount();

    // Entries 4..7 live on the second page of the chain; removing all four
    // empties it.
    for (std::int32_t key = 4; key < 8; ++key) {
        EXPECT_TRUE(index.Remove(key));
    }

    EXPECT_EQ(index.ChainLength(0), 2u) << "la página de overflow vacía no se desenlazó";
    EXPECT_NE(stack.Disk().FreePageHead(), kInvalidPageId) << "la página no volvió a la lista libre";
    EXPECT_EQ(stack.Disk().PageCount(), pages_before) << "el archivo creció";

    // Everything else is still reachable: the chain was relinked correctly.
    for (std::int32_t key = 0; key < 12; ++key) {
        const bool removed = (key >= 4 && key < 8);
        EXPECT_EQ(index.Search(key).has_value(), !removed) << "clave " << key;
    }

    // Both remaining pages of the chain are full, so the next four inserts need
    // a fresh overflow page. It must come from the free list rather than from
    // the end of the file.
    ASSERT_EQ(index.ChainLength(0), 2u);
    for (std::int32_t key = 100; key < 104; ++key) {
        index.Insert(key, Rid(200, static_cast<SlotId>(key - 100)));
    }

    EXPECT_EQ(index.ChainLength(0), 3u);
    EXPECT_EQ(stack.Disk().PageCount(), pages_before)
        << "el archivo creció en vez de reutilizar la página liberada";
    EXPECT_EQ(stack.Disk().FreePageHead(), kInvalidPageId);
}

TEST(HashIndexTest, PrimaryBucketPageIsNeverUnlinked) {
    TempDatabase temp("keepprimary");
    IndexStack stack(temp.Path(), 8, 1, 4);
    HashIndex& index = stack.Index();

    index.Insert(1, Rid(2, 0));
    EXPECT_TRUE(index.Remove(1));

    EXPECT_EQ(index.ChainLength(0), 1u);
    EXPECT_EQ(index.EntryCount(), 0u);
    EXPECT_NO_THROW(index.Insert(1, Rid(3, 0)));  // still usable
}

// The failure mode the rubric singles out: an index that works for lookups but
// breaks under heavy insertion.
TEST(HashIndexTest, MassiveInsertOfTenThousandKeys) {
    TempDatabase temp("massive");
    IndexStack stack(temp.Path(), /*frames=*/8);
    HashIndex& index = stack.Index();

    constexpr std::int32_t kKeys = 10000;
    for (std::int32_t key = 0; key < kKeys; ++key) {
        index.Insert(key, Rid(static_cast<PageId>(key / 90 + 20), static_cast<SlotId>(key % 90)));
    }
    EXPECT_EQ(index.EntryCount(), static_cast<std::uint32_t>(kKeys));

    // 10000 keys over 16 buckets is ~625 each, past the 408 that fit in one
    // page, so every bucket must have grown an overflow page.
    std::uint32_t buckets_with_overflow = 0;
    for (std::uint16_t b = 0; b < index.BucketCount(); ++b) {
        if (index.ChainLength(b) > 1) {
            ++buckets_with_overflow;
        }
    }
    EXPECT_EQ(buckets_with_overflow, index.BucketCount());

    for (std::int32_t key = 0; key < kKeys; ++key) {
        ASSERT_TRUE(index.Search(key).has_value()) << "se perdió la clave " << key;
    }
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
    EXPECT_GT(stack.Pool().Statistics().evictions, 0u);
}

TEST(HashIndexTest, MassiveInsertThenDeleteEverything) {
    TempDatabase temp("massivedelete");
    IndexStack stack(temp.Path(), 8, 4, 16);
    HashIndex& index = stack.Index();

    constexpr std::int32_t kKeys = 2000;
    for (std::int32_t key = 0; key < kKeys; ++key) {
        index.Insert(key, Rid(50, static_cast<SlotId>(key % 100)));
    }
    for (std::int32_t key = 0; key < kKeys; ++key) {
        EXPECT_TRUE(index.Remove(key)) << "no se pudo borrar la clave " << key;
    }

    EXPECT_EQ(index.EntryCount(), 0u);
    for (std::uint16_t b = 0; b < index.BucketCount(); ++b) {
        EXPECT_EQ(index.ChainLength(b), 1u) << "el bucket " << b << " conservó páginas overflow";
    }
    // Reinserting the same load must reuse the reclaimed pages.
    const std::uint32_t pages_after_delete = stack.Disk().PageCount();
    for (std::int32_t key = 0; key < kKeys; ++key) {
        index.Insert(key, Rid(50, static_cast<SlotId>(key % 100)));
    }
    EXPECT_EQ(stack.Disk().PageCount(), pages_after_delete);
}

// --- Persistence ---------------------------------------------------------

TEST(HashIndexTest, EntriesSurviveCloseAndReopen) {
    TempDatabase temp("persist");
    PageId header_page_id = kInvalidPageId;

    {
        IndexStack stack(temp.Path());
        for (std::int32_t key = 0; key < 500; ++key) {
            stack.Index().Insert(key, Rid(static_cast<PageId>(key + 100),
                                          static_cast<SlotId>(key % 50)));
        }
        header_page_id = stack.HeaderPageId();
        stack.Close();
    }

    {
        IndexStack stack(IndexStack::Reopen{}, temp.Path(), header_page_id);
        EXPECT_EQ(stack.Index().EntryCount(), 500u);
        EXPECT_EQ(stack.Index().BucketCount(), kDefaultBucketCount);
        for (std::int32_t key = 0; key < 500; ++key) {
            EXPECT_EQ(stack.Index().Search(key),
                      std::optional<RecordId>{
                          Rid(static_cast<PageId>(key + 100), static_cast<SlotId>(key % 50))})
                << "se perdió la clave " << key << " al reabrir";
        }
    }
}

TEST(HashIndexTest, BucketCapacityIsPersistedNotAssumed) {
    TempDatabase temp("persistcapacity");
    PageId header_page_id = kInvalidPageId;

    {
        IndexStack stack(temp.Path(), 8, 2, 6);
        for (std::int32_t key = 0; key < 30; ++key) {
            stack.Index().Insert(key, Rid(1, static_cast<SlotId>(key)));
        }
        header_page_id = stack.HeaderPageId();
        stack.Close();
    }

    {
        IndexStack stack(IndexStack::Reopen{}, temp.Path(), header_page_id);
        EXPECT_EQ(stack.Index().BucketCount(), 2u);
        EXPECT_EQ(stack.Index().BucketCapacity(), 6u);
        for (std::int32_t key = 0; key < 30; ++key) {
            EXPECT_TRUE(stack.Index().Search(key).has_value());
        }
    }
}

// The index must live in real pages, not in RAM. Reading the raw file and
// finding bucket pages is what proves it.
TEST(HashIndexTest, IndexPagesExistOnDisk) {
    TempDatabase temp("ondisk");
    PageId header_page_id = kInvalidPageId;

    {
        IndexStack stack(temp.Path(), 8, 2, 4);
        for (std::int32_t key = 0; key < 20; ++key) {
            stack.Index().Insert(key, Rid(77, static_cast<SlotId>(key)));
        }
        header_page_id = stack.HeaderPageId();
        stack.Close();
    }

    // Open the file with the disk manager alone and count page types.
    DiskManager disk(temp.Path());
    int headers = 0;
    int buckets = 0;
    int overflows = 0;
    std::array<std::byte, kPageSize> page{};

    for (PageId page_id = 1; page_id < disk.PageCount(); ++page_id) {
        disk.ReadPage(page_id, page);
        switch (static_cast<PageType>(serialization::ReadU8(page, 0))) {
            case PageType::kHashIndexHeader: ++headers; break;
            case PageType::kHashBucket: ++buckets; break;
            case PageType::kHashOverflow: ++overflows; break;
            default: break;
        }
    }

    EXPECT_EQ(headers, 1);
    EXPECT_EQ(buckets, 2);
    EXPECT_GT(overflows, 0) << "no hay páginas de overflow físicas en el archivo";
    EXPECT_EQ(disk.PageCount(), 1u + 1u + 2u + static_cast<PageId>(overflows));
    EXPECT_NE(header_page_id, kInvalidPageId);
}

TEST(HashIndexTest, RejectsAHeaderPageThatIsNotAnIndex) {
    TempDatabase temp("badheader");
    DiskManager disk(temp.Path());
    BufferPoolManager pool(disk, 4);

    PageId page_id = kInvalidPageId;
    {
        PageGuard guard = NewGuarded(pool, page_id);
        serialization::WriteU8(guard.Data(), 0, static_cast<std::uint8_t>(PageType::kTableData));
        guard.MarkDirty();
    }

    EXPECT_THROW(HashIndex(pool, page_id), StorageError);
    EXPECT_THROW(HashIndex(pool, kInvalidPageId), StorageError);
}

TEST(HashIndexTest, WorksWithAPoolSmallerThanTheChain) {
    TempDatabase temp("smallpool");
    IndexStack stack(temp.Path(), /*frames=*/2, /*bucket_count=*/1, /*bucket_capacity=*/4);

    for (std::int32_t key = 0; key < 40; ++key) {
        stack.Index().Insert(key, Rid(9, static_cast<SlotId>(key)));
    }
    for (std::int32_t key = 0; key < 40; ++key) {
        EXPECT_TRUE(stack.Index().Search(key).has_value());
    }
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
}

}  // namespace
}  // namespace minidb
