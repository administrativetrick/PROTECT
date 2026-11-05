# PROTECT Suite (Tauri + Rust)

A single cross-platform desktop application that unifies all six PROTECT tools
behind one modern UI. The frontend is TypeScript + Vite (a sleek sidebar shell,
dark/light aware); the backend is Rust exposed through Tauri commands.

This is the recommended implementation of the suite. The Python apps and the
C++/Qt ports remain in the repository for reference.

## Tools

- **Chat Logs** - scan pasted logs against a keyword list (literal whole-word,
  case-insensitive matching); saved scans persist to SQLite.
- **Acronyms** - manage the acronym dictionary, expand acronyms in a block of
  text, and run an optional global keyboard hook (rdev + enigo) that expands
  abbreviations as you type.
- **Link Scraper** - fetch a page and store its links (resolved to absolute
  URLs) grouped by source.
- **Website Cloner** - save a page's HTML (images intentionally not fetched)
  and open it later.
- **Image Metadata** - scrape a page's images and record each one's format,
  dimensions, and EXIF metadata (the images themselves are not stored).
- **Barcode Search** - decode barcodes/QR codes from an image file with ZBar's
  Rust port (rxing); numeric EAN/UPC codes are padded to 13 digits.

All data (SQLite databases, saved chats and clones) is stored under the OS
application-data directory, not the working directory.

## Prerequisites

- Rust (stable) and Node.js 18+.
- Tauri's platform webview dependencies. On Debian/Ubuntu:

  ```sh
  sudo apt-get install libwebkit2gtk-4.1-dev libjavascriptcoregtk-4.1-dev \
    libsoup-3.0-dev libgtk-3-dev librsvg2-dev libayatana-appindicator3-dev \
    libxdo-dev build-essential curl
  ```

  macOS and Windows: see https://tauri.app/start/prerequisites/.

## Develop and build

```sh
cd tauri
npm install
npm run tauri dev      # run the app in development
npm run tauri build    # produce a distributable bundle
```

To build only the pieces: `npm run build` compiles the frontend into `dist/`,
and `cargo build` (in `src-tauri/`) compiles the Rust binary.

## Notes

- The acronym global keyboard hook is platform-specific; the reference
  implementation targets X11 (Linux) and needs an active desktop session.
- The scrapers fetch over HTTPS with a 15-second timeout and extract links and
  images with a real HTML parser (`scraper`).
