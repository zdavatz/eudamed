// migel.hpp — MiGeL CSV parser & keyword matcher (converted from migel.rs)
// No external dependencies — reads CSV files produced by ssconvert from gnumeric.
// Usage: ssconvert --export-type=Gnumeric_stf:stf_csv --export-file-per-sheet migel.xlsx migel_%n.csv
//        Then pass migel_0.csv (DE), migel_1.csv (FR), migel_2.csv (IT) to parse_migel_items().
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace migel {

struct MigelItem {
    std::string position_nr;
    std::string bezeichnung;
    std::string limitation;
    /// DE first-line keywords (used for primary scoring)
    std::vector<std::string> keywords_de;
    /// FR first-line keywords (used for primary scoring)
    std::vector<std::string> keywords_fr;
    /// IT first-line keywords (used for primary scoring)
    std::vector<std::string> keywords_it;
    /// DE bonus keywords from additional lines (>= 8 chars, counted toward match count)
    std::vector<std::string> secondary_de;
    /// FR bonus keywords from additional lines
    std::vector<std::string> secondary_fr;
    /// IT bonus keywords from additional lines
    std::vector<std::string> secondary_it;
    /// Union of all keywords (used for candidate index)
    std::vector<std::string> all_keywords;
};

// ------------------------------ Stop words -----------------------------------

inline const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> sw = {
        // German articles, prepositions, conjunctions
        "der", "die", "das", "den", "dem", "des", "ein", "eine", "eines", "einem", "einen", "einer",
        "fuer", "mit", "von", "und", "oder", "bei", "auf", "nach", "ueber", "unter", "aus", "bis",
        "pro", "als", "inkl", "exkl", "max", "min", "per", "zur", "zum", "ins", "vom", "ohne",
        "auch", "sich", "noch", "wenn", "muss", "darf", "resp", "bzw",
        // German generic terms
        "kauf", "miete", "tag", "jahr", "monate", "stueck", "set", "alle", "nur",
        "wird", "ist", "kann", "sind", "werden", "wurde", "hat", "haben",
        "steril", "unsteril", "sterile", "non",
        "diverse", "divers", "diversi",
        "gross", "klein", "lang", "kurz",
        "position", "definierte", "einstellbare",
        // French
        "les", "des", "pour", "avec", "par", "une", "dans", "sur", "qui", "que",
        "achat", "location", "piece", "sans",
        // Italian
        "acquisto", "noleggio", "pezzo", "senza",
        // English
        "the", "for", "and", "with", "per",
        // Generic medical/product terms
        "material", "produkt", "products", "product", "medical", "device",
        "system", "systeme", "systems", "geraet", "geraete", "appareil",
        // Cross-type medical terms
        "compression", "compressione", "kompression",
        "verlaengerung", "extension", "estensione", "prolongation",
        "silikon", "silicone",
        // Generic surgical instrument terms
        "ecarteur", "divaricatore", "retraktor",
    };
    return sw;
}

// ------------------------------ Text utilities --------------------------------

/// Normalize German umlauts so ALL-CAPS text matches proper text.
inline std::string normalize_german(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 32);
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        // UTF-8 two-byte sequences starting with 0xC3
        if (c == 0xC3 && i + 1 < text.size()) {
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
            switch (c2) {
                case 0xA4: out += "ae"; i += 2; continue; // ä
                case 0xB6: out += "oe"; i += 2; continue; // ö
                case 0xBC: out += "ue"; i += 2; continue; // ü
                case 0x9F: out += "ss"; i += 2; continue; // ß
                case 0x84: out += "Ae"; i += 2; continue; // Ä
                case 0x96: out += "Oe"; i += 2; continue; // Ö
                case 0x9C: out += "Ue"; i += 2; continue; // Ü
                case 0xA9: out += "e";  i += 2; continue; // é
                case 0xA8: out += "e";  i += 2; continue; // è
                case 0xAA: out += "e";  i += 2; continue; // ê
                case 0xA0: out += "a";  i += 2; continue; // à
                case 0xA2: out += "a";  i += 2; continue; // â
                case 0xB9: out += "u";  i += 2; continue; // ù
                case 0xBB: out += "u";  i += 2; continue; // û
                case 0xB4: out += "o";  i += 2; continue; // ô
                case 0xAE: out += "i";  i += 2; continue; // î
                case 0xA7: out += "c";  i += 2; continue; // ç
                default: break;
            }
        }
        out += static_cast<char>(c);
        ++i;
    }
    return out;
}

inline std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

