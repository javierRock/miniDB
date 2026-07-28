#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "minidb/buffer/buffer_pool_manager.hpp"
#include "minidb/catalog/catalog.hpp"
#include "minidb/execution/execution_engine.hpp"
#include "minidb/index/hash_index.hpp"
#include "minidb/parser/statement.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb {

/// Settings read from minidb.conf, a plain `key=value` text file.
///
/// Keeping the data file and the pool size out of the binary means the
/// database directory is self-describing, and it gives the deliverable the
/// configuration file it is expected to have alongside the binary data file.
struct DatabaseConfig {
    std::filesystem::path data_file = "data/minidb.db";
    std::size_t buffer_pool_frames = kDefaultBufferPoolFrames;

    /// Reads a config file. Missing files are not an error: the defaults above
    /// are used. Throws QueryError on a malformed or out-of-range value.
    [[nodiscard]] static DatabaseConfig Load(const std::filesystem::path& path);
};

/// Facade over the whole system: one object owns the file, the buffer pool, the
/// catalog, the table, the index and the execution engine.
///
/// This is the only class the CLI talks to, which keeps main.cpp free of any
/// database logic.
class Database {
public:
    /// Opens `path`, creating and initialising it when it does not exist.
    explicit Database(std::filesystem::path path,
                      std::size_t buffer_pool_frames = kDefaultBufferPoolFrames);

    /// Flushes everything. Doing it here means a redirected script that simply
    /// reaches end of input still persists its work.
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Parses and runs one SQL statement.
    [[nodiscard]] QueryResult Execute(const std::string& sql);

    /// Writes every dirty page and the catalog to disk.
    void Flush();

    [[nodiscard]] bool HasTable() const { return catalog_->HasTable(); }
    [[nodiscard]] const Catalog& GetCatalog() const { return *catalog_; }
    [[nodiscard]] const BufferPoolManager& Pool() const { return *pool_; }
    [[nodiscard]] BufferPoolManager& Pool() { return *pool_; }
    [[nodiscard]] DiskManager& Disk() { return *disk_; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    /// Pages currently used by the table's chain and by the index, for `.pages`.
    [[nodiscard]] std::uint32_t TablePageCount() const;
    [[nodiscard]] std::uint32_t IndexPageCount() const;

private:
    [[nodiscard]] QueryResult CreateTable(const CreateTableStatement& statement);
    void OpenExistingTable();

    std::filesystem::path path_;
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPoolManager> pool_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<TableHeap> heap_;
    std::unique_ptr<HashIndex> index_;
    std::unique_ptr<ExecutionEngine> engine_;
};

}  // namespace minidb
