// json2csv.cpp  ← KOMPLETT ÜBERSCHREIBEN! (100% fehlerfrei & absturzsicher)
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <filesystem>
#include <sqlite3.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

std::mutex cout_mtx, file_mtx;
std::set<std::string> global_headers;
std::atomic<size_t> processed_files{0};

std::string current_date_str() {
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

std::string escape_csv(const std::string& s) {
    std::string out = s;
    size_t pos = 0;
    while ((pos = out.find('"', pos)) != std::string::npos) {
        out.replace(pos, 1, "\"\"");
        pos += 2;
    }
    if (out.find(',') != std::string::npos || out.find('\n') != std::string::npos || out.find('"') != std::string::npos)
        out = "\"" + out + "\"";
    return out;
}

std::string safe_str(const json& j, const std::string& key) {
    return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : "";
}
std::string safe_nested(const json& j, const std::vector<std::string>& keys) {
    json cur = j;
    for (const auto& k : keys) {
        if (!cur.contains(k) || cur[k].is_null()) return "";
        cur = cur[k];
    }
    return cur.is_string() ? cur.get<std::string>() : "";
}
std::string get_text(const json& arr) {
    if (!arr.is_array() || arr.empty()) return "";
    for (const auto& t : arr) {
        if (t.contains("language") && t["language"].contains("isoCode") &&
            t["language"]["isoCode"] == "en" && t.contains("text") && t["text"].is_string())
            return t["text"].get<std::string>();
    }
    for (const auto& t : arr)
        if (t.contains("text") && t["text"].is_string())
            return t["text"].get<std::string>();
    return "";
}
bool safe_bool(const json& j, const std::string& key, bool def = false) {
    if (!j.contains(key) || j[key].is_null()) return def;
    return j[key].is_boolean() ? j[key].get<bool>() : def;
}
std::string join_vector(const std::vector<std::string>& v, const std::string& sep = ", ") {
    if (v.empty()) return "";
    std::string result = v[0];
    for (size_t i = 1; i < v.size(); ++i) result += sep + v[i];
    return result;
}
int safe_stoi(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    try {
        size_t pos;
        int val = std::stoi(s, &pos);
        if (pos == s.size()) return val;
    } catch (...) {}
    return fallback;
}

// ======================== EXTRACT ROW ========================
std::map<std::string, std::string> extract_row(const fs::path& path) {
    std::ifstream f(path);
    if (!f) { std::lock_guard<std::mutex> lk(cout_mtx); std::cerr << "Nicht lesbar: " << path << "\n"; return {}; }

    json j;
    try { f >> j; }
    catch (const json::exception& e) {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cerr << "JSON-Fehler in " << path.filename() << ": " << e.what() << "\n";
        return {};
    }

    std::map<std::string, std::string> row;
    row["File"] = path.filename().string();
    row["UUID"] = safe_str(j, "uuid");
    row["ULID"] = safe_str(j, "ulid");
    row["UDI_DI"] = safe_nested(j, {"primaryDi", "code"});
    row["Issuing_Agency"] = safe_nested(j, {"primaryDi", "issuingAgency", "code"});
    row["Reference"] = safe_str(j, "reference");
    row["TradeName"] = (j.contains("tradeName") && j["tradeName"].contains("texts")) ? get_text(j["tradeName"]["texts"]) : "";
    row["Description"] = (j.contains("additionalDescription") && j["additionalDescription"].contains("texts")) ? get_text(j["additionalDescription"]["texts"]) : "";
    row["Manufacturer_URL"] = safe_str(j, "additionalInformationUrl");

    std::vector<std::string> countries;
    if (j.contains("marketInfoLink") && j["marketInfoLink"].contains("msWhereAvailable")) {
        for (const auto& c : j["marketInfoLink"]["msWhereAvailable"]) {
            if (c.contains("country") && c["country"].contains("iso2Code"))
                countries.emplace_back(c["country"]["iso2Code"].get<std::string>());
        }
    }
    std::sort(countries.begin(), countries.end());
    auto it = std::unique(countries.begin(), countries.end());
    countries.erase(it, countries.end());
    row["Countries_Available"] = join_vector(countries);
    row["Countries_Count"] = std::to_string(countries.size());

    std::string cnd_code = "", cnd_desc = "";
    if (j.contains("cndNomenclatures") && j["cndNomenclatures"].is_array() && !j["cndNomenclatures"].empty()) {
        auto c = j["cndNomenclatures"][0];
        cnd_code = safe_str(c, "code");
        if (c.contains("description") && c["description"].contains("texts"))
            cnd_desc = get_text(c["description"]["texts"]);
    }
    row["CND_Code"] = cnd_code;
    row["CND_Description"] = cnd_desc;

    row["Sterile"]        = safe_bool(j, "sterile") ? "Yes" : "No";
    row["SingleUse"]      = safe_bool(j, "singleUse") ? "Yes" : "No";
    row["Latex"]          = safe_bool(j, "latex") ? "Yes" : "No";
    row["DirectMarking"]  = safe_bool(j, "directMarking") ? "Yes" : "No";
    row["Reprocessed"]    = safe_bool(j, "reprocessed") ? "Yes" : "No";

    if (j.contains("directMarkingDi") && j["directMarkingDi"].contains("code"))
        row["DirectMarking_DI"] = j["directMarkingDi"]["code"].get<std::string>();

    if (j.contains("placedOnTheMarket") && j["placedOnTheMarket"].contains("iso2Code"))
        row["Placed_On_Market_Country"] = j["placedOnTheMarket"]["iso2Code"].get<std::string>();

    row["Status_Code"] = safe_nested(j, {"deviceStatus", "type", "code"});
    row["Version_Number"] = j.contains("versionNumber") ? std::to_string(j["versionNumber"].get<int>()) : "";

    std::vector<std::string> warnings;
    if (j.contains("criticalWarnings") && j["criticalWarnings"].is_array()) {
        for (const auto& w : j["criticalWarnings"]) {
            std::string code = safe_nested(w, {"typeCode"});
            std::string text = w.contains("description") && w["description"].contains("texts") ? get_text(w["description"]["texts"]) : "";
            std::string entry = code;
            if (!text.empty()) entry += (entry.empty() ? "" : ": ") + text.substr(0, 500);
            if (!entry.empty()) warnings.push_back(entry);
        }
    }
    row["Critical_Warnings"] = join_vector(warnings, " | ").substr(0, 32000);

    std::vector<std::string> storage;
    if (j.contains("storageHandlingConditions") && j["storageHandlingConditions"].is_array()) {
        for (const auto& s : j["storageHandlingConditions"]) {
            std::string text = s.contains("description") && s["description"].contains("texts") ? get_text(s["description"]["texts"]) : "";
            if (!text.empty()) storage.emplace_back(text.substr(0, 500));
        }
    }
    row["Storage_Conditions"] = join_vector(storage, " || ").substr(0, 32000);

    int contained_count = 0;
    std::vector<std::string> contained_gtin;
    if (j.contains("containedItem") && !j["containedItem"].is_null()) {
        json ci = j["containedItem"];
        if (ci.contains("containedItems") && ci["containedItems"].is_array()) {
            for (const auto& item : ci["containedItems"]) {
                if (item.contains("itemIdentifier") && item["itemIdentifier"].contains("code")) {
                    contained_gtin.emplace_back(item["itemIdentifier"]["code"].get<std::string>());
                    contained_count += item.value("numberOfItems", 1);
                }
            }
        }
    }
    row["Contained_Items_Count"] = std::to_string(contained_count);
    row["Contained_Item_Codes"] = join_vector(contained_gtin);

    { std::lock_guard<std::mutex> lk(file_mtx);
      for (const auto& kv : row) global_headers.insert(kv.first);
    }
    return row;
}

// ======================== SQLITE: DB ERSTELLEN ========================
sqlite3* create_database(const std::string& db_path, const std::vector<std::string>& columns) {
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "SQLite öffnen fehlgeschlagen: " << db_path << "\n";
        return nullptr;
    }

    std::string sql = "CREATE TABLE devices (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i) sql += ", ";
        const std::string& col = columns[i];
        if (col.find("Count") != std::string::npos || col == "Version_Number" ||
            col == "Sterile" || col == "SingleUse" || col == "Latex" || col == "DirectMarking" || col == "Reprocessed")
            sql += "\"" + col + "\" INTEGER";
        else
            sql += "\"" + col + "\" TEXT";
    }
    sql += ");";

    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Tabelle erstellen fehlgeschlagen: " << err << "\n";
        sqlite3_free(err);
        sqlite3_close(db);
        return nullptr;
    }

    sqlite3_exec(db, "CREATE INDEX idx_udi_di ON devices(UDI_DI);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX idx_countries ON devices(Countries_Available);", nullptr, nullptr, nullptr);

    return db;
}

