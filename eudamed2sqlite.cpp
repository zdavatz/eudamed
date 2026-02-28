#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <sqlite3.h>

using namespace std;

// Fully RFC 4180-compliant CSV parser that handles multi-line quoted fields
vector<string> parseCsvRow(istream& str) {
    vector<string> row;
    string field;
    bool inQuotes = false;
    char c;

    while (str.get(c)) {
        if (inQuotes) {
            if (c == '"') {
                char next = str.peek();
                if (next == '"') {
                    str.get(c);  // skip the second quote
                    field += '"';
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                row.push_back(field);
                field.clear();
            } else if (c == '\n' || c == '\r') {
                // End of row (but only if not in quotes)
                if (c == '\r' && str.peek() == '\n') str.get(c); // handle \r\n
                row.push_back(field);
                return row;
            } else {
                field += c;
            }
        }
    }

    // End of stream
    if (!field.empty() || inQuotes) {
        row.push_back(field);
    }
    return row;
}

// Clean field: remove surrounding quotes and unescape ""
string cleanField(string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    size_t pos = 0;
    while ((pos = s.find("\"\"", pos)) != string::npos) {
        s.replace(pos, 2, "\"");
        pos += 1;
    }
    return s;
}

void printUsage(const char* prog) {
    cout << "Usage: " << prog << " <input.csv> [output.db]\n";
    cout << "Example: " << prog << " eudamed.csv devices.db\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    string csvPath = argv[1];
    string dbPath = (argc >= 3) ? argv[2] : "eudamed_devices.db";
    string tableName = "devices";

    ifstream file(csvPath);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file: " << csvPath << endl;
        return 1;
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        cerr << "Error: Can't create database: " << sqlite3_errmsg(db) << endl;
        return 1;
    }

    cout << "Parsing CSV: " << csvPath << endl;
    cout << "Output DB:  " << dbPath << endl;

    // Read header
    vector<string> header = parseCsvRow(file);
    if (header.empty()) {
        cerr << "Error: Empty or invalid header" << endl;
        return 1;
    }
    vector<string> columns;
    for (auto& col : header) {
        columns.push_back(cleanField(col));
    }

    // Create table
    stringstream sql;
    sql << "CREATE TABLE IF NOT EXISTS " << tableName << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << "\"" << columns[i] << "\" TEXT";
    }
    sql << ");";

    char* err = nullptr;
    rc = sqlite3_exec(db, sql.str().c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        cerr << "SQL error (create table): " << err << endl;
        sqlite3_free(err);
        return 1;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    // Prepare insert
    stringstream insertSql;
    insertSql << "INSERT INTO " << tableName << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) insertSql << ", ";
        insertSql << "\"" << columns[i] << "\"";
    }
    insertSql << ") VALUES (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) insertSql << ", ";
        insertSql << "?";
    }
    insertSql << ")";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, insertSql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << endl;
        return 1;
    }

    long long rowCount = 0;
    while (file) {
        vector<string> row = parseCsvRow(file);
        if (row.empty()) break;

        if (row.size() != columns.size()) {
            cerr << "Warning: Row " << (rowCount + 2) << " has " << row.size()
                 << " fields (expected " << columns.size() << "). Skipping." << endl;
            continue;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        for (size_t i = 0; i < row.size(); ++i) {
            string val = cleanField(row[i]);
            if (val.empty()) {
                sqlite3_bind_null(stmt, static_cast<int>(i + 1));
            } else {
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), val.c_str(), -1, SQLITE_TRANSIENT);
            }
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            cerr << "Insert error at row " << (rowCount + 2) << ": " << sqlite3_errmsg(db) << endl;
        }

        rowCount++;
        if (rowCount % 10000 == 0) {
            cout << "Processed " << rowCount << " rows..." << endl;
        }
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    cout << "\nSUCCESS! Imported " << rowCount << " rows into " << dbPath << endl;
    cout << "Table: " << tableName << endl;

    return 0;
}
