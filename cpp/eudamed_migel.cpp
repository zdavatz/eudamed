// eudamed_migel.cpp — Match EUDAMED devices against Swiss MiGeL codes
// Build: g++ -std=c++20 -O2 cpp/eudamed_migel.cpp -lsqlite3 -o eudamed_migel
// Usage: ./eudamed_migel --db1 db/eudamed_devices.db --db2 db/eudamed_full_with_urls.db \
//          --migel-de xlsx/migel_0.csv --migel-fr xlsx/migel_1.csv --migel-it xlsx/migel_2.csv

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sqlite3.h>
#include "migel.hpp"

// ----------------------------- Helpers ---------------------------------------

static std::string date_stamp() {
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    std::chrono::year_month_day ymd{today};
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(ymd.day()) << "."
        << std::setw(2) << static_cast<unsigned>(ymd.month()) << "."
        << static_cast<int>(ymd.year());
    return oss.str();
}

static int count_non_empty(const std::vector<std::string>& row) {
    int n = 0;
    for (const auto& s : row)
        if (!s.empty()) ++n;
    return n;
}

// ----------------------------- CLI parsing ------------------------------------

struct Args {
    std::string db1;
    std::string db2;
    std::string migel_de;
    std::string migel_fr;
    std::string migel_it;
};

static Args parse_args(int argc, char* argv[]) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db1" && i + 1 < argc) args.db1 = argv[++i];
        else if (arg == "--db2" && i + 1 < argc) args.db2 = argv[++i];
        else if (arg == "--migel-de" && i + 1 < argc) args.migel_de = argv[++i];
        else if (arg == "--migel-fr" && i + 1 < argc) args.migel_fr = argv[++i];
        else if (arg == "--migel-it" && i + 1 < argc) args.migel_it = argv[++i];
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0]
                      << " --db1 <db> --db2 <db> --migel-de <csv> --migel-fr <csv> --migel-it <csv>\n"
                      << "\nMerges two EUDAMED SQLite DBs, matches devices against MiGeL codes,\n"
                      << "and outputs db/eudamed_migel_DD.MM.YYYY.db with matched products.\n"
                      << "\nGenerate CSVs from XLSX with:\n"
                      << "  ssconvert --export-type=Gnumeric_stf:stf_csv --export-file-per-sheet xlsx/migel.xlsx xlsx/migel_%n.csv\n";
            exit(0);
        }
    }
    if (args.db1.empty() || args.db2.empty() || args.migel_de.empty()) {
        std::cerr << "Error: --db1, --db2, and --migel-de are required.\n"
                  << "Run with --help for usage.\n";
        exit(1);
    }
    return args;
}

// ----------------------------- SQLite helpers ---------------------------------

/// Read column names from a table using PRAGMA table_info.
static std::vector<std::string> read_columns(sqlite3* db, const std::string& table) {
    std::vector<std::string> cols;
    std::string sql = "PRAGMA table_info(" + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error reading columns: " << sqlite3_errmsg(db) << "\n";
        return cols;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name) cols.emplace_back(name);
    }
    sqlite3_finalize(stmt);
    return cols;
}

/// Row type: column values indexed by position in the unified column list.
using Row = std::vector<std::string>;

/// Read all rows from a database, mapping columns to the unified column list.
/// Returns the number of rows read.
static size_t read_db_rows(
    const std::string& db_path,
    const std::vector<std::string>& unified_cols,
    std::unordered_map<std::string, Row>& rows,
    size_t uuid_col_idx)
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "Error opening " << db_path << ": " << sqlite3_errmsg(db) << "\n";
        return 0;
    }

    // Get this DB's columns
    auto db_cols = read_columns(db, "devices");

    // Build mapping: db column index -> unified column index
    std::unordered_map<std::string, size_t> unified_map;
    for (size_t i = 0; i < unified_cols.size(); ++i)
        unified_map[unified_cols[i]] = i;

    std::vector<int> col_mapping(db_cols.size(), -1); // db col idx -> unified col idx
    for (size_t i = 0; i < db_cols.size(); ++i) {
        auto it = unified_map.find(db_cols[i]);
        if (it != unified_map.end())
            col_mapping[i] = static_cast<int>(it->second);
    }

    // Read all rows
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT * FROM devices", -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error querying " << db_path << ": " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 0;
    }

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row(unified_cols.size());
        int ncols = sqlite3_column_count(stmt);

        for (int i = 0; i < ncols && i < static_cast<int>(col_mapping.size()); ++i) {
            if (col_mapping[i] < 0) continue;
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            if (val) row[col_mapping[i]] = val;
        }

        std::string uuid = row[uuid_col_idx];
        if (uuid.empty()) {
            // No uuid — still store with a synthetic key
            uuid = "__no_uuid_" + std::to_string(count);
        }

        auto it = rows.find(uuid);
        if (it == rows.end()) {
            rows[uuid] = std::move(row);
        } else {
            // Keep the row with more non-empty fields
            if (count_non_empty(row) > count_non_empty(it->second))
                it->second = std::move(row);
        }
        ++count;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

