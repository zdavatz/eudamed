# EUDAMED Data Toolkit

ETL toolkit for downloading and processing data from the [European Union Medical Devices Database (EUDAMED)](https://ec.europa.eu/tools/eudamed/).

## Prerequisites

- `bash`, `curl`, `jq`, `sqlite3`
- Rust toolchain (for the JSON-to-CSV converter)
- `g++` with `libsqlite3-dev` (for the SQLite importer)
- `ssconvert` from gnumeric (for XLSX to CSV conversion)

## Scripts

### download — Actor Data (Importers, Manufacturers, Authorised Representatives)

```bash
./download --importer          # Download all importers
./download --manufacturer      # Download all manufacturers
./download --ar                # Download all authorised representatives
./download --importer --count  # Show record count without downloading
```

Auto-detects total pages from the API. Data is saved as paginated JSON files and merged into a single `merged.json` per category.

### download_devices — UDI/DI Device Data

```bash
./download_devices --full          # Full dataset as resumable NDJSON
./download_devices --sample        # First 100 records as pretty JSON
./download_devices --pages 50      # First 50 pages (~1000 records) as NDJSON
./download_devices --csv-sample    # First 100 records directly to CSV
./download_devices --to-csv all    # Convert local NDJSON → CSV with EUDAMED URLs
./download_devices --to-csv 5000   # Convert first 5000 records
```

### cpp/ — C++ Tools

```bash
# CSV to SQLite importer
g++ cpp/eudamed2sqlite.cpp -lsqlite3 -o eudamed2sqlite
./eudamed2sqlite

# Multi-threaded JSON files to CSV+SQLite converter
g++ -std=c++20 -O2 -pthread cpp/json2csv.cpp -lsqlite3 -o json2csv
find json/ -name '*.json' | ./json2csv -o output.csv+db -
```

- **eudamed2sqlite.cpp** — imports CSV into SQLite (RFC 4180-compliant parser)
- **json2csv.cpp** — multi-threaded converter from individual JSON device files to CSV and/or SQLite (uses nlohmann `json.hpp`)
- **eudamed_migel.cpp** — matches EUDAMED devices against Swiss MiGeL codes, outputs only matched products
- **migel.hpp** — header-only MiGeL CSV parser and keyword matcher (no external dependencies)

```bash
# Convert XLSX to CSV (one file per sheet: DE, FR, IT)
ssconvert --export-type=Gnumeric_stf:stf_csv --export-file-per-sheet xlsx/migel.xlsx xlsx/migel_%n.csv

# Build and run EUDAMED-MiGeL matcher
g++ -std=c++20 -O2 cpp/eudamed_migel.cpp -lsqlite3 -o eudamed_migel
./eudamed_migel --db1 db/eudamed_devices.db --db2 db/eudamed_full_with_urls.db \
    --migel-de xlsx/migel_0.csv --migel-fr xlsx/migel_1.csv --migel-it xlsx/migel_2.csv
# Outputs: db/eudamed_migel_DD.MM.YYYY.db
```

### authorized_representatives/ — JSON to CSV (Rust)

```bash
cd authorized_representatives && cargo build --release
./target/release/json-to-csv <input.json> [output.csv]
```

Converts downloaded actor JSON to CSV with UTF-8 BOM for Excel compatibility.

## Data Pipeline

```
EUDAMED API → paginated JSON → NDJSON/JSON → CSV → SQLite
```

## Data Entities

- **Devices** (UDI/DI) — medical device identifiers and metadata
- **Authorised Representatives** — EU-based representatives for non-EU manufacturers
- **Manufacturers** — device manufacturers
- **Importers** — entities importing devices into the EU

Cross-referenced via UUIDs and SRN (Single Registration Number) codes.

## API

Base URL: `https://ec.europa.eu/tools/eudamed/api/`

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
