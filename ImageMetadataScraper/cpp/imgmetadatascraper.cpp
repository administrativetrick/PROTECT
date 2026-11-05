// imgmetadatascraper - C++ port of ImageMetadataScraper/imgmetadatascraper.py.
//
// Scrapes a web page for <img> tags, fetches each referenced image, and
// records image metadata (format and pixel dimensions) in a SQLite database
// for later investigation. As in the original, the images themselves are not
// kept - only their metadata.
//
// Dimensions and format are read directly from the image file headers (PNG,
// JPEG and GIF), so no image library is required. (Extracting embedded EXIF
// tags such as GPS would additionally require libexif; this port records the
// format and dimensions that the Python tool stored as its fallback.)
//
// HTTP uses the system `curl` binary; storage uses the SQLite C API.
//
// Usage:
//   imgmetadatascraper scrape <url> [--notes "..."] [--db image_metadata.db]
//   imgmetadatascraper list [--db image_metadata.db]
//   imgmetadatascraper filter <text> [--db image_metadata.db]
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra imgmetadatascraper.cpp
//             -o imgmetadatascraper -lsqlite3

#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace {

std::string shell_single_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out.push_back(c); }
    out.push_back('\'');
    return out;
}

bool http_get(const std::string& url, std::string& body) {
    std::string cmd = "curl -sL --max-time 15 -A 'PROTECT-ImageMetadataScraper' -- " +
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
    Url u; std::smatch m;
    std::regex re(R"RX(^([a-zA-Z][a-zA-Z0-9+.\-]*)://([^/]+)(/[^?#]*)?)RX");
    if (std::regex_search(url, m, re)) {
        u.scheme = m[1]; u.host = m[2];
        u.path = m[3].matched ? m[3].str() : "/"; u.ok = true;
    }
    return u;
}
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

std::vector<std::string> extract_img_src(const std::string& html) {
    std::vector<std::string> out;
    std::regex re(R"RX(<img\b[^>]*?\bsrc\s*=\s*("([^"]*)"|'([^']*)'))RX",
                  std::regex::icase);
    for (auto it = std::sregex_iterator(html.begin(), html.end(), re);
         it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        std::string s = m[2].matched ? m[2].str() : m[3].str();
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

// Read format + dimensions from the leading bytes of an image.
// Returns e.g. {"PNG", 120, 80}; format empty if unrecognised.
struct ImgInfo { std::string format; int w = 0; int h = 0; };
uint16_t be16(const unsigned char* p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
}
uint16_t le16(const unsigned char* p) { return (uint16_t(p[1]) << 8) | p[0]; }

ImgInfo image_info(const std::string& data) {
    ImgInfo info;
    const auto* b = reinterpret_cast<const unsigned char*>(data.data());
    size_t n = data.size();
    // PNG
    static const unsigned char png_sig[8] =
        {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 24 && std::equal(png_sig, png_sig + 8, b)) {
        info.format = "PNG";
        info.w = static_cast<int>(be32(b + 16));
        info.h = static_cast<int>(be32(b + 20));
        return info;
    }
    // GIF
    if (n >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') {
        info.format = "GIF";
        info.w = le16(b + 6);
        info.h = le16(b + 8);
        return info;
    }
    // JPEG: walk markers looking for a Start-Of-Frame.
    if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < n) {
            if (b[i] != 0xFF) { ++i; continue; }
            unsigned char marker = b[i + 1];
            if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 ||
                (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
            uint16_t seg_len = be16(b + i + 2);
            bool is_sof = (marker >= 0xC0 && marker <= 0xCF) &&
                          marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
            if (is_sof) {
                info.format = "JPEG";
                info.h = be16(b + i + 5);
                info.w = be16(b + i + 7);
                return info;
            }
            i += 2 + seg_len;
        }
    }
    return info;
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error: cannot open database " << path << ": "
                  << sqlite3_errmsg(db) << "\n";
        return nullptr;
    }
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, "
        "metadata_key TEXT, metadata_value TEXT, notes TEXT)",
        nullptr, nullptr, nullptr);
    return db;
}

void insert_meta(sqlite3* db, const std::string& url, const std::string& key,
                 const std::string& value, const std::string& notes) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO images (url, metadata_key, metadata_value, notes) "
        "VALUES (?, ?, ?, ?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void print_rows(sqlite3* db, const std::string& filter) {
    sqlite3_stmt* st = nullptr;
    if (filter.empty()) {
        sqlite3_prepare_v2(db,
            "SELECT url, metadata_key, metadata_value, notes FROM images",
            -1, &st, nullptr);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT url, metadata_key, metadata_value, notes FROM images "
            "WHERE url LIKE ? OR metadata_key LIKE ?", -1, &st, nullptr);
        std::string like = "%" + filter + "%";
        sqlite3_bind_text(st, 1, like.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::cout << sqlite3_column_text(st, 0) << "\t"
                  << sqlite3_column_text(st, 1) << "\t"
                  << sqlite3_column_text(st, 2) << "\t"
                  << sqlite3_column_text(st, 3) << "\n";
    }
    sqlite3_finalize(st);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string db_path = "image_metadata.db";
    std::string notes;
    for (std::size_t i = 0; i < args.size();) {
        if (args[i] == "--db" && i + 1 < args.size()) {
            db_path = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--notes" && i + 1 < args.size()) {
            notes = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);
        } else ++i;
    }
    if (args.empty()) {
        std::cerr << "Usage: imgmetadatascraper scrape <url> [--notes ...] | "
                     "list | filter <text> [--db image_metadata.db]\n";
        return 2;
    }

    sqlite3* db = open_db(db_path);
    if (!db) return 2;
    int ret = 0;

    if (args[0] == "scrape" && args.size() >= 2) {
        const std::string page = args[1];
        std::string html;
        if (!http_get(page, html)) {
            std::cerr << "Error: failed to fetch " << page << "\n";
            ret = 1;
        } else {
            int found = 0;
            for (const auto& src : extract_img_src(html)) {
                std::string img_url = urljoin(page, src);
                std::string bytes;
                if (!http_get(img_url, bytes)) {
                    std::cerr << "  (could not fetch " << img_url << ")\n";
                    continue;
                }
                ImgInfo info = image_info(bytes);
                std::string value = info.format.empty()
                    ? ("unknown, " + std::to_string(bytes.size()) + " bytes")
                    : (info.format + " " + std::to_string(info.w) + "x" +
                       std::to_string(info.h));
                insert_meta(db, img_url, "Dimensions", value, notes);
                std::cout << img_url << "  ->  " << value << "\n";
                ++found;
            }
            std::cerr << found << " image(s) recorded from " << page << "\n";
        }
    } else if (args[0] == "list") {
        print_rows(db, "");
    } else if (args[0] == "filter" && args.size() >= 2) {
        print_rows(db, args[1]);
    } else {
        std::cerr << "Usage: imgmetadatascraper scrape <url> [--notes ...] | "
                     "list | filter <text> [--db image_metadata.db]\n";
        ret = 2;
    }

    sqlite3_close(db);
    return ret;
}
