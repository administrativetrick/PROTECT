// websitecloner - C++ port of websiteCloner.py.
//
// Fetches the HTML of a URL and saves it under WebsiteClones/, recording
// (url, path) in a SQLite database. As in the original, only the page HTML is
// saved; images and other resources are intentionally not downloaded.
//
// HTTP uses the system `curl` binary; storage uses the SQLite C API. Output is
// written as UTF-8 and the filename is sanitised so URLs containing
// characters that are illegal on some filesystems still produce a valid path.
//
// Usage:
//   websitecloner clone <url> [--db clone_data.db] [--dir WebsiteClones]
//   websitecloner list [--db clone_data.db]
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -o websitecloner websitecloner.cpp -lsqlite3

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace fs = std::filesystem;

namespace {

std::string shell_single_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out.push_back(c); }
    out.push_back('\'');
    return out;
}

bool http_get(const std::string& url, std::string& body) {
    std::string cmd = "curl -sL --max-time 15 -A 'PROTECT-websiteCloner' -- " +
                      shell_single_quote(url);
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    body.clear();
    std::array<char, 8192> buf;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0)
        body.append(buf.data(), n);
    int rc = pclose(pipe);
    return rc == 0 && !body.empty();
}

std::string sanitize_filename(const std::string& url) {
    std::string stripped = std::regex_replace(url, std::regex(R"RX(^https?://)RX"), "");
    std::string safe;
    for (char c : stripped) {
        if (std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')
            safe.push_back(c);
        else
            safe.push_back('_');
    }
    if (safe.size() > 150) safe = safe.substr(0, 150);
    if (safe.empty()) safe = "clone";
    return safe + ".html";
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error: cannot open database " << path << ": "
                  << sqlite3_errmsg(db) << "\n";
        return nullptr;
    }
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS clones (url TEXT PRIMARY KEY, path TEXT)",
        nullptr, nullptr, nullptr);
    return db;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string db_path = "clone_data.db";
    std::string out_dir = "WebsiteClones";
    for (std::size_t i = 0; i < args.size();) {
        if (args[i] == "--db" && i + 1 < args.size()) {
            db_path = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--dir" && i + 1 < args.size()) {
            out_dir = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);
        } else ++i;
    }

    if (args.empty()) {
        std::cerr << "Usage: websitecloner clone <url> | list [--db clone_data.db]\n";
        return 2;
    }

    sqlite3* db = open_db(db_path);
    if (!db) return 2;
    int ret = 0;

    if (args[0] == "clone" && args.size() >= 2) {
        const std::string url = args[1];
        std::string html;
        if (!http_get(url, html)) {
            std::cerr << "Error: failed to fetch " << url << "\n";
            ret = 1;
        } else {
            std::error_code ec;
            fs::create_directories(out_dir, ec);
            fs::path path = fs::path(out_dir) / sanitize_filename(url);
            std::ofstream f(path, std::ios::binary);
            if (!f) {
                std::cerr << "Error: cannot write " << path << "\n";
                ret = 1;
            } else {
                f << html;
                f.close();
                sqlite3_stmt* st = nullptr;
                sqlite3_prepare_v2(db,
                    "INSERT OR REPLACE INTO clones VALUES (?, ?)", -1, &st, nullptr);
                std::string ps = path.string();
                sqlite3_bind_text(st, 1, url.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, ps.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
                sqlite3_finalize(st);
                std::cout << "Saved " << url << " -> " << ps << "\n";
            }
        }
    } else if (args[0] == "list") {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "SELECT url, path FROM clones", -1, &st, nullptr);
        while (sqlite3_step(st) == SQLITE_ROW) {
            std::cout << sqlite3_column_text(st, 0) << "\t"
                      << sqlite3_column_text(st, 1) << "\n";
        }
        sqlite3_finalize(st);
    } else {
        std::cerr << "Usage: websitecloner clone <url> | list [--db clone_data.db]\n";
        ret = 2;
    }

    sqlite3_close(db);
    return ret;
}
