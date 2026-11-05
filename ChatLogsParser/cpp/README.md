# chatlogparser (C++)

A dependency-free C++ port of the keyword-scanning core of `ChatLogsParser.py`.

The Python tool is a Tkinter desktop app; this is the matching engine extracted
into a small command-line program that can be scripted, piped, or run in a
headless/batch context (e.g. scanning many exported logs). It needs only a C++17
compiler — no third-party libraries.

Like the fixed Python version, keywords are matched as **literal** whole words,
case-insensitively. Each keyword is regex-escaped before being wrapped in word
boundaries, so a keyword containing regex metacharacters (`(`, `[`, `?`, ...)
can never raise a regex error.

## Build

```sh
cmake -B build && cmake --build build
# or, without CMake:
g++ -std=c++17 -O2 -Wall -Wextra -o chatlogparser chatlogparser.cpp
```

## Usage

```sh
chatlogparser [--keywords "a,b,c"] [--prefs preferences.json] [LOGFILE]
```

- `--keywords`  Comma-separated keywords (overrides `--prefs`).
- `--prefs`     Path to a JSON array of keyword strings, e.g. `["foo","bar"]`.
                Defaults to `preferences.json` if present. This is the same
                format used by the Python tool.
- `LOGFILE`     Chat log to scan. If omitted, the log is read from stdin.

### Examples

```sh
# Scan a file using the shared preferences.json
./chatlogparser --prefs ../preferences.json chat.txt

# Scan piped input with inline keywords
cat chat.txt | ./chatlogparser --keywords "meet, irl, asl"
```

For each match it prints `Keyword '<kw>' found at line <N>`. If nothing
matches it prints `Clean`. Exit status is `1` when any keyword is found, `0`
when the log is clean, and `2` on a usage/IO error.
