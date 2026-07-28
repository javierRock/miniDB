#include <unistd.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"

namespace minidb {
namespace {

/// Display width of a UTF-8 string, counting characters rather than bytes.
///
/// Without this the table columns misalign on the accented Spanish data, since
/// 'ó' occupies two bytes but one column.
std::size_t DisplayWidth(const std::string& text) {
    std::size_t width = 0;
    for (char c : text) {
        // Continuation bytes (10xxxxxx) do not start a new character.
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++width;
        }
    }
    return width;
}

std::string Pad(const std::string& text, std::size_t width) {
    const std::size_t used = DisplayWidth(text);
    return text + std::string(width > used ? width - used : 0, ' ');
}

/// Prints a result as an ASCII table.
void PrintRows(const QueryResult& result) {
    if (result.column_names.empty()) {
        return;
    }

    std::vector<std::size_t> widths;
    for (const std::string& name : result.column_names) {
        widths.push_back(DisplayWidth(name));
    }
    for (const Record& row : result.rows) {
        for (std::size_t i = 0; i < row.Size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], DisplayWidth(ValueToString(row.GetValue(i))));
        }
    }

    auto separator = [&widths]() {
        std::string line = "+";
        for (std::size_t width : widths) {
            line += std::string(width + 2, '-') + "+";
        }
        return line;
    };

    std::cout << separator() << '\n' << "|";
    for (std::size_t i = 0; i < result.column_names.size(); ++i) {
        std::cout << ' ' << Pad(result.column_names[i], widths[i]) << " |";
    }
    std::cout << '\n' << separator() << '\n';

    for (const Record& row : result.rows) {
        std::cout << "|";
        for (std::size_t i = 0; i < row.Size() && i < widths.size(); ++i) {
            std::cout << ' ' << Pad(ValueToString(row.GetValue(i)), widths[i]) << " |";
        }
        std::cout << '\n';
    }
    std::cout << separator() << '\n';
}

void PrintPlan(const QueryResult& result) {
    if (result.plan.empty()) {
        return;
    }
    std::cout << "Plan físico (modelo Volcano): ";
    for (std::size_t i = 0; i < result.plan.size(); ++i) {
        std::cout << result.plan[i];
        if (i + 1 < result.plan.size()) {
            std::cout << " <- ";
        }
    }
    std::cout << '\n';
}

void PrintHelp() {
    std::cout << R"(SQL admitido:
  CREATE TABLE <tabla> (<col> INT [PRIMARY KEY] | <col> VARCHAR(<n>), ...);
  INSERT INTO <tabla> VALUES (<v1>, <v2>, ...);
  SELECT * | <col>, ... FROM <tabla> [WHERE <col> <op> <valor>];
  UPDATE <tabla> SET <col> = <valor> [, ...] [WHERE <col> <op> <valor>];
  DELETE FROM <tabla> [WHERE <col> <op> <valor>];

Operadores de comparación: =  !=  <  <=  >  >=
Las palabras clave no distinguen mayúsculas; el ';' final es opcional.

Comandos internos:
  .help     Esta ayuda
  .schema   Esquema de la tabla
  .pages    Páginas del archivo por tipo
  .buffer   Frames del Buffer Pool y estadísticas
  .files    Archivos en disco y su tamaño
  .flush    Sincroniza todas las páginas sucias
  .exit     Sincroniza y sale

Ejemplos:
  CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), age INT, career VARCHAR(50));
  INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación');
  SELECT * FROM students WHERE id = 1;
  SELECT * FROM students WHERE age >= 20;
)";
}

void PrintSchema(const Database& db) {
    if (!db.HasTable()) {
        std::cout << "No hay ninguna tabla definida.\n";
        return;
    }

    const Catalog& catalog = db.GetCatalog();
    const Schema& schema = catalog.GetSchema();

    std::cout << "Tabla: " << catalog.TableName() << " (" << catalog.RecordCount()
              << " registros)\n";
    for (const Column& column : schema.Columns()) {
        std::cout << "  " << column.name << ' '
                  << (column.type == ColumnType::kInteger
                          ? "INT"
                          : "VARCHAR(" + std::to_string(column.max_length) + ")")
                  << (column.is_primary_key ? " PRIMARY KEY" : "") << '\n';
    }
}

void PrintPages(Database& db) {
    std::cout << "Tamaño de página:      " << kPageSize << " bytes\n"
              << "Páginas totales:       " << db.Disk().PageCount() << '\n'
              << "  Cabecera del archivo: 1 (página 0)\n"
              << "  Catálogo:             1 (página 1)\n"
              << "  Índice hash:          " << db.IndexPageCount()
              << " (cabecera + buckets + overflow)\n"
              << "  Datos de la tabla:    " << db.TablePageCount() << '\n';

    const PageId free_head = db.Disk().FreePageHead();
    std::cout << "Lista de páginas libres: "
              << (free_head == kInvalidPageId ? std::string("vacía")
                                              : "cabeza en la página " + std::to_string(free_head))
              << '\n';
}

