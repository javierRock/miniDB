#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::TempDatabase;

constexpr const char* kCreate =
    "CREATE TABLE students ("
    "  id INT PRIMARY KEY,"
    "  name VARCHAR(50),"
    "  age INT,"
    "  career VARCHAR(50))";

std::set<std::int32_t> IdsOf(const QueryResult& result) {
    std::set<std::int32_t> ids;
    for (const Record& row : result.rows) {
        ids.insert(std::get<std::int32_t>(row.GetValue(0)));
    }
    return ids;
}

// The full lifecycle the system is meant to demonstrate.
TEST(IntegrationTest, CreateInsertQueryUpdateDeleteThenReopen) {
    TempDatabase temp("lifecycle");

    {
        Database db(temp.Path());
        EXPECT_FALSE(db.HasTable());

        (void)db.Execute(kCreate);
        EXPECT_TRUE(db.HasTable());

        (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación')");
        (void)db.Execute("INSERT INTO students VALUES (2, 'Luis', 22, 'Ingeniería de Sistemas')");
        (void)db.Execute("INSERT INTO students VALUES (9, 'María', 19, 'Ingeniería de Software')");

        EXPECT_EQ(db.Execute("SELECT * FROM students").rows.size(), 3u);
        EXPECT_EQ(db.Execute("SELECT * FROM students WHERE id = 1").rows.size(), 1u);
        EXPECT_EQ(IdsOf(db.Execute("SELECT * FROM students WHERE age >= 20")),
                  (std::set<std::int32_t>{1, 2}));

        EXPECT_EQ(db.Execute("UPDATE students SET age = 21 WHERE id = 1").affected_rows, 1u);
        EXPECT_EQ(db.Execute("DELETE FROM students WHERE id = 2").affected_rows, 1u);

        db.Flush();
    }

    // Reopen: everything must be there, including the index.
    {
        Database db(temp.Path());
        EXPECT_TRUE(db.HasTable());
        EXPECT_EQ(db.GetCatalog().TableName(), "students");
        EXPECT_EQ(db.GetCatalog().RecordCount(), 2u);

        const QueryResult all = db.Execute("SELECT * FROM students");
        EXPECT_EQ(IdsOf(all), (std::set<std::int32_t>{1, 9}));

        // The update survived.
        const QueryResult ana = db.Execute("SELECT * FROM students WHERE id = 1");
        ASSERT_EQ(ana.rows.size(), 1u);
        EXPECT_EQ(std::get<std::int32_t>(ana.rows[0].GetValue(2)), 21);
        EXPECT_EQ(ValueToString(ana.rows[0].GetValue(3)), "Ciencia de la Computación");

        // The index survived: this lookup used it, and the deleted row is gone
        // from it too.
        EXPECT_EQ(ana.plan[1], "IndexScanOperator");
        EXPECT_TRUE(db.Execute("SELECT * FROM students WHERE id = 2").rows.empty());
    }
}

// Data written but never explicitly flushed must still reach the disk when the
// Database goes away, which is what makes `minidb db < script.sql` work.
TEST(IntegrationTest, DestructorFlushesWithoutAnExplicitCall) {
    TempDatabase temp("implicitflush");

    {
        Database db(temp.Path());
        (void)db.Execute(kCreate);
        (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'C')");
        // No Flush() here on purpose.
    }

    Database reopened(temp.Path());
    EXPECT_EQ(reopened.Execute("SELECT * FROM students").rows.size(), 1u);
}

TEST(IntegrationTest, DuplicatePrimaryKeyLeavesTheTableUnchanged) {
    TempDatabase temp("duplicate");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'C')");

    EXPECT_THROW((void)db.Execute("INSERT INTO students VALUES (1, 'Otra', 30, 'X')"), QueryError);

    const QueryResult all = db.Execute("SELECT * FROM students");
    ASSERT_EQ(all.rows.size(), 1u);
    EXPECT_EQ(ValueToString(all.rows[0].GetValue(1)), "Ana");  // the original row
    EXPECT_EQ(db.GetCatalog().RecordCount(), 1u);
}

