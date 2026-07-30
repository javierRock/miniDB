#include "minidb/database/database.hpp"

#include <charconv>
#include <chrono>
#include <fstream>
#include <utility>

#include "minidb/parser/parser.hpp"

namespace minidb {
namespace {

/// Trims ASCII whitespace from both ends.
std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

DatabaseConfig DatabaseConfig::Load(const std::filesystem::path& path) {
    DatabaseConfig config;

    std::ifstream file(path);
    if (!file) {
        return config;  // absent file simply means "use the defaults"
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto separator = trimmed.find('=');
        if (separator == std::string::npos) {
            throw QueryError("Línea " + std::to_string(line_number) + " de " + path.string() +
                             ": se esperaba 'clave=valor'");
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));

        if (key == "data_file") {
            config.data_file = value;
        } else if (key == "buffer_pool_frames") {
            std::size_t frames = 0;
            const auto* first = value.data();
            const auto* last = first + value.size();
            if (std::from_chars(first, last, frames).ec != std::errc{} || frames == 0) {
                throw QueryError("Línea " + std::to_string(line_number) + " de " + path.string() +
                                 ": buffer_pool_frames debe ser un entero mayor que cero");
            }
            config.buffer_pool_frames = frames;
        } else {
            throw QueryError("Línea " + std::to_string(line_number) + " de " + path.string() +
                             ": opción desconocida '" + key + "'");
        }
    }
    return config;
}

Database::Database(std::filesystem::path path, std::size_t buffer_pool_frames)
    : path_(std::move(path)) {
    disk_ = std::make_unique<DiskManager>(path_);
    pool_ = std::make_unique<BufferPoolManager>(*disk_, buffer_pool_frames);

    // The catalog always occupies page 1, so it is created immediately after
    // the file header and before anything else can allocate.
    catalog_ = std::make_unique<Catalog>(*pool_, disk_->WasCreated());

    if (catalog_->HasTable()) {
        OpenExistingTable();
    }
}

Database::~Database() {
    // Best effort: a destructor must not throw. This is what makes
    // `minidb data.db < script.sql` persist when stdin simply ends.
    try {
        Flush();
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch)
    }
}

void Database::OpenExistingTable() {
    heap_ = std::make_unique<TableHeap>(*pool_, *catalog_);
    index_ = std::make_unique<HashIndex>(*pool_, catalog_->IndexHeaderPageId());
    engine_ = std::make_unique<ExecutionEngine>(*catalog_, *heap_, *index_);
}

QueryResult Database::CreateTable(const CreateTableStatement& statement) {
    if (catalog_->HasTable()) {
        throw QueryError("Ya existe la tabla '" + catalog_->TableName() +
                         "'; este sistema admite una sola tabla");
    }

    // Building the Schema validates the column list: one INT primary key, no
    // duplicate names, VARCHAR lengths in range.
    const Schema schema(statement.columns);

    // Allocated in this order so the file layout is predictable: page 2 is the
    // index header, pages 3..18 the buckets, and the table starts right after.
    const PageId index_header_page_id = HashIndex::Create(*pool_);
    const PageId first_table_page_id = TableHeap::CreateFirstPage(*pool_);

    catalog_->CreateTable(statement.table_name, schema, first_table_page_id,
                          index_header_page_id);
    OpenExistingTable();

    QueryResult result;
    result.message = "Tabla '" + statement.table_name + "' creada con " +
                     std::to_string(schema.ColumnCount()) + " columnas.";
    return result;
}

QueryResult Database::Execute(const std::string& sql) {
    // Measured here rather than inside the engine so the reported time covers
    // parsing too, which is what the user actually waited for, and so the engine
    // keeps knowing nothing about clocks.
    const BufferPoolStatistics before = pool_->Statistics();
    const auto started = std::chrono::steady_clock::now();

    QueryResult result = ExecuteParsed(sql);

    const auto finished = std::chrono::steady_clock::now();
    const BufferPoolStatistics after = pool_->Statistics();

    result.elapsed_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    result.pages_read = after.disk_reads - before.disk_reads;
    result.buffer_hits = after.hits - before.hits;
    result.buffer_misses = after.misses - before.misses;
    return result;
}

QueryResult Database::ExecuteParsed(const std::string& sql) {
    Statement statement = Parser::Parse(sql);

    // CREATE TABLE is handled here rather than in the engine, because it brings
    // the heap and the index into existence and the engine holds references to
    // them.
    if (const auto* create = std::get_if<CreateTableStatement>(&statement)) {
        return CreateTable(*create);
    }

    if (engine_ == nullptr) {
        throw QueryError("No existe ninguna tabla; use CREATE TABLE primero");
    }
    return engine_->Execute(statement);
}

void Database::Flush() {
    if (catalog_ != nullptr) {
        catalog_->Flush();
    }
    if (pool_ != nullptr) {
        pool_->FlushAllPages();
    }
}

void Database::SetIndexEnabled(bool enabled) {
    if (engine_ == nullptr) {
        throw QueryError("No existe ninguna tabla; use CREATE TABLE primero");
    }
    engine_->SetIndexEnabled(enabled);
}

bool Database::IndexEnabled() const {
    // With no table there is no index either, so nothing can be using one.
    return engine_ != nullptr && engine_->IndexEnabled();
}

std::uint32_t Database::TablePageCount() const {
    return heap_ == nullptr ? 0 : heap_->PageCountInChain();
}

std::uint32_t Database::IndexPageCount() const {
    return index_ == nullptr ? 0 : index_->TotalPageCount();
}

}  // namespace minidb