/// Get the first line of a string.
inline std::string first_line(const std::string& text) {
    auto pos = text.find('\n');
    if (pos == std::string::npos) return text;
    std::string line = text.substr(0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

/// Get everything after the first line.
inline std::string rest_lines(const std::string& text) {
    auto pos = text.find('\n');
    if (pos == std::string::npos) return "";
    return text.substr(pos + 1);
}

/// Split text on non-alphanumeric characters into words.
inline std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += c;
        } else {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

/// Shared keyword extraction logic.
inline std::vector<std::string> extract_keywords_from(const std::string& text, size_t min_len) {
    std::string normalized = to_lower(normalize_german(text));
    auto words = split_words(normalized);
    const auto& sw = stop_words();

    std::vector<std::string> keywords;
    for (auto& w : words) {
        if (w.size() >= min_len && sw.find(w) == sw.end()) {
            keywords.push_back(std::move(w));
        }
    }
    std::sort(keywords.begin(), keywords.end());
    keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
    return keywords;
}

/// Extract search keywords from first line of text (min 3 chars).
inline std::vector<std::string> extract_keywords(const std::string& text) {
    return extract_keywords_from(first_line(text), 3);
}

/// Extract search keywords from ALL lines of text (min 3 chars).
inline std::vector<std::string> extract_keywords_full(const std::string& text) {
    return extract_keywords_from(text, 3);
}

/// Extract only long (>= 8 char) keywords from additional lines (not first line).
inline std::vector<std::string> extract_secondary_keywords(const std::string& text) {
    std::string rest = rest_lines(text);
    if (rest.empty()) return {};
    return extract_keywords_from(rest, 8);
}

// ------------------------------ RFC 4180 CSV parser ---------------------------

/// Parse a single CSV row from a stream, handling quoted fields with embedded
/// newlines and escaped quotes. Returns false on EOF.
inline bool parse_csv_row(std::istream& in, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool in_quotes = false;
    char c;

    while (in.get(c)) {
        if (in_quotes) {
            if (c == '"') {
                char next = static_cast<char>(in.peek());
                if (next == '"') {
                    in.get(c); // consume escaped quote
                    field += '"';
                } else {
                    in_quotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else if (c == '\n') {
                fields.push_back(field);
                return true;
            } else if (c == '\r') {
                // skip \r, \n will follow
            } else {
                field += c;
            }
        }
    }
    // EOF
    if (!field.empty() || !fields.empty()) {
        fields.push_back(field);
        return true;
    }
    return false;
}

/// Get a field by index, returning empty string if out of bounds.
inline std::string csv_field(const std::vector<std::string>& fields, size_t idx) {
    if (idx >= fields.size()) return "";
    return trim(fields[idx]);
}

// ------------------------------ CSV parsing -----------------------------------

/// Parse a single MiGeL CSV sheet. Returns rows as (pos_nr, bezeichnung, limitation).
/// Column indices: 7=Positions-Nr (H), 9=Bezeichnung (J), 10=Limitation (K)
/// Category rows (1..6, cols B-G) have no Positions-Nr.
inline std::vector<std::tuple<std::string, std::string, std::string>>
parse_csv_sheet(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open " << path << "\n";
        return {};
    }

    std::vector<std::tuple<std::string, std::string, std::string>> rows;
    std::vector<std::string> fields;

    // Skip header row
    parse_csv_row(file, fields);

    while (parse_csv_row(file, fields)) {
        std::string pos_nr = csv_field(fields, 7);
        std::string bezeichnung = csv_field(fields, 9);
        std::string limitation = csv_field(fields, 10);
        rows.emplace_back(std::move(pos_nr), std::move(bezeichnung), std::move(limitation));
    }
    return rows;
}

/// Parse MiGeL items from CSV files (one per language sheet).
/// csv_de: German sheet CSV (required)
/// csv_fr: French sheet CSV (optional, pass "" to skip)
/// csv_it: Italian sheet CSV (optional, pass "" to skip)
inline std::vector<MigelItem> parse_migel_items(
    const std::string& csv_de,
    const std::string& csv_fr = "",
    const std::string& csv_it = "")
{
    // --- Pass 1: Parse German sheet ---
    auto rows_de = parse_csv_sheet(csv_de);
    std::vector<MigelItem> items;

    for (const auto& [pos_nr, bezeichnung, limitation] : rows_de) {
        if (pos_nr.empty()) continue; // category header row

        std::string fl = first_line(bezeichnung);
        auto keywords_de = extract_keywords(fl);
        auto secondary_de = extract_secondary_keywords(bezeichnung);

        auto all_kw = extract_keywords_full(bezeichnung);
        if (!limitation.empty()) {
            auto lim_kw = extract_keywords_full(limitation);
            all_kw.insert(all_kw.end(), lim_kw.begin(), lim_kw.end());
            std::sort(all_kw.begin(), all_kw.end());
            all_kw.erase(std::unique(all_kw.begin(), all_kw.end()), all_kw.end());
        }

        MigelItem item;
        item.position_nr = pos_nr;
        item.bezeichnung = std::move(fl);
        item.limitation = limitation;
        item.keywords_de = std::move(keywords_de);
        item.secondary_de = std::move(secondary_de);
        item.all_keywords = std::move(all_kw);
        items.push_back(std::move(item));
    }

    // --- Pass 2: Parse French and Italian sheets ---
    std::unordered_map<std::string, size_t> pos_map;
    for (size_t i = 0; i < items.size(); ++i)
        pos_map[items[i].position_nr] = i;

    auto process_lang_sheet = [&](const std::string& path, int lang) {
        if (path.empty()) return;
        auto rows = parse_csv_sheet(path);
        for (const auto& [pos_nr, bezeichnung, limitation] : rows) {
            if (pos_nr.empty()) continue;
            auto it = pos_map.find(pos_nr);
            if (it == pos_map.end()) continue;

            size_t item_idx = it->second;
            auto kw = extract_keywords(bezeichnung);
            auto secondary = extract_secondary_keywords(bezeichnung);

            if (lang == 1) {
                items[item_idx].keywords_fr = std::move(kw);
                items[item_idx].secondary_fr = std::move(secondary);
            } else if (lang == 2) {
                items[item_idx].keywords_it = std::move(kw);
                items[item_idx].secondary_it = std::move(secondary);
            }

            auto full_kw = extract_keywords_full(bezeichnung);
            items[item_idx].all_keywords.insert(
                items[item_idx].all_keywords.end(), full_kw.begin(), full_kw.end());
            if (!limitation.empty()) {
                auto lim_kw = extract_keywords_full(limitation);
                items[item_idx].all_keywords.insert(
                    items[item_idx].all_keywords.end(), lim_kw.begin(), lim_kw.end());
            }
        }
    };

    process_lang_sheet(csv_fr, 1);
    process_lang_sheet(csv_it, 2);

    // Deduplicate all_keywords per item
    for (auto& item : items) {
        std::sort(item.all_keywords.begin(), item.all_keywords.end());
        item.all_keywords.erase(
            std::unique(item.all_keywords.begin(), item.all_keywords.end()),
            item.all_keywords.end());
    }

    return items;
}

// ------------------------------ Keyword index --------------------------------

/// Build an inverted index: keyword -> list of MigelItem indices.
inline std::unordered_map<std::string, std::vector<size_t>>
build_keyword_index(const std::vector<MigelItem>& items) {
    std::unordered_map<std::string, std::vector<size_t>> index;
    for (size_t i = 0; i < items.size(); ++i) {
        for (const auto& kw : items[i].all_keywords) {
            index[kw].push_back(i);
        }
    }
    return index;
}

// ------------------------------ Matching -------------------------------------

/// Check if a keyword matches in the text at word level.
/// suffix: also match as suffix of compound word (German only)
/// fuzzy: also try keyword truncated by 1 char (German plural/case)
inline bool word_match(const std::vector<std::string>& text_words,
                       const std::string& keyword, bool suffix, bool fuzzy) {
    for (const auto& word : text_words) {
        if (word == keyword) return true;
        if (suffix && word.size() > keyword.size() + 2 &&
            word.compare(word.size() - keyword.size(), keyword.size(), keyword) == 0)
            return true;
    }
    if (fuzzy && keyword.size() >= 7) {
        std::string trunc = keyword.substr(0, keyword.size() - 1);
        for (const auto& word : text_words) {
            if (word == trunc) return true;
            if (suffix && word.size() > trunc.size() + 2 &&
                word.compare(word.size() - trunc.size(), trunc.size(), trunc) == 0)
                return true;
        }
    }
    return false;
}

/// Check if keyword matches anywhere in text as a substring (for candidate pre-filter).
inline bool fuzzy_contains(const std::string& haystack, const std::string& keyword) {
    if (haystack.find(keyword) != std::string::npos) return true;
    if (keyword.size() >= 7) {
        std::string trunc = keyword.substr(0, keyword.size() - 1);
        if (haystack.find(trunc) != std::string::npos) return true;
    }
    return false;
}

struct KeywordScore {
    double score;
    size_t max_matched_len;
    size_t matched_count;
};

/// Compute keyword overlap score using word-level matching.
inline KeywordScore keyword_score(const std::vector<std::string>& text_words,
                                  const std::vector<std::string>& keywords,
                                  bool suffix, bool fuzzy) {
    double total = 0.0;
    for (const auto& k : keywords) total += static_cast<double>(k.size());
    if (total == 0.0) return {0.0, 0, 0};

    double matched_weight = 0.0;
    size_t max_matched_len = 0;
    size_t matched_count = 0;

    for (const auto& kw : keywords) {
        if (word_match(text_words, kw, suffix, fuzzy)) {
            matched_weight += static_cast<double>(kw.size());
            matched_count++;
            if (kw.size() > max_matched_len) max_matched_len = kw.size();
        }
    }
    return {matched_weight / total, max_matched_len, matched_count};
}

/// Find the best-matching MiGeL item for a product.
/// Each language's keywords are scored ONLY against the same language's product description.
inline const MigelItem* find_best_migel_match(
    const std::string& desc_de,
    const std::string& desc_fr,
    const std::string& desc_it,
    const std::string& brand,
    const std::vector<MigelItem>& migel_items,
    const std::unordered_map<std::string, std::vector<size_t>>& keyword_index)
{
    std::string de_lower = to_lower(normalize_german(desc_de + " " + brand));
    std::string fr_lower = to_lower(normalize_german(desc_fr + " " + brand));
    std::string it_lower = to_lower(normalize_german(desc_it + " " + brand));
    std::string combined = de_lower + " " + fr_lower + " " + it_lower;

    auto de_words = split_words(de_lower);
    auto fr_words = split_words(fr_lower);
    auto it_words = split_words(it_lower);

    // Step 1: Find candidate items via broad keyword index
    std::unordered_set<size_t> candidates;
    for (const auto& [keyword, indices] : keyword_index) {
        if (fuzzy_contains(combined, keyword)) {
            for (size_t idx : indices) candidates.insert(idx);
        }
    }

    // Step 2: Score each candidate using word-level matching
    const MigelItem* best_item = nullptr;
    double best_score = 0.0;
    size_t best_max_len = 0;

    for (size_t idx : candidates) {
        const auto& item = migel_items[idx];

        auto [score_de, max_len_de, count_de] = keyword_score(de_words, item.keywords_de, true, true);
        auto [score_fr, max_len_fr, count_fr] = keyword_score(fr_words, item.keywords_fr, false, false);
        auto [score_it, max_len_it, count_it] = keyword_score(it_words, item.keywords_it, false, false);

        // Secondary bonus matches (only if at least 1 primary matched)
        auto [s_score_de, sec_max_de, sec_count_de] = count_de > 0
            ? keyword_score(de_words, item.secondary_de, true, true)
            : KeywordScore{0.0, 0, 0};
        auto [s_score_fr, sec_max_fr, sec_count_fr] = count_fr > 0
            ? keyword_score(fr_words, item.secondary_fr, false, false)
            : KeywordScore{0.0, 0, 0};
        auto [s_score_it, sec_max_it, sec_count_it] = count_it > 0
            ? keyword_score(it_words, item.secondary_it, false, false)
            : KeywordScore{0.0, 0, 0};

        size_t total_de = count_de + sec_count_de;
        size_t total_fr = count_fr + sec_count_fr;
        size_t total_it = count_it + sec_count_it;
        size_t max_de = std::max(max_len_de, sec_max_de);
        size_t max_fr = std::max(max_len_fr, sec_max_fr);
        size_t max_it = std::max(max_len_it, sec_max_it);

        // Pick best-scoring language
        struct LangScore { double score; size_t max_len; size_t count; };
        LangScore langs[3] = {
            {score_de, max_de, total_de},
            {score_fr, max_fr, total_fr},
            {score_it, max_it, total_it},
        };

        LangScore best_lang = langs[0];
        for (int l = 1; l < 3; ++l) {
            if (langs[l].score > best_lang.score) best_lang = langs[l];
        }

        // Match criteria
        bool passes = false;
        if (best_lang.count >= 2) {
            passes = best_lang.score >= 0.3 && best_lang.max_len >= 6;
        } else {
            passes = best_lang.score >= 0.5 && best_lang.max_len >= 10;
        }

        if (passes) {
            if (best_lang.score > best_score ||
                (best_lang.score == best_score && best_lang.max_len > best_max_len)) {
                best_score = best_lang.score;
                best_max_len = best_lang.max_len;
                best_item = &item;
            }
        }
    }

    return best_item;
}

} // namespace migel
