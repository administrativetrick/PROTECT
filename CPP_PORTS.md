# PROTECT C++ command-line ports

Each tool in this suite is a Python Tkinter desktop application. This directory
tree also provides a **C++17 command-line port of each tool's core logic**,
under `<tool>/cpp/`. The ports are headless and scriptable, which suits batch
or server-side investigation (scanning many exported logs or images at once),
and they keep the behaviour of the fixed Python versions.

GUI-only behaviour that cannot be meaningfully represented on the command line
is intentionally left to the Python apps - specifically acronym2Text's global
keyboard hook and system-tray integration. Its C++ port provides the portable
core (the acronym dictionary and text expansion/lookup).

## Building

Whole suite (from the repository root):

```sh
cmake -B build && cmake --build build
# binaries land under build/<tool>/cpp/
```

Any single tool, standalone:

```sh
cd <tool>/cpp && cmake -B build && cmake --build build
```

## Dependencies

| Tool                    | Needs to build                     | Needs at runtime |
|-------------------------|------------------------------------|------------------|
| chatlogparser           | C++17 compiler                     | -                |
| acronym2text            | C++17 compiler                     | -                |
| linkscraper             | C++17, SQLite3                     | `curl` binary    |
| websitecloner           | C++17, SQLite3                     | `curl` binary    |
| imgmetadatascraper      | C++17, SQLite3                     | `curl` binary    |
| imagebarcodesearch      | C++17, SQLite3, libpng, libjpeg, ZBar | -             |

On Debian/Ubuntu:
`sudo apt-get install build-essential cmake libsqlite3-dev libpng-dev libjpeg-dev libzbar-dev curl`

## Tools

### chatlogparser
Scans a chat log for whole-word, case-insensitive keyword matches. Keywords are
treated as literal text (regex-escaped), so metacharacters never crash it.

```sh
chatlogparser --prefs ../preferences.json chat.txt
cat chat.txt | chatlogparser --keywords "meet, irl, asl"
```

### acronym2text
Loads the shared `preferences.json` acronym dictionary and either lists it,
explains one term, or expands every acronym found in a block of text.

```sh
acronym2text --prefs ../preferences.json --list
acronym2text --prefs ../preferences.json --explain ASL
echo "hey asl? pos right now" | acronym2text --prefs ../preferences.json
```

### linkscraper
Fetches a page, extracts every `<a href>`, resolves it to an absolute URL, and
stores (source, link) rows in SQLite.

```sh
linkscraper scrape https://example.com --db links.db
linkscraper list --db links.db
```

### websitecloner
Saves a page's HTML under `WebsiteClones/` (images are intentionally not
downloaded) and records the path in SQLite. Filenames are sanitised and output
is UTF-8.

```sh
websitecloner clone https://example.com --db clone_data.db
websitecloner list --db clone_data.db
```

### imgmetadatascraper
Scrapes a page for `<img>` tags, fetches each image, and records its format and
pixel dimensions (read from the file header) in SQLite. The images themselves
are not kept. Extracting embedded EXIF tags (e.g. GPS) would additionally
require libexif and is not performed by this port.

```sh
imgmetadatascraper scrape https://example.com --notes "case 42" --db image_metadata.db
imgmetadatascraper list --db image_metadata.db
imgmetadatascraper filter example --db image_metadata.db
```

### imagebarcodesearch
Decodes barcodes/QR codes from a JPEG or PNG image using ZBar and stores the
results in SQLite. Only numeric EAN/UPC payloads are zero-padded to 13 digits;
other payloads (e.g. a QR code holding a URL) are stored verbatim.

```sh
imagebarcodesearch scan photo.png --db barcodes.db
imagebarcodesearch list --db barcodes.db
imagebarcodesearch remove 0012345678905 --db barcodes.db
```