// ----------------------------- Main ------------------------------------------

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    // Step 1: Load MiGeL items from CSV files
    std::cout << "Loading MiGeL items from CSVs (DE: " << args.migel_de
              << ", FR: " << args.migel_fr << ", IT: " << args.migel_it << ") ...\n";
    auto migel_items = migel::parse_migel_items(args.migel_de, args.migel_fr, args.migel_it);
    std::cout << "   " << migel_items.size() << " MiGeL items loaded.\n";

    auto keyword_index = migel::build_keyword_index(migel_items);
    std::cout << "   " << keyword_index.size() << " unique keywords indexed.\n";

    // Step 2: Read column headers from both DBs and build unified column list
    sqlite3* tmp_db1 = nullptr;
    sqlite3* tmp_db2 = nullptr;
    sqlite3_open_v2(args.db1.c_str(), &tmp_db1, SQLITE_OPEN_READONLY, nullptr);
    sqlite3_open_v2(args.db2.c_str(), &tmp_db2, SQLITE_OPEN_READONLY, nullptr);

    auto cols1 = read_columns(tmp_db1, "devices");
    auto cols2 = read_columns(tmp_db2, "devices");
    sqlite3_close(tmp_db1);
    sqlite3_close(tmp_db2);

    // Build unified column list (preserve order from db1, append new cols from db2)
    std::vector<std::string> unified_cols = cols1;
    std::unordered_set<std::string> col_set(cols1.begin(), cols1.end());
    for (const auto& c : cols2) {
        if (col_set.find(c) == col_set.end()) {
            unified_cols.push_back(c);
            col_set.insert(c);
        }
    }

    std::cout << "   Unified columns: " << unified_cols.size()
              << " (db1: " << cols1.size() << ", db2: " << cols2.size() << ")\n";

    // Find uuid column index
    size_t uuid_idx = 0;
    for (size_t i = 0; i < unified_cols.size(); ++i) {
        if (unified_cols[i] == "uuid") { uuid_idx = i; break; }
    }

    // Find tradeName and manufacturerName column indices
    size_t tradeName_idx = SIZE_MAX;
    size_t mfr_idx = SIZE_MAX;
    for (size_t i = 0; i < unified_cols.size(); ++i) {
        if (unified_cols[i] == "tradeName") tradeName_idx = i;
        if (unified_cols[i] == "manufacturerName") mfr_idx = i;
    }

    // Step 3: Read and merge rows from both DBs
    std::unordered_map<std::string, Row> all_rows;
    all_rows.reserve(1000000);

    std::cout << "Reading " << args.db1 << " ...\n";
    size_t count1 = read_db_rows(args.db1, unified_cols, all_rows, uuid_idx);
    std::cout << "   " << count1 << " rows read, " << all_rows.size() << " unique.\n";

    std::cout << "Reading " << args.db2 << " ...\n";
    size_t count2 = read_db_rows(args.db2, unified_cols, all_rows, uuid_idx);
    std::cout << "   " << count2 << " rows read, " << all_rows.size() << " unique after merge.\n";

    // Step 4: Match against MiGeL
    std::cout << "Matching " << all_rows.size() << " devices against MiGeL ...\n";

    // Add MiGeL columns to the output schema
    std::vector<std::string> output_cols = unified_cols;
    output_cols.push_back("migel_position_nr");
    output_cols.push_back("migel_bezeichnung");
    output_cols.push_back("migel_limitation");

    std::vector<Row> matched_rows;
    matched_rows.reserve(50000); // rough estimate

    size_t processed = 0;
    size_t skipped_empty = 0;

    for (auto& [uuid, row] : all_rows) {
        std::string trade_name = (tradeName_idx < row.size()) ? row[tradeName_idx] : "";
        std::string mfr_name = (mfr_idx < row.size()) ? row[mfr_idx] : "";

        if (trade_name.empty()) {
            ++skipped_empty;
            ++processed;
            continue;
        }

        // Pass tradeName as all three language descriptions (it's typically English/brand)
        // Use manufacturerName as the brand parameter for additional context
        const migel::MigelItem* match = migel::find_best_migel_match(
            trade_name, trade_name, trade_name, mfr_name,
            migel_items, keyword_index);

        if (match) {
            Row out_row = row;
            out_row.resize(output_cols.size());
            out_row[unified_cols.size()]     = match->position_nr;
            out_row[unified_cols.size() + 1] = match->bezeichnung;
            out_row[unified_cols.size() + 2] = match->limitation;
            matched_rows.push_back(std::move(out_row));
        }

        ++processed;
        if (processed % 100000 == 0)
            std::cout << "   Processed: " << processed << " / " << all_rows.size() << "\r" << std::flush;
    }

    std::cout << "\nMatching complete:\n"
              << "   Total devices: " << all_rows.size() << "\n"
              << "   Skipped (empty tradeName): " << skipped_empty << "\n"
              << "   Matched to MiGeL: " << matched_rows.size() << "\n";

    // Step 5: Write output database
    std::string output_path = "db/eudamed_migel_" + date_stamp() + ".db";
    std::cout << "Writing output to " << output_path << " ...\n";

    sqlite3* out_db = nullptr;
    if (sqlite3_open(output_path.c_str(), &out_db) != SQLITE_OK) {
        std::cerr << "Error creating output DB: " << sqlite3_errmsg(out_db) << "\n";
        return 1;
    }

    // Create table
    std::string create_sql = "CREATE TABLE devices (";
    for (size_t i = 0; i < output_cols.size(); ++i) {
        if (i) create_sql += ", ";
        create_sql += "\"" + output_cols[i] + "\" TEXT";
    }
    create_sql += ")";

    char* err = nullptr;
    if (sqlite3_exec(out_db, create_sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Error creating table: " << err << "\n";
        sqlite3_free(err);
        sqlite3_close(out_db);
        return 1;
    }

    // Create indices for common lookups
    sqlite3_exec(out_db, "CREATE INDEX idx_uuid ON devices(uuid)", nullptr, nullptr, nullptr);
    sqlite3_exec(out_db, "CREATE INDEX idx_tradeName ON devices(tradeName)", nullptr, nullptr, nullptr);
    sqlite3_exec(out_db, "CREATE INDEX idx_migel_nr ON devices(migel_position_nr)", nullptr, nullptr, nullptr);

    // Prepare INSERT statement
    std::string placeholders;
    for (size_t i = 0; i < output_cols.size(); ++i) {
        if (i) placeholders += ",";
        placeholders += "?";
    }
    std::string insert_sql = "INSERT INTO devices VALUES (" + placeholders + ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(out_db, insert_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing insert: " << sqlite3_errmsg(out_db) << "\n";
        sqlite3_close(out_db);
        return 1;
    }

    // Write rows in a transaction
    sqlite3_exec(out_db, "PRAGMA synchronous=OFF; PRAGMA journal_mode=WAL; BEGIN TRANSACTION;",
                 nullptr, nullptr, nullptr);

    for (const auto& row : matched_rows) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        for (size_t i = 0; i < output_cols.size(); ++i) {
            const std::string& val = (i < row.size()) ? row[i] : "";
            if (val.empty())
                sqlite3_bind_null(stmt, static_cast<int>(i + 1));
            else
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), val.c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE)
            std::cerr << "INSERT error: " << sqlite3_errmsg(out_db) << "\n";
    }

    sqlite3_exec(out_db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);
    sqlite3_close(out_db);

    std::cout << "Done! Output: " << output_path << " (" << matched_rows.size() << " rows)\n";
    return 0;
}