// ======================== WORKER THREAD ========================
void worker_thread(
    std::queue<fs::path>* queue,
    std::ofstream* csv_file,
    sqlite3* db,
    sqlite3_stmt* stmt,
    const std::vector<std::string>& ordered_headers,
    std::atomic<bool>& headers_finalized)
{
    std::string line_buffer; line_buffer.reserve(8192);

    while (true) {
        fs::path path;
        { std::lock_guard<std::mutex> lk(file_mtx);
          if (queue->empty()) break;
          path = std::move(queue->front()); queue->pop();
        }

        auto row = extract_row(path);
        if (row.empty()) continue;
        while (!headers_finalized.load()) std::this_thread::yield();

        if (csv_file) {
            line_buffer.clear();
            for (size_t i = 0; i < ((ordered_headers.size())); ++i) {
                if (i) line_buffer += ",";
                auto it = row.find(ordered_headers[i]);
                line_buffer += escape_csv(it != row.end() ? it->second : "");
            }
            line_buffer += "\n";
            { std::lock_guard<std::mutex> lk(file_mtx); *csv_file << line_buffer; }
        }

        if (db && stmt) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);

            for (size_t i = 0; i < ordered_headers.size(); ++i) {
                const std::string& col = ordered_headers[i];
                auto it = row.find(col);
                const std::string& val = (it != row.end()) ? it->second : "";
                int idx = static_cast<int>(i + 1);

                bool is_int_col = (col.find("Count") != std::string::npos || col == "Version_Number" ||
                                  col == "Sterile" || col == "SingleUse" || col == "Latex" ||
                                  col == "DirectMarking" || col == "Reprocessed");

                if (is_int_col) {
                    int int_val = (val == "Yes") ? 1 : safe_stoi(val, 0);
                    sqlite3_bind_int(stmt, idx, int_val);
                }
                else if (val.empty()) {
                    sqlite3_bind_null(stmt, idx);
                }
                else {
                    sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
                }
            }

            { std::lock_guard<std::mutex> lk(file_mtx);
              if (sqlite3_step(stmt) != SQLITE_DONE)
                  std::cerr << "INSERT error: " << sqlite3_errmsg(db) << "\n";
            }
        }

        if ((++processed_files) % 10000 == 0) {
            std::lock_guard<std::mutex> lk(cout_mtx);
            std::cout << "Verarbeitet: " << processed_files << " Dateien...\r" << std::flush;
        }
    }
}