void PrintBuffer(const Database& db) {
    const BufferPoolManager& pool = db.Pool();
    const BufferPoolStatistics& stats = pool.Statistics();

    std::cout << "Frames del Buffer Pool: " << pool.FrameCount() << " (política LRU)\n";
    std::cout << "+-------+---------+-------+-----------+\n"
              << "| frame | página  | pin   | modificada|\n"
              << "+-------+---------+-------+-----------+\n";

    std::size_t frame_id = 0;
    for (const Frame& frame : pool.Frames()) {
        std::cout << "| " << Pad(std::to_string(frame_id), 5) << " | "
                  << Pad(frame.is_occupied ? std::to_string(frame.page_id) : "-", 7) << " | "
                  << Pad(std::to_string(frame.pin_count), 5) << " | "
                  << Pad(frame.is_dirty ? "sí" : "no", 9) << " |\n";
        ++frame_id;
    }
    std::cout << "+-------+---------+-------+-----------+\n";

    const std::uint64_t accesses = stats.hits + stats.misses;
    std::cout << "Aciertos (hits):    " << stats.hits << '\n'
              << "Fallos (misses):    " << stats.misses << '\n'
              << "Reemplazos (LRU):   " << stats.evictions << '\n'
              << "Lecturas de disco:  " << stats.disk_reads << '\n'
              << "Escrituras a disco: " << stats.disk_writes << '\n';
    if (accesses > 0) {
        std::cout << "Tasa de aciertos:   " << (stats.hits * 100 / accesses) << "%\n";
    }
}

void PrintFiles(Database& db, const std::filesystem::path& config_path) {
    std::cout << "Archivo de datos:  " << std::filesystem::absolute(db.Path()).string() << '\n'
              << "  Tamaño:          " << db.Disk().FileSize() << " bytes ("
              << db.Disk().PageCount() << " páginas de " << kPageSize << ")\n";

    std::error_code error;
    const auto config_size = std::filesystem::file_size(config_path, error);
    std::cout << "Archivo de config: " << std::filesystem::absolute(config_path).string() << '\n'
              << "  Tamaño:          "
              << (error ? std::string("no existe (se usan los valores por defecto)")
                        : std::to_string(config_size) + " bytes")
              << '\n';
    std::cout << "El sistema no genera archivos temporales: no hay operadores bloqueantes\n"
                 "(ORDER BY, GROUP BY o JOIN) que necesiten volcar resultados intermedios.\n";
}

/// Runs a dot command. Returns false when the session should end.
bool RunInternalCommand(const std::string& command, Database& db,
                        const std::filesystem::path& config_path) {
    if (command == ".exit" || command == ".quit") {
        return false;
    }
    if (command == ".help") {
        PrintHelp();
    } else if (command == ".schema") {
        PrintSchema(db);
    } else if (command == ".pages") {
        PrintPages(db);
    } else if (command == ".buffer") {
        PrintBuffer(db);
    } else if (command == ".files") {
        PrintFiles(db, config_path);
    } else if (command == ".flush") {
        db.Flush();
        std::cout << "Páginas sincronizadas con el disco.\n";
    } else {
        std::cout << "Comando desconocido: " << command << ". Use .help\n";
    }
    return true;
}

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// Reads statements from `input` and runs them.
///
/// A statement ends at a semicolon, so it may span several lines. Errors the
/// user can fix are reported and the session continues; anything else
/// propagates and ends the process with a non-zero status.
int RunSession(std::istream& input, Database& db, const std::filesystem::path& config_path,
               bool interactive) {
    std::string pending;
    std::string line;

    while (true) {
        if (interactive) {
            std::cout << (pending.empty() ? "minidb> " : "   ...> ") << std::flush;
        }
        if (!std::getline(input, line)) {
            break;
        }

        const std::string trimmed = Trim(line);
        if (pending.empty() && (trimmed.empty() || trimmed.starts_with("--"))) {
            continue;
        }
        if (pending.empty() && trimmed.starts_with(".")) {
            if (!RunInternalCommand(trimmed, db, config_path)) {
                break;
            }
            continue;
        }

        pending += (pending.empty() ? "" : " ") + trimmed;
        if (!pending.ends_with(";")) {
            continue;  // keep reading: the statement is not finished
        }

        const std::string sql = pending;
        pending.clear();

        if (!interactive) {
            std::cout << "minidb> " << sql << '\n';
        }
        try {
            const QueryResult result = db.Execute(sql);
            PrintPlan(result);
            PrintRows(result);
            std::cout << result.message << "\n\n";
        } catch (const QueryError& error) {
            std::cout << "Error: " << error.what() << "\n\n";
        }
    }

    if (!Trim(pending).empty()) {
        std::cout << "Aviso: se descartó una sentencia incompleta (falta ';').\n";
    }

    db.Flush();
    if (interactive) {
        std::cout << "\nBase de datos sincronizada. Hasta luego.\n";
    }
    return 0;
}

}  // namespace
}  // namespace minidb

int main(int argc, char** argv) {
    using namespace minidb;

    try {
        const std::filesystem::path config_path =
            (argc > 2) ? std::filesystem::path(argv[2]) : std::filesystem::path("minidb.conf");
        DatabaseConfig config = DatabaseConfig::Load(config_path);

        // An explicit path on the command line wins over the config file.
        if (argc > 1) {
            config.data_file = argv[1];
        }

        Database db(config.data_file, config.buffer_pool_frames);

        const bool interactive = ::isatty(0) != 0;
        if (interactive) {
            std::cout << "MiniDB iniciado correctamente\n"
                      << "Archivo: " << std::filesystem::absolute(db.Path()).string() << '\n'
                      << "Tamaño de página: " << kPageSize << " bytes\n"
                      << "Frames del Buffer Pool: " << db.Pool().FrameCount() << '\n'
                      << (db.HasTable()
                              ? "Tabla existente: " + db.GetCatalog().TableName() + " (" +
                                    std::to_string(db.GetCatalog().RecordCount()) + " registros)\n"
                              : "Base de datos vacía: use CREATE TABLE para empezar\n")
                      << "Escriba .help para ver la ayuda\n\n";
        }

        return RunSession(std::cin, db, config_path, interactive);

    } catch (const StorageError& error) {
        std::cerr << "Error de almacenamiento: " << error.what() << '\n';
        return 2;
    } catch (const QueryError& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error inesperado: " << error.what() << '\n';
        return 3;
    }
}
