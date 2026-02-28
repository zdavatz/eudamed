# EUDAMED Data Toolkit

ETL toolkit for downloading and processing data from the [European Union Medical Devices Database (EUDAMED)](https://ec.europa.eu/tools/eudamed/).

## Prerequisites

- `bash`, `curl`, `jq`, `sqlite3`
- Rust toolchain (for the JSON-to-CSV converter)
- `g++` with `libsqlite3-dev` (for the SQLite importer)

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

### eudamed2sqlite — CSV to SQLite

```bash
g++ eudamed2sqlite.cpp -lsqlite3 -o eudamed2sqlite
./eudamed2sqlite
```

Imports CSV data into a SQLite database. RFC 4180-compliant CSV parser with multi-line quoted field support.

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
