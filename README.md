# PROTECT
**P**redatory **R**ecognition and **O**bservation **T**hrough **E**nhanced **C**onversational **T**racking

## Uses
This is meant to be a suite of tools to assist anyone investigating matters of Child Sexual Abuse that will grow over time. This suite focuses on providing tools for the monitoring of chat logs, and interactions that make having predatory conversations with minors more difficult.

### The tools are as follows

#### ChatLogsParser
It can be used by parents, law enforcement, and other concerned parties to parse chat logs for a list of keywords
There is a preference JSON file, you can add a list of JSON keywords into that file and the program will parse for those keywords.
You can also add the comma separated list into the preferences box in the application and it will generate the JSON file for you.
Preferences Persist across reboots, and chat logs are stored in a folder called chats.

#### ImageMetadataScraper
This can be used to scrape a page for images. It does not download the images, it only scrapes the page for metadata and stores that metadata to a database for later investigation.

#### linkScraper
This can be used to scrape a page for links. It stores the parent URL as a parent and the links on the page as a child.

#### websiteCloner
This can be used to pull and save the HTML from websites. images are intentionally not saved as a part of the clone.

#### acronym2Text
This program monitors your keyboard input, if it matches an acronym or shortcut of letters that you have given it, it will type the long form version of that text that you provide. When you copy a word in the list while the program is running it will provide a pop-up window with an explanation of the term you copied. You can use the preferences.json file included, or you can create your own.

Note: As a requirement for a program like acronym2Text to work, it monitors your keystrokes. If you are uncomfortable with that, do not run this program. The other modules above do not include this type of behaviour unless otherwise mentioned.

## Tauri + Rust app (recommended)

The suite has been reimplemented as a single cross-platform desktop
application with a modern UI: a TypeScript/Vite frontend and a Rust backend via
Tauri, under [`tauri/`](tauri/). It unifies all six tools behind one sleek
sidebar. See [tauri/README.md](tauri/README.md) to build and run it.

```sh
cd tauri && npm install && npm run tauri dev
```

## C++ ports

Each tool is also available in C++17 under `<tool>/cpp/`: a **Qt GUI
application** (`<tool>_gui`) that reproduces the original Tkinter app, and a
**command-line program** (`<tool>`) for scripted or headless use. Build the
whole C++ suite with:

```sh
cmake -B build && cmake --build build
```

See [CPP_PORTS.md](CPP_PORTS.md) for per-tool details, build options, and
dependencies.
