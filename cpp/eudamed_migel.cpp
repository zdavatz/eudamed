// eudamed_migel.cpp — Match EUDAMED devices against Swiss MiGeL codes
// Build: g++ -std=c++20 -O2 -pthread cpp/eudamed_migel.cpp -lsqlite3 -o eudamed_migel
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
#include <thread>
#include <mutex>
#include <atomic>
#include <sqlite3.h>
#include "migel.hpp"

// ----------------------------- English→DE/FR/IT medical term map ---------------
// EUDAMED tradeNames are often in English. MiGeL keywords are in DE/FR/IT.
// This map translates common English medical device terms to their DE/FR/IT
// equivalents so they can match against MiGeL keywords.

static const std::unordered_map<std::string, std::string>& english_medical_terms() {
    static const std::unordered_map<std::string, std::string> m = {
        // Catheters
        {"catheter", "katheter catheter catetere"},
        {"catheters", "katheter catheter catetere"},
        {"urinary", "blasenkatheter urinaire urinario"},
        {"foley", "verweilkatheter foley"},
        {"aspiration", "absaugkatheter aspiration aspirazione"},
        // Bandages & compression
        {"bandage", "bandage binde bendaggio fasciatura"},
        {"bandages", "bandagen binden bendaggi"},
        {"elastic", "elastische elastique elastico"},
        {"compression", "kompression compression compressione"},
        {"stocking", "kompressionsstruempfe bas calze"},
        {"stockings", "kompressionsstruempfe bas calze"},
        // Orthoses & supports
        {"orthosis", "orthese orthese ortesi"},
        {"orthoses", "orthesen ortheses ortesi"},
        {"orthotic", "orthese orthese ortesi"},
        {"brace", "orthese bandage ortesi"},
        {"splint", "schiene attelle stecca"},
        {"splints", "schienen attelles stecche"},
        {"support", "bandage stuetze support supporto"},
        {"stabilizer", "stabilisierung stabilisateur stabilizzatore"},
        // Wound care
        {"wound", "wunde plaie ferita"},
        {"dressing", "verband pansement medicazione"},
        {"dressings", "verbaende pansements medicazioni"},
        {"gauze", "gaze gaze garza"},
        {"compress", "kompresse compresse compressa"},
        {"compresses", "kompressen compresses compresse"},
        {"plaster", "pflaster platre cerotto"},
        {"adhesive", "klebend adhesif adesivo"},
        {"sterile", "steril sterile sterile"},
        // Syringes & needles
        {"syringe", "spritze seringue siringa"},
        {"syringes", "spritzen seringues siringhe"},
        {"needle", "nadel kanuelle aiguille ago"},
        {"needles", "nadeln kanuelen aiguilles aghi"},
        {"injection", "injektion injection iniezione"},
        {"cannula", "kanuele canule cannula"},
        {"infusion", "infusion perfusion infusione"},
        // Respiratory
        {"ventilation", "beatmung ventilation ventilazione"},
        {"breathing", "beatmung respiration respirazione"},
        {"oxygen", "sauerstoff oxygene ossigeno"},
        {"respiratory", "atemwege respiratoire respiratorio"},
        {"inhaler", "inhalationsgeraet inhalateur inalatore"},
        {"inhalation", "inhalation inhalation inalazione"},
        {"nebulizer", "vernebler nebuliseur nebulizzatore"},
        {"tracheostomy", "tracheostomie tracheotomie tracheostomia"},
        {"tracheotomy", "tracheostomie tracheotomie tracheostomia"},
        // Incontinence
        {"incontinence", "inkontinenz incontinence incontinenza"},
        {"absorbent", "aufsaugend absorbant assorbente"},
        {"diaper", "windel couche pannolino"},
        {"urine", "urin urine urina"},
        // Mobility & wheelchair
        {"wheelchair", "rollstuhl fauteuil sedia"},
        {"crutch", "kruecke bequille stampella"},
        {"crutches", "kruecken bequilles stampelle"},
        {"walker", "gehgestell deambulateur deambulatore"},
        {"rollator", "rollator rollator rollator"},
        // Prosthetics
        {"prosthesis", "prothese prothese protesi"},
        {"prosthetic", "prothese prothese protesi"},
        // Insulin & diabetes
        {"insulin", "insulin insuline insulina"},
        {"lancet", "lanzette lancette lancetta"},
        {"lancets", "lanzetten lancettes lancette"},
        {"glucometer", "blutzuckermessgeraet glucometre glucometro"},
        {"glucose", "blutzucker glucose glucosio"},
        {"diabetes", "diabetes diabete diabete"},
        {"test strip", "teststreifen bandelette striscia"},
        // Hearing
        {"hearing", "hoergeraet appareil udito"},
        // Thermometer
        {"thermometer", "thermometer thermometre termometro"},
        // Stoma
        {"stoma", "stoma stomie stomia"},
        {"colostomy", "kolostomie colostomie colostomia"},
        {"ostomy", "stomie stomie stomia"},
        // Blood pressure
        {"blood pressure", "blutdruckmessgeraet tensiometre misuratore"},
        // Suction
        {"suction", "absaugung aspiration aspirazione"},
        // Bed & mattress
        {"mattress", "matratze matelas materasso"},
        {"bed", "bett lit letto"},
        // Gloves
        {"glove", "handschuh gant guanto"},
        {"gloves", "handschuhe gants guanti"},
        // Eye
        {"contact lens", "kontaktlinse lentille lente"},
        {"eye patch", "augenkompresse compresse oculaire compressa oculare"},
    };
    return m;
}

