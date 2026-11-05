// imagebarcodesearch - C++ port of imageBarcodeSearch.py.
//
// Decodes barcodes/QR codes from an image file using ZBar and stores the
// results in a SQLite database. JPEG and PNG inputs are supported (decoded to
// grayscale with libjpeg / libpng); no OpenCV dependency is required.
//
// As in the fixed Python version, only numeric EAN/UPC payloads are zero-
// padded to 13 digits; non-numeric payloads (e.g. a QR code holding a URL)
// are stored verbatim.
//
// Usage:
//   imagebarcodesearch scan <image> [--db barcodes.db]
//   imagebarcodesearch list [--db barcodes.db]
//   imagebarcodesearch remove <data> [--db barcodes.db]
//
// Build (CMake recommended):
//   g++ -std=c++17 -O2 -Wall -Wextra imagebarcodesearch.cpp -o imagebarcodesearch
//       -lsqlite3 -ljpeg -lpng -lz `pkg-config --cflags --libs zbar`

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <csetjmp>

#include <sqlite3.h>
#include <zbar.h>
#include <jpeglib.h>
#include <png.h>

namespace {

struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;  // width*height, 8-bit grayscale
    bool ok() const { return width > 0 && height > 0 &&
                             pixels.size() == static_cast<size_t>(width) * height; }
};

bool has_suffix(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    std::string a = s.substr(s.size() - suf.size());
    for (char& c : a) c = static_cast<char>(std::tolower((unsigned char)c));
    return a == suf;
}

struct JpegError { jpeg_error_mgr mgr; jmp_buf setjmp_buffer; };
void jpeg_error_exit(j_common_ptr cinfo) {
    longjmp(reinterpret_cast<JpegError*>(cinfo->err)->setjmp_buffer, 1);
}

GrayImage load_jpeg(const std::string& path) {
    GrayImage img;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return img;
    jpeg_decompress_struct cinfo;
    JpegError jerr;
    cinfo.err = jpeg_std_error(&jerr.mgr);
    jerr.mgr.error_exit = jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer)) { jpeg_destroy_decompress(&cinfo); fclose(f); return GrayImage{}; }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_GRAYSCALE;
    jpeg_start_decompress(&cinfo);
    img.width = static_cast<int>(cinfo.output_width);
    img.height = static_cast<int>(cinfo.output_height);
    img.pixels.resize(static_cast<size_t>(img.width) * img.height);
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* row = img.pixels.data() +
            static_cast<size_t>(cinfo.output_scanline) * img.width;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
}

GrayImage load_png(const std::string& path) {
    GrayImage img;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return img;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(f); return img; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(f); return img; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr); fclose(f); return GrayImage{};
    }
    png_init_io(png, f);
    png_read_info(png, info);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_strip_alpha(png);
    png_set_rgb_to_gray_fixed(png, 1, -1, -1);  // convert any color to gray
    png_set_packing(png);
    png_read_update_info(png, info);
    img.width = static_cast<int>(png_get_image_width(png, info));
    img.height = static_cast<int>(png_get_image_height(png, info));
    img.pixels.resize(static_cast<size_t>(img.width) * img.height);
    std::vector<png_bytep> rows(img.height);
    for (int y = 0; y < img.height; ++y)
        rows[y] = img.pixels.data() + static_cast<size_t>(y) * img.width;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);
    return img;
}

GrayImage load_image(const std::string& path) {
    if (has_suffix(path, ".png")) return load_png(path);
    if (has_suffix(path, ".jpg") || has_suffix(path, ".jpeg")) return load_jpeg(path);
    // Fall back on content: try PNG then JPEG.
    GrayImage g = load_png(path);
    if (g.ok()) return g;
    return load_jpeg(path);
}

std::string normalize_barcode(const std::string& data) {
    bool numeric = !data.empty();
    for (char c : data) if (!std::isdigit((unsigned char)c)) { numeric = false; break; }
    if (numeric && data.size() < 13) return std::string(13 - data.size(), '0') + data;
    return data;
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error: cannot open database " << path << ": "
                  << sqlite3_errmsg(db) << "\n";
        return nullptr;
    }
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS barcodes (data TEXT)",
                 nullptr, nullptr, nullptr);
    return db;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string db_path = "barcodes.db";
    for (std::size_t i = 0; i < args.size();) {
        if (args[i] == "--db" && i + 1 < args.size()) {
            db_path = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2);
        } else ++i;
    }
    if (args.empty()) {
        std::cerr << "Usage: imagebarcodesearch scan <image> | list | "
                     "remove <data> [--db barcodes.db]\n";
        return 2;
    }

    sqlite3* db = open_db(db_path);
    if (!db) return 2;
    int ret = 0;

    if (args[0] == "scan" && args.size() >= 2) {
        GrayImage img = load_image(args[1]);
        if (!img.ok()) {
            std::cerr << "Error: could not read image: " << args[1] << "\n";
            ret = 1;
        } else {
            zbar::ImageScanner scanner;
            scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
            zbar::Image zimg(img.width, img.height, "Y800",
                             img.pixels.data(), img.pixels.size());
            int n = scanner.scan(zimg);
            if (n <= 0) {
                std::cout << "No barcodes found\n";
            } else {
                sqlite3_stmt* st = nullptr;
                sqlite3_prepare_v2(db, "INSERT INTO barcodes VALUES (?)", -1, &st, nullptr);
                for (auto sym = zimg.symbol_begin(); sym != zimg.symbol_end(); ++sym) {
                    std::string data = normalize_barcode(sym->get_data());
                    std::cout << sym->get_type_name() << ": " << data << "\n";
                    sqlite3_bind_text(st, 1, data.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(st);
                    sqlite3_reset(st);
                }
                sqlite3_finalize(st);
            }
        }
    } else if (args[0] == "list") {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "SELECT data FROM barcodes", -1, &st, nullptr);
        while (sqlite3_step(st) == SQLITE_ROW)
            std::cout << sqlite3_column_text(st, 0) << "\n";
        sqlite3_finalize(st);
    } else if (args[0] == "remove" && args.size() >= 2) {
        std::string data = normalize_barcode(args[1]);
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "DELETE FROM barcodes WHERE data = ?", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, data.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
        std::cout << "Removed rows matching: " << data << "\n";
    } else {
        std::cerr << "Usage: imagebarcodesearch scan <image> | list | "
                     "remove <data> [--db barcodes.db]\n";
        ret = 2;
    }

    sqlite3_close(db);
    return ret;
}
