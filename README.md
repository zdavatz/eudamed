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
./download_devices --full-detail   # Listing + detail + Basic UDI-DI (json/ + /tmp/basic_udi_cache/)
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
g++ -std=c++20 -O2 -pthread -I cpp cpp/json2csv.cpp -lsqlite3 -o json2csv
mkdir -p csv
find json -maxdepth 1 -type f -name '*.json' | ./json2csv -o csv/eudamed_$(date +%d.%m.%Y).csv+db -
```

- **eudamed2sqlite.cpp** — imports CSV into SQLite (RFC 4180-compliant parser)
- **json2csv.cpp** — multi-threaded converter from individual JSON device files to CSV and/or SQLite (uses nlohmann `json.hpp`)
- **eudamed_migel.cpp** — multi-threaded matcher: merges two EUDAMED SQLite DBs (case-insensitive dedup by UUID), matches devices against Swiss MiGeL codes using tradeName + Description + CND_Description fields with per-field language detection (EN/DE/FR/IT), language-routed matching, and English→DE/FR/IT term expansion; skips unsupported languages (Latvian, Polish, etc.)
- **migel.hpp** — header-only MiGeL CSV parser, keyword matcher (inverted index, fuzzy/suffix matching, per-language scoring), and language detector (stop-word + UTF-8 character feature based)

```bash
# Convert XLSX to CSV (one file per sheet: DE, FR, IT)
ssconvert --export-type=Gnumeric_stf:stf_csv --export-file-per-sheet xlsx/migel.xlsx xlsx/migel_%n.csv

# Build and run EUDAMED-MiGeL matcher
g++ -std=c++20 -O2 -pthread cpp/eudamed_migel.cpp -lsqlite3 -o eudamed_migel
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

## EUDAMED Device API — Three Data Levels

EUDAMED exposes device data across three separate API levels. All three must be merged to produce a complete GS1 firstbase record:

| Level | Endpoint | Data |
|---|---|---|
| 1. Listing | `GET /devices/udiDiData?page=N` | Flat summary: GTIN, manufacturer SRN, AR SRN, risk class, basicUdi code |
| 2. UDI-DI Detail | `GET /devices/udiDiData/{uuid}` | Rich product data: clinical sizes, substances, market info, warnings, storage, trade name, sterility |
| 3. Basic UDI-DI | `GET /devices/basicUdiData/udiDiData/{uuid}` | Device family data: active, implantable, measuringFunction, multiComponent, reusable, medicinalProduct, humanTissues, animalTissues, humanProduct, administeringMedicine |

The **listing** (level 1) provides manufacturer/AR SRN and risk class, which are missing from the detail endpoint. The **UDI-DI detail** (level 2) provides the rich product-specific data (clinical sizes, substances, market info, etc.). The **Basic UDI-DI** (level 3) provides the MDR mandatory boolean fields (implantable, active, measuring function, multi-component type, tissue, etc.) which live at the device family level and are not returned by the UDI-DI detail endpoint.

The `--full-detail` mode of `download_devices` chains all three levels: listing download, per-UUID detail download to `json/`, and Basic UDI-DI download to `/tmp/basic_udi_cache/`. The [eudamed2firstbase](https://github.com/zdavatz/eudamed2firstbase) converter merges all three sources into GS1 firstbase JSON.

## API

Base URL: `https://ec.europa.eu/tools/eudamed/api/`

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