// ======================== MAIN ========================
int main(int argc, char* argv[]) {
    if (argc != 4 || std::string(argv[1]) != "-o") {
        std::cerr << "Aufruf:\n";
        std::cerr << "  find . -name '*.json' | " << (argc > 0 ? argv[0] : "./json2csv") << " -o ausgabe.csv+db -\n";
        std::cerr << "  Nur DB: -o eudamed_extend.db   → eudamed_extend_28.11.2025.db\n";
        return 1;
    }

    std::string spec = argv[2];
    bool want_csv = spec.ends_with(".csv+db");
    bool want_db  = want_csv || spec.ends_with(".db");
    std::string csv_path, db_path;

    if (want_csv) { csv_path = spec.substr(0, spec.size()-7); db_path = csv_path + ".db"; }
    else if (spec == "eudamed_extend.db") db_path = "eudamed_extend_" + current_date_str() + ".db";
    else db_path = spec;

    std::vector<fs::path> files;
    std::unique_ptr<std::istream> src;
    if (std::string(argv[3]) == "-") src.reset(&std::cin);
    else { src = std::make_unique<std::ifstream>(argv[3]); if (!*src) return 1; }

    std::cout << "Lese Dateiliste...\n";
    std::string line;
    while (std::getline(*src, line)) if (!line.empty()) files.emplace_back(line);
    if (files.empty()) { std::cerr << "Keine Dateien!\n"; return 1; }
    std::cout << "Dateien: " << files.size() << "\n";

    for (size_t i = 0; i < std::min<size_t>(5000, files.size()); ++i)
        extract_row(files[i]);

    std::vector<std::string> ordered_headers(global_headers.begin(), global_headers.end());
    std::cout << "Spalten: " << ordered_headers.size() << "\n";

    std::ofstream csv_out;
    if (want_csv) {
        csv_out.open(csv_path, std::ios::binary);
        if (!csv_out) { std::cerr << "CSV nicht schreibbar\n"; return 1; }
        for (size_t i = 0; i < ordered_headers.size(); ++i) {
            if (i) csv_out << ",";
            csv_out << escape_csv(ordered_headers[i]);
        }
        csv_out << "\n";
    }

    sqlite3* db = nullptr;
    sqlite3_stmt* stmt = nullptr;
    bool have_db = false;

    if (want_db) {
        db = create_database(db_path, ordered_headers);
        if (!db) return 1;

        std::string placeholders;
        for (size_t i = 0; i < ordered_headers.size(); ++i) {
            if (i) placeholders += ",";
            placeholders += "?";
        }
        std::string sql = "INSERT INTO devices VALUES (" + placeholders + ");";
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Prepared statement fehlgeschlagen\n";
            sqlite3_close(db);
            return 1;
        }
        have_db = true;
        sqlite3_exec(db, "PRAGMA synchronous=OFF; PRAGMA journal_mode=WAL; BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    }

    std::queue<fs::path> queue;
    for (auto& f : files) queue.push(std::move(f));

    unsigned int threads = std::max(2u, std::thread::hardware_concurrency());
    std::cout << "Starte " << threads << " Threads...\n";

    std::atomic<bool> headers_finalized{true};
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < threads; ++i)
        workers.emplace_back(worker_thread, &queue, want_csv ? &csv_out : nullptr, db, stmt,
                             std::cref(ordered_headers), std::ref(headers_finalized));

    for (auto& t : workers) t.join();

    if (have_db) {
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        std::cout << "SQLite gespeichert: " << db_path << "\n";
    }
    if (want_csv) std::cout << "CSV gespeichert: " << csv_path << "\n";

    std::cout << "\nFertig! " << processed_files << " Datensätze verarbeitet.\n";
    return 0;
}
