# PROTECT C++ ports

Every tool in this suite was originally a Python Tkinter desktop application.
Each one has been reimplemented in C++17:

- a **Qt GUI application** (`<tool>/cpp/<tool>_gui.cpp`) that reproduces the
  original desktop app - the same windows, widgets and behaviour, backed by the
  same SQLite databases and file layouts; and
- a **command-line program** (`<tool>/cpp/<tool>.cpp`) exposing the same core
  logic for scripted or headless/batch use.

All ports carry the behaviour of the fixed Python versions (literal keyword
matching, safe filename handling, graceful handling of unreadable images, and
EAN/UPC-only barcode padding).

## Building

Whole suite (from the repository root):

```sh
cmake -B build && cmake --build build
```

Binaries land under `build/<tool>/cpp/`. GUI targets build only when Qt5 (and,
for the barcode tool, ZBar/libpng/libjpeg; for acronym2Text, X11 + Xtst) are
found; otherwise that target is skipped and the CLI still builds.

Any single tool, standalone:

```sh
cd <tool>/cpp && cmake -B build && cmake --build build
```

## Dependencies

| Component            | Build-time                                   | Runtime        |
|----------------------|----------------------------------------------|----------------|
| CLI tools            | C++17; SQLite3 (DB-backed tools)             | `curl` (scrapers) |
| imagebarcodesearch*  | + libpng, libjpeg, ZBar                       | -              |
| GUI apps             | + Qt5 Widgets                                 | a display      |
| acronym2text_gui     | + X11, Xtst (global keyboard hook)            | X11 session    |

On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake \
  qtbase5-dev libsqlite3-dev libpng-dev libjpeg-dev libzbar-dev \
  libx11-dev libxtst-dev curl
```

## GUI applications

Each `<tool>_gui` is the C++/Qt replacement for the Python Tkinter app:

- **chatlogparser_gui** - chat-log text area, Submit/Preferences, results
  table, Add/Remove; writes `chats/` and `keyword_results.db`. Keywords are
  matched as literal whole words, so metacharacters can't crash the scan.
- **acronym2text_gui** - add/remove abbreviations, toggle the listener, system
  tray (Show/Quit), clipboard-triggered explanation popups, and a global
  keyboard hook (X11 RECORD + XTEST) that expands a typed abbreviation into its
  full text. The hook needs an X11 session with the RECORD and XTEST
  extensions (as the Python pynput version needs an active desktop); it
  degrades to a no-op if they're unavailable.
- **linkscraper_gui** - enter a source URL, Add to scrape and store its links
  in a tree grouped by source, Remove to delete a source or a single link.
- **websitecloner_gui** - Clone saves a page's HTML under `WebsiteClones/`
  (images not downloaded); double-click a row to open the saved file.
- **imgmetadatascraper_gui** - URL/Notes/Filter fields and Add/Edit/Remove/
  Filter; records image format and dimensions per `<img>`.
- **imagebarcodesearch_gui** - Browse to a JPEG/PNG, decode barcodes/QR with
  ZBar, list and store them, Remove selected.

## Command-line programs

The CLI equivalents are useful for scripting and batch work.

```sh
chatlogparser --prefs ../preferences.json chat.txt
acronym2text --prefs ../preferences.json --explain ASL
linkscraper scrape https://example.com --db links.db
websitecloner clone https://example.com --db clone_data.db
imgmetadatascraper scrape https://example.com --db image_metadata.db
imagebarcodesearch scan photo.png --db barcodes.db
```

Run any CLI with `-h`/`--help` (or no arguments) for its full usage.

## Notes and limitations

- The scrapers extract links/images with regular expressions rather than a full
  HTML parser, and perform HTTP by invoking the system `curl` binary.
- `imgmetadatascraper` records image format and pixel dimensions (read from the
  file header). Extracting embedded EXIF tags (e.g. GPS) would additionally
  require libexif and is not performed.
- The acronym2Text global keyboard hook is inherently platform-specific; the
  X11 implementation here targets Linux desktops.