/// Expand English medical terms in a tradeName to include DE/FR/IT equivalents.
static std::string expand_english_terms(const std::string& text) {
    std::string lower = migel::to_lower(text);
    const auto& terms = english_medical_terms();
    std::string expanded = text;

    for (const auto& [en, translations] : terms) {
        if (lower.find(en) != std::string::npos) {
            expanded += " " + translations;
        }
    }
    return expanded;
}

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
    int threads = 0; // 0 = auto-detect
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
        else if (arg == "--threads" && i + 1 < argc) args.threads = std::stoi(argv[++i]);
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0]
                      << " --db1 <db> --db2 <db> --migel-de <csv> --migel-fr <csv> --migel-it <csv> [--threads N]\n"
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

using Row = std::vector<std::string>;

static size_t read_db_rows(
    const std::string& db_path,
    const std::vector<std::string>& unified_cols,
    std::unordered_map<std::string, Row>& rows,
    const std::vector<size_t>& uuid_col_indices)
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "Error opening " << db_path << ": " << sqlite3_errmsg(db) << "\n";
        return 0;
    }

    auto db_cols = read_columns(db, "devices");

    // Case-insensitive mapping from DB columns to unified columns
    std::unordered_map<std::string, size_t> unified_map;
    for (size_t i = 0; i < unified_cols.size(); ++i)
        unified_map[migel::to_lower(unified_cols[i])] = i;

    std::vector<int> col_mapping(db_cols.size(), -1);
    for (size_t i = 0; i < db_cols.size(); ++i) {
        auto it = unified_map.find(migel::to_lower(db_cols[i]));
        if (it != unified_map.end())
            col_mapping[i] = static_cast<int>(it->second);
    }

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

        std::string uuid;
        for (size_t idx : uuid_col_indices) {
            if (idx < row.size() && !row[idx].empty()) {
                uuid = row[idx];
                break;
            }
        }
        if (uuid.empty()) uuid = "__no_uuid_" + std::to_string(count);

        auto it = rows.find(uuid);
        if (it == rows.end()) {
            rows[uuid] = std::move(row);
        } else {
            if (count_non_empty(row) > count_non_empty(it->second))
                it->second = std::move(row);
        }
        ++count;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

// ----------------------------- Parallel matching result -----------------------

struct MatchResult {
    Row row;
    std::string migel_nr;
    std::string migel_bez;
    std::string migel_lim;
};

