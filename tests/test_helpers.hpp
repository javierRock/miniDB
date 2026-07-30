#pragma once

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "minidb/common/constants.hpp"
#include "minidb/index/hash_index.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb::testing {

/// Gives each test its own database file under the system temp directory and
/// deletes it afterwards, so tests never share state and leave nothing behind.
class TempDatabase {
public:
    explicit TempDatabase(const std::string& label) {
        static int counter = 0;
        directory_ = std::filesystem::temp_directory_path() /
                     ("minidb_test_" + label + "_" + std::to_string(++counter) + "_" +
                      std::to_string(::getpid()));
        std::filesystem::create_directories(directory_);
        path_ = directory_ / "test.db";
    }

    ~TempDatabase() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    TempDatabase(const TempDatabase&) = delete;
    TempDatabase& operator=(const TempDatabase&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    [[nodiscard]] std::uint64_t SizeOnDisk() const {
        std::error_code ignored;
        const auto size = std::filesystem::file_size(path_, ignored);
        return ignored ? 0 : size;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

/// Builds the layers by hand so the operators can be driven directly, rather
/// than only through a whole SQL statement.
class OperatorStack {
public:
    explicit OperatorStack(const std::filesystem::path& path, std::size_t frames = 8) {
        disk_ = std::make_unique<DiskManager>(path);
        pool_ = std::make_unique<BufferPoolManager>(*disk_, frames);
        catalog_ = std::make_unique<Catalog>(*pool_, true);
        index_header_ = HashIndex::Create(*pool_);
        const PageId first_page = TableHeap::CreateFirstPage(*pool_);
        catalog_->CreateTable("students", Schema({{"id", ColumnType::kInteger, 0, true},
                                                  {"name", ColumnType::kVarchar, 50, false},
                                                  {"age", ColumnType::kInteger, 0, false},
                                                  {"career", ColumnType::kVarchar, 50, false}}),
                              first_page, index_header_);
        heap_ = std::make_unique<TableHeap>(*pool_, *catalog_);
        index_ = std::make_unique<HashIndex>(*pool_, index_header_);
    }

    void Add(std::int32_t id, const std::string& name, std::int32_t age,
             const std::string& career) {
        const RecordId rid = heap_->InsertRecord(Record({id, name, age, career}));
        index_->Insert(id, rid);
        catalog_->IncrementRecordCount();
    }

    TableHeap& Heap() { return *heap_; }
    HashIndex& Index() { return *index_; }
    const Schema& GetSchema() { return catalog_->GetSchema(); }
    BufferPoolManager& Pool() { return *pool_; }

private:
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPoolManager> pool_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<TableHeap> heap_;
    std::unique_ptr<HashIndex> index_;
    PageId index_header_ = kInvalidPageId;
};

/// Fills a page with a byte pattern derived from `seed`, so a page read back
/// from disk can be checked byte for byte.
inline std::array<std::byte, kPageSize> MakePattern(std::uint8_t seed) {
    std::array<std::byte, kPageSize> page{};
    for (std::size_t i = 0; i < kPageSize; ++i) {
        page[i] = static_cast<std::byte>((seed + i) % 251);
    }
    return page;
}

}  // namespace minidb::testing
