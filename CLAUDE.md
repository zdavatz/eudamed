# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ETL toolkit for extracting, transforming, and loading data from the European Union Medical Devices Database (EUDAMED). Combines Bash, C++, and Rust to download paginated API data and produce CSV/SQLite/NDJSON outputs.

## Build Commands

### Rust (authorized_representatives/)
```bash
cd authorized_representatives && cargo build --release
```

### C++ (cpp/)
```bash
g++ cpp/eudamed2sqlite.cpp -lsqlite3 -o eudamed2sqlite
g++ -std=c++20 -O2 -pthread cpp/json2csv.cpp -lsqlite3 -o json2csv
g++ -std=c++20 -O2 -pthread cpp/eudamed_migel.cpp -lsqlite3 -o eudamed_migel
```

### Bash scripts
No build needed. Require: `bash`, `curl`, `jq`, `sqlite3`.

## Key Scripts & Tools

| Tool | Language | Purpose |
|------|----------|---------|
| `download_devices` | Bash+jq | Unified device downloader & converter (`--full`, `--sample`, `--pages N`, `--csv-sample`, `--to-csv [N\|all]`) |
| `cpp/eudamed2sqlite.cpp` | C++ | Import CSV into SQLite database (RFC 4180-compliant parser) |
| `cpp/json2csv.cpp` | C++ | Multi-threaded JSON files → CSV+SQLite converter (uses nlohmann json.hpp) |
| `cpp/eudamed_migel.cpp` | C++ | Multi-threaded EUDAMED↔MiGeL matcher: merges two DBs (case-insensitive dedup by UUID), matches on tradeName+Description+CND_Description, English→DE/FR/IT term expansion |
| `cpp/migel.hpp` | C++ | Header-only MiGeL CSV parser & keyword matcher: inverted index, fuzzy/suffix matching, per-language scoring |
| `authorized_representatives/` | Rust | Convert downloaded actor JSON to CSV (`json-to-csv <input.json> [output.csv]`), adds UTF-8 BOM for Excel |
| `download` | Bash | Unified downloader for actor data (`--importer`, `--manufacturer`, `--ar`) — auto-detects page count |

## Architecture

**Data pipeline flow:** EUDAMED API → paginated JSON → NDJSON/JSON → CSV → SQLite

- API downloaders handle pagination (Spring-style `page`/`size`/`sort` params) with browser-like User-Agent headers
- NDJSON is the primary intermediate format for streaming large datasets (~946MB full dataset)
- CSV outputs use UTF-8 BOM for Excel compatibility (Rust component) and RFC 4180 compliance
- Nested JSON fields (e.g., `riskClass.code`, `deviceStatusType.code`) are flattened with jq during CSV conversion

**Data entities:** Devices (UDI/DI), Authorized Representatives, Manufacturers, Importers — cross-referenced via UUIDs and SRN codes.

**API base:** `https://ec.europa.eu/tools/eudamed/api/`
