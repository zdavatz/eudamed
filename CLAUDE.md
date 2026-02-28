# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ETL toolkit for extracting, transforming, and loading data from the European Union Medical Devices Database (EUDAMED). Combines Bash, C++, and Rust to download paginated API data and produce CSV/SQLite/NDJSON outputs.

## Build Commands

### Rust (authorized_representatives/)
```bash
cd authorized_representatives && cargo build --release
```

### C++ (eudamed2sqlite)
```bash
g++ eudamed2sqlite.cpp -lsqlite3 -o eudamed2sqlite
```

### Bash scripts
No build needed. Require: `bash`, `curl`, `jq`, `sqlite3`.

## Key Scripts & Tools

| Tool | Language | Purpose |
|------|----------|---------|
| `download_devices` | Bash+jq | Unified device downloader & converter (`--full`, `--sample`, `--pages N`, `--csv-sample`, `--to-csv [N\|all]`) |
| `eudamed2sqlite` | C++ | Import CSV into SQLite database (RFC 4180-compliant parser) |
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
