// linkscraper - C++ port of linkScraper.py.
//
// The Python program is a Tkinter app that scrapes a page for <a href> links,
// resolves them to absolute URLs, and stores (source, link) rows in a SQLite
// database. This is a command-line port of that core.
//
// HTTP is performed by invoking the system `curl` binary (so no libcurl
// development headers are required); rows are stored with the SQLite C API.
//
// Usage:
//   linkscraper scrape <url> [--db links.db]   Fetch, store and print links.
//   linkscraper list [--db links.db]           Print stored links by source.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -o linkscraper linkscraper.cpp -lsqlite3

#include <array>
#include <cstdio>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace {

std::string shell_single_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// Fetch a URL using the system curl binary. Returns false on failure.
bool http_get(const std::string& url, std::string& body) {
    std::string cmd = "curl -sL --max-time 15 -A 'PROTECT-linkScraper' -- " +
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

struct Url { std::string scheme, host, path; bool ok = false; };

Url parse_url(const std::string& url) {
    Url u;
    std::smatch m;
    std::regex re(R"RX(^([a-zA-Z][a-zA-Z0-9+.\-]*)://([^/]+)(/[^?#]*)?)RX");
    if (std::regex_search(url, m, re)) {
        u.scheme = m[1];
        u.host = m[2];
        u.path = m[3].matched ? m[3].str() : "/";
        u.ok = true;
    }
    return u;
}

// Resolve a possibly-relative link against a base URL. Handles absolute URLs,
// scheme-relative (//host), root-relative (/path) and simple relative links.
std::string urljoin(const std::string& base, const std::string& link) {
    if (std::regex_search(link, std::regex(R"RX(^[a-zA-Z][a-zA-Z0-9+.\-]*://)RX")))
        return link;
    Url b = parse_url(base);
    if (!b.ok) return link;
    if (link.rfind("//", 0) == 0) return b.scheme + ":" + link;
    if (!link.empty() && link[0] == '/') return b.scheme + "://" + b.host + link;
    std::string dir = b.path;
    auto slash = dir.find_last_of('/');
    dir = (slash == std::string::npos) ? "/" : dir.substr(0, slash + 1);
    return b.scheme + "://" + b.host + dir + link;
}

std::vector<std::string> extract_hrefs(const std::string& html) {
    std::vector<std::string> out;
    std::regex re(R"RX(<a\b[^>]*?\bhref\s*=\s*("([^"]*)"|'([^']*)'))RX",
                  std::regex::icase);
    for (auto it = std::sregex_iterator(html.begin(), html.end(), re);
         it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        out.push_back(m[2].matched ? m[2].str() : m[3].str());
    }
    return out;
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error: cannot open database " << path << ": "
                  << sqlite3_errmsg(db) << "\n";
        return nullptr;
    }
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS links (source TEXT, link TEXT)",
                 nullptr, nullptr, nullptr);
    return db;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string db_path = "links.db";
    for (std::size_t i = 0; i < args.size(); ++i)
        if (args[i] == "--db" && i + 1 < args.size()) {
            db_path = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
            break;
        }

    if (args.empty()) {
        std::cerr << "Usage: linkscraper scrape <url> | list [--db links.db]\n";
        return 2;
    }

    sqlite3* db = open_db(db_path);
    if (!db) return 2;
    int ret = 0;

    if (args[0] == "scrape" && args.size() >= 2) {
        const std::string source = args[1];
        std::string html;
        if (!http_get(source, html)) {
            std::cerr << "Error: failed to fetch " << source << "\n";
            ret = 1;
        } else {
            auto hrefs = extract_hrefs(html);
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db, "INSERT INTO links VALUES (?, ?)", -1, &st, nullptr);
            int count = 0;
            for (const auto& h : hrefs) {
                std::string abs = urljoin(source, h);
                sqlite3_bind_text(st, 1, source.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, abs.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
                sqlite3_reset(st);
                std::cout << abs << "\n";
                ++count;
            }
            sqlite3_finalize(st);
            std::cerr << count << " link(s) stored for " << source << "\n";
        }
    } else if (args[0] == "list") {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "SELECT source, link FROM links ORDER BY source",
                           -1, &st, nullptr);
        std::string current;
        while (sqlite3_step(st) == SQLITE_ROW) {
            std::string src = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            std::string lnk = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            if (src != current) { std::cout << src << "\n"; current = src; }
            std::cout << "    " << lnk << "\n";
        }
        sqlite3_finalize(st);
    } else {
        std::cerr << "Usage: linkscraper scrape <url> | list [--db links.db]\n";
        ret = 2;
    }

    sqlite3_close(db);
    return ret;
}