TEST(IntegrationTest, DeleteRemovesTheRowFromTheIndexAsWell) {
    TempDatabase temp("deleteindex");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    for (std::int32_t i = 0; i < 50; ++i) {
        (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'N', 20, 'C')");
    }
    (void)db.Execute("DELETE FROM students WHERE id = 25");

    // Looked up through the index, so a stale entry would surface here.
    const QueryResult gone = db.Execute("SELECT * FROM students WHERE id = 25");
    EXPECT_EQ(gone.plan[1], "IndexScanOperator");
    EXPECT_TRUE(gone.rows.empty());

    // The key can be inserted again, which only works if it really left.
    EXPECT_NO_THROW((void)db.Execute("INSERT INTO students VALUES (25, 'Nueva', 21, 'C')"));
    EXPECT_EQ(db.Execute("SELECT * FROM students").rows.size(), 50u);
}

// An update that grows a record past what its page holds relocates it. The
// index has to follow, or the entry would point at a slot that is now free.
TEST(IntegrationTest, RelocatingUpdateKeepsTheIndexPointingAtTheRecord) {
    TempDatabase temp("relocate");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    // Short rows, packed tight, so growing one has nowhere to go in its page.
    for (std::int32_t i = 0; i < 400; ++i) {
        (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'n', 20, 'c')");
    }

    const std::string long_name(50, 'A');
    const std::string long_career(50, 'B');
    for (std::int32_t i = 0; i < 100; ++i) {
        (void)db.Execute("UPDATE students SET name = '" + long_name + "', career = '" +
                         long_career + "' WHERE id = " + std::to_string(i));
    }

    // Every relocated row must still be reachable through the index.
    for (std::int32_t i = 0; i < 100; ++i) {
        const QueryResult found =
            db.Execute("SELECT * FROM students WHERE id = " + std::to_string(i));
        ASSERT_EQ(found.plan[1], "IndexScanOperator");
        ASSERT_EQ(found.rows.size(), 1u) << "se perdió la fila " << i << " tras reubicarse";
        EXPECT_EQ(ValueToString(found.rows[0].GetValue(1)), long_name);
    }
    EXPECT_EQ(db.Execute("SELECT * FROM students").rows.size(), 400u);
}

// Table and index must agree in both directions: no row without an entry, and
// no entry without a row.
TEST(IntegrationTest, TableAndIndexStayConsistentUnderMixedTraffic) {
    TempDatabase temp("consistency");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    for (std::int32_t i = 0; i < 300; ++i) {
        (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) +
                         ", 'Estudiante " + std::to_string(i) + "', " +
                         std::to_string(18 + i % 10) + ", 'Ingeniería')");
    }
    (void)db.Execute("DELETE FROM students WHERE age = 18");
    (void)db.Execute("UPDATE students SET career = 'Ciencia de la Computación' WHERE age >= 25");
    for (std::int32_t i = 300; i < 350; ++i) {
        (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'N', 20, 'C')");
    }

    const QueryResult all = db.Execute("SELECT * FROM students");
    const std::set<std::int32_t> live = IdsOf(all);

    // Every live row is findable through the index.
    for (std::int32_t id : live) {
        const QueryResult found =
            db.Execute("SELECT * FROM students WHERE id = " + std::to_string(id));
        ASSERT_EQ(found.plan[1], "IndexScanOperator");
        EXPECT_EQ(found.rows.size(), 1u) << "la fila " << id << " no está en el índice";
    }
    // No deleted row is still findable.
    for (std::int32_t id = 0; id < 350; ++id) {
        if (live.contains(id)) {
            continue;
        }
        EXPECT_TRUE(db.Execute("SELECT * FROM students WHERE id = " + std::to_string(id))
                        .rows.empty())
            << "la fila borrada " << id << " sigue en el índice";
    }
    EXPECT_EQ(db.GetCatalog().RecordCount(), all.rows.size());
}