// ----------------------------- Main ------------------------------------------

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    // Step 1: Load MiGeL items from CSV files
    std::cout << "Loading MiGeL items from CSVs ...\n";
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

    // Case-insensitive column unification (e.g., UUID/uuid, TradeName/tradeName)
    std::vector<std::string> unified_cols = cols1;
    std::unordered_set<std::string> col_set_lower;
    for (const auto& c : cols1) col_set_lower.insert(migel::to_lower(c));
    for (const auto& c : cols2) {
        if (col_set_lower.find(migel::to_lower(c)) == col_set_lower.end()) {
            unified_cols.push_back(c);
            col_set_lower.insert(migel::to_lower(c));
        }
    }

    std::cout << "   Unified columns: " << unified_cols.size()
              << " (db1: " << cols1.size() << ", db2: " << cols2.size() << ")\n";

    // Find column indices (case-insensitive)
    size_t uuid_idx = SIZE_MAX;
    size_t tradeName_idx = SIZE_MAX;
    size_t description_idx = SIZE_MAX;
    size_t cnd_description_idx = SIZE_MAX;
    size_t mfr_idx = SIZE_MAX;
    for (size_t i = 0; i < unified_cols.size(); ++i) {
        std::string lower = migel::to_lower(unified_cols[i]);
        if (lower == "uuid") uuid_idx = i;
        else if (lower == "tradename") tradeName_idx = i;
        else if (lower == "description") description_idx = i;
        else if (lower == "cnd_description") cnd_description_idx = i;
        else if (lower == "manufacturername") mfr_idx = i;
    }

    // Step 3: Read and merge rows from both DBs
    std::vector<size_t> uuid_indices;
    if (uuid_idx != SIZE_MAX) uuid_indices.push_back(uuid_idx);

    std::unordered_map<std::string, Row> all_rows;
    all_rows.reserve(1000000);

    std::cout << "Reading " << args.db1 << " ...\n";
    size_t count1 = read_db_rows(args.db1, unified_cols, all_rows, uuid_indices);
    std::cout << "   " << count1 << " rows read, " << all_rows.size() << " unique.\n";

    std::cout << "Reading " << args.db2 << " ...\n";
    size_t count2 = read_db_rows(args.db2, unified_cols, all_rows, uuid_indices);
    std::cout << "   " << count2 << " rows read, " << all_rows.size() << " unique after merge.\n";

    // Step 4: Flatten to vector for parallel processing
    std::vector<std::pair<std::string, Row>> device_vec;
    device_vec.reserve(all_rows.size());
    for (auto& [uuid, row] : all_rows)
        device_vec.emplace_back(std::move(uuid), std::move(row));
    all_rows.clear(); // free memory

    // Step 5: Parallel matching
    unsigned int num_threads = args.threads > 0
        ? static_cast<unsigned int>(args.threads)
        : std::max(2u, std::thread::hardware_concurrency());
    std::cout << "Matching " << device_vec.size() << " devices against MiGeL using "
              << num_threads << " threads ...\n";

    std::vector<std::vector<MatchResult>> thread_results(num_threads);
    std::atomic<size_t> processed{0};
    std::atomic<size_t> skipped_empty{0};

    auto worker = [&](unsigned int tid, size_t start, size_t end) {
        auto& results = thread_results[tid];
        for (size_t i = start; i < end; ++i) {
            auto& [uuid, row] = device_vec[i];

            std::string trade_name = (tradeName_idx < row.size()) ? row[tradeName_idx] : "";

            std::string description = (description_idx < row.size()) ? row[description_idx] : "";
            std::string cnd_desc = (cnd_description_idx < row.size()) ? row[cnd_description_idx] : "";
            std::string mfr_name = (mfr_idx < row.size()) ? row[mfr_idx] : "";

            if (trade_name.empty() && description.empty() && cnd_desc.empty()) {
                skipped_empty.fetch_add(1, std::memory_order_relaxed);
                size_t p = processed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (p % 200000 == 0)
                    std::cout << "   Processed: " << p << " / " << device_vec.size() << "\n" << std::flush;
                continue;
            }

            // Combine all available text fields for richer matching
            std::string combined = trade_name;
            if (!description.empty()) combined += " " + description;
            if (!cnd_desc.empty()) combined += " " + cnd_desc;

            // Expand English medical terms to DE/FR/IT equivalents for better matching
            std::string expanded = expand_english_terms(combined);

            const migel::MigelItem* match = migel::find_best_migel_match(
                expanded, expanded, expanded, mfr_name,
                migel_items, keyword_index);

            if (match) {
                results.push_back({row, match->position_nr, match->bezeichnung, match->limitation});
            }

            size_t p = processed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (p % 200000 == 0)
                std::cout << "   Processed: " << p << " / " << device_vec.size() << "\n" << std::flush;
        }
    };

    // Launch threads with equal work distribution
    std::vector<std::thread> threads;
    size_t chunk = device_vec.size() / num_threads;
    size_t remainder = device_vec.size() % num_threads;
    size_t offset = 0;

    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start = offset;
        size_t end = offset + chunk + (t < remainder ? 1 : 0);
        offset = end;
        threads.emplace_back(worker, t, start, end);
    }

    for (auto& t : threads) t.join();

    // Merge results
    std::vector<MatchResult> all_matches;
    size_t total_matched = 0;
    for (auto& tr : thread_results) total_matched += tr.size();
    all_matches.reserve(total_matched);
    for (auto& tr : thread_results) {
        all_matches.insert(all_matches.end(),
                           std::make_move_iterator(tr.begin()),
                           std::make_move_iterator(tr.end()));
    }

    std::cout << "\nMatching complete:\n"
              << "   Total devices: " << device_vec.size() << "\n"
              << "   Skipped (no text fields): " << skipped_empty.load() << "\n"
              << "   Matched to MiGeL: " << all_matches.size() << "\n";

    // Step 6: Write output database
    std::vector<std::string> output_cols = unified_cols;
    output_cols.push_back("migel_position_nr");
    output_cols.push_back("migel_bezeichnung");
    output_cols.push_back("migel_limitation");

    std::string output_path = "db/eudamed_migel_" + date_stamp() + ".db";
    std::cout << "Writing output to " << output_path << " ...\n";

    // Remove existing file if present (date collision)
    std::remove(output_path.c_str());

    sqlite3* out_db = nullptr;
    if (sqlite3_open(output_path.c_str(), &out_db) != SQLITE_OK) {
        std::cerr << "Error creating output DB: " << sqlite3_errmsg(out_db) << "\n";
        return 1;
    }

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

    sqlite3_exec(out_db, "CREATE INDEX idx_uuid ON devices(uuid)", nullptr, nullptr, nullptr);
    sqlite3_exec(out_db, "CREATE INDEX idx_tradeName ON devices(tradeName)", nullptr, nullptr, nullptr);
    sqlite3_exec(out_db, "CREATE INDEX idx_migel_nr ON devices(migel_position_nr)", nullptr, nullptr, nullptr);

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

    sqlite3_exec(out_db, "PRAGMA synchronous=OFF; PRAGMA journal_mode=WAL; BEGIN TRANSACTION;",
                 nullptr, nullptr, nullptr);

    for (const auto& mr : all_matches) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        for (size_t i = 0; i < unified_cols.size(); ++i) {
            const std::string& val = (i < mr.row.size()) ? mr.row[i] : "";
            if (val.empty())
                sqlite3_bind_null(stmt, static_cast<int>(i + 1));
            else
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), val.c_str(), -1, SQLITE_TRANSIENT);
        }
        // MiGeL columns
        sqlite3_bind_text(stmt, static_cast<int>(unified_cols.size() + 1),
                          mr.migel_nr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, static_cast<int>(unified_cols.size() + 2),
                          mr.migel_bez.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, static_cast<int>(unified_cols.size() + 3),
                          mr.migel_lim.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE)
            std::cerr << "INSERT error: " << sqlite3_errmsg(out_db) << "\n";
    }

    sqlite3_exec(out_db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);
    sqlite3_close(out_db);

    std::cout << "Done! Output: " << output_path << " (" << all_matches.size() << " rows)\n";
    return 0;
}