// Space freed by DELETE has to come back into use, or the file would grow
// without bound across delete/insert cycles.
TEST(IntegrationTest, FileDoesNotGrowAcrossDeleteAndReinsertCycles) {
    TempDatabase temp("nogrowth");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    auto load = [&db]() {
        for (std::int32_t i = 0; i < 300; ++i) {
            (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) +
                             ", 'Estudiante " + std::to_string(i) + "', 20, 'Ingeniería')");
        }
    };

    load();
    db.Flush();
    const std::uint64_t size_after_first_load = db.Disk().FileSize();

    for (int round = 0; round < 3; ++round) {
        EXPECT_EQ(db.Execute("DELETE FROM students").affected_rows, 300u);
        EXPECT_TRUE(db.Execute("SELECT * FROM students").rows.empty());
        load();
        EXPECT_EQ(db.Execute("SELECT * FROM students").rows.size(), 300u);
    }
    db.Flush();

    EXPECT_EQ(db.Disk().FileSize(), size_after_first_load);
}

// Nothing may stay pinned once a statement finishes; a leak would exhaust the
// pool a few statements later.
TEST(IntegrationTest, NoStatementLeavesAPagePinned) {
    TempDatabase temp("pins");
    Database db(temp.Path(), /*buffer_pool_frames=*/4);

    const std::vector<std::string> statements = {
        kCreate,
        "INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación')",
        "INSERT INTO students VALUES (2, 'Luis', 22, 'Ingeniería')",
        "SELECT * FROM students",
        "SELECT * FROM students WHERE id = 1",
        "SELECT id, name FROM students WHERE age >= 20",
        "UPDATE students SET age = 21 WHERE id = 1",
        "UPDATE students SET age = 30",
        "DELETE FROM students WHERE id = 2",
        "DELETE FROM students",
    };

    for (const std::string& sql : statements) {
        (void)db.Execute(sql);
        EXPECT_TRUE(db.Pool().AllPagesUnpinned()) << "quedó una página fijada tras: " << sql;
    }

    // Failed statements must not leak either.
    EXPECT_THROW((void)db.Execute("SELECT * FROM inexistente"), QueryError);
    EXPECT_TRUE(db.Pool().AllPagesUnpinned());
    EXPECT_THROW((void)db.Execute("INSERT INTO students VALUES (1)"), QueryError);
    EXPECT_TRUE(db.Pool().AllPagesUnpinned());
}

TEST(IntegrationTest, RejectsUnknownTablesAndASecondCreate) {
    TempDatabase temp("tables");
    Database db(temp.Path());

    EXPECT_THROW((void)db.Execute("SELECT * FROM students"), QueryError);

    (void)db.Execute(kCreate);
    EXPECT_THROW((void)db.Execute(kCreate), QueryError);
    EXPECT_THROW((void)db.Execute("CREATE TABLE otra (id INT PRIMARY KEY)"), QueryError);
    EXPECT_THROW((void)db.Execute("SELECT * FROM profesores"), QueryError);
    EXPECT_NO_THROW((void)db.Execute("SELECT * FROM STUDENTS"));  // case-insensitive
}

TEST(IntegrationTest, RejectsUpdatingThePrimaryKey) {
    TempDatabase temp("pkupdate");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'C')");

    EXPECT_THROW((void)db.Execute("UPDATE students SET id = 5 WHERE id = 1"), QueryError);
    EXPECT_EQ(db.Execute("SELECT * FROM students WHERE id = 1").rows.size(), 1u);
}

TEST(IntegrationTest, RejectsValuesThatBreakTheSchema) {
    TempDatabase temp("schema");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    EXPECT_THROW((void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20)"), QueryError);
    EXPECT_THROW((void)db.Execute("INSERT INTO students VALUES ('uno', 'Ana', 20, 'C')"),
                 QueryError);
    EXPECT_THROW((void)db.Execute("INSERT INTO students VALUES (1, '" + std::string(51, 'x') +
                                  "', 20, 'C')"),
                 QueryError);
    EXPECT_TRUE(db.Execute("SELECT * FROM students").rows.empty());
}

TEST(IntegrationTest, CreateTableValidatesTheColumnList) {
    TempDatabase temp("badcreate");
    Database db(temp.Path());

    EXPECT_THROW((void)db.Execute("CREATE TABLE t (id INT, name VARCHAR(10))"), QueryError);
    EXPECT_THROW((void)db.Execute("CREATE TABLE t (id VARCHAR(10) PRIMARY KEY)"), QueryError);
    EXPECT_THROW((void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, ID INT)"), QueryError);
    EXPECT_FALSE(db.HasTable());
}

// The physical layout the design predicts, checked against the real file.
TEST(IntegrationTest, PageLayoutAfterCreateTableMatchesTheDesign) {
    TempDatabase temp("layout");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    db.Flush();

    // page 0 file header, page 1 catalog, page 2 index header,
    // pages 3..18 the 16 buckets, page 19 the first table page.
    EXPECT_EQ(db.Disk().PageCount(), 20u);
    EXPECT_EQ(db.Disk().FileSize(), 20u * kPageSize);
    EXPECT_EQ(db.Disk().FileSize(), 81920u);
    EXPECT_EQ(db.IndexPageCount(), 17u);  // header plus 16 buckets
    EXPECT_EQ(db.TablePageCount(), 1u);
}

TEST(IntegrationTest, TheFileIsSelfIdentifyingOnDisk) {
    TempDatabase temp("magic");
    {
        Database db(temp.Path());
        (void)db.Execute(kCreate);
        (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'C')");
    }

    std::ifstream raw(temp.Path(), std::ios::binary);
    std::string magic(4, '\0');
    raw.read(magic.data(), 4);
    EXPECT_EQ(magic, "MIND");
    EXPECT_EQ(temp.SizeOnDisk() % kPageSize, 0u);
}

// --- Configuration file --------------------------------------------------

TEST(ConfigTest, MissingFileFallsBackToDefaults) {
    const DatabaseConfig config = DatabaseConfig::Load("no-existe.conf");
    EXPECT_EQ(config.buffer_pool_frames, kDefaultBufferPoolFrames);
    EXPECT_EQ(config.data_file, "data/minidb.db");
}

TEST(ConfigTest, ReadsKeysAndIgnoresCommentsAndBlankLines) {
    TempDatabase temp("config");
    const auto config_path = temp.Path().parent_path() / "minidb.conf";
    {
        std::ofstream file(config_path);
        file << "# comentario\n\n"
             << "data_file = data/otra.db\n"
             << "buffer_pool_frames=16\n";
    }

    const DatabaseConfig config = DatabaseConfig::Load(config_path);
    EXPECT_EQ(config.data_file, "data/otra.db");
    EXPECT_EQ(config.buffer_pool_frames, 16u);
}

TEST(ConfigTest, RejectsMalformedLinesAndBadValues) {
    TempDatabase temp("badconfig");
    const auto config_path = temp.Path().parent_path() / "bad.conf";

    auto write = [&config_path](const std::string& contents) {
        std::ofstream file(config_path);
        file << contents;
    };

    write("esto no es una asignación\n");
    EXPECT_THROW((void)DatabaseConfig::Load(config_path), QueryError);

    write("buffer_pool_frames=0\n");
    EXPECT_THROW((void)DatabaseConfig::Load(config_path), QueryError);

    write("buffer_pool_frames=muchos\n");
    EXPECT_THROW((void)DatabaseConfig::Load(config_path), QueryError);

    write("opcion_desconocida=1\n");
    EXPECT_THROW((void)DatabaseConfig::Load(config_path), QueryError);
}

TEST(ConfigTest, FrameCountFromConfigIsActuallyUsed) {
    TempDatabase temp("frames");
    Database db(temp.Path(), /*buffer_pool_frames=*/5);
    EXPECT_EQ(db.Pool().FrameCount(), 5u);
}

}  // namespace
}  // namespace minidb
