// chatlogparser - C++ port of the ChatLogsParser keyword-scanning core.
//
// This is a dependency-free command-line reimplementation of the matching
// logic in ChatLogsParser.py. It reads a list of keywords and scans a chat
// log line by line, reporting the line number of every whole-word,
// case-insensitive match.
//
// Like the fixed Python version, keywords are treated as *literal* text:
// each one is regex-escaped before being wrapped in word boundaries, so a
// keyword containing regex metacharacters (such as "(", "[" or "?") can never
// raise a regex error or match unintended input.
//
// Usage:
//   chatlogparser [--keywords "a,b,c"] [--prefs preferences.json] [LOGFILE]
//
//   --keywords  Comma-separated keywords (overrides --prefs).
//   --prefs     Path to a JSON array of keyword strings, e.g. ["foo","bar"].
//               Defaults to "preferences.json" if present.
//   LOGFILE     Chat log to scan. If omitted, the log is read from stdin.
//
// Build:  cmake -B build && cmake --build build
//    or:  g++ -std=c++17 -O2 -Wall -Wextra -o chatlogparser chatlogparser.cpp

#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Escape the ECMAScript regex metacharacters so a keyword is matched as a
// literal string. Mirrors Python's re.escape for the characters that matter.
std::string escape_regex(const std::string& in) {
    static const std::string specials = R"(\^$.|?*+()[]{})";
    std::string out;
    out.reserve(in.size() * 2);
    for (char c : in) {
        if (specials.find(c) != std::string::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = trim(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// Minimal, tolerant reader for a flat JSON array of strings: ["a", "b"].
// Extracts the contents of each double-quoted string, honouring \" and \\.
std::vector<std::string> parse_json_string_array(const std::string& text) {
    std::vector<std::string> out;
    bool in_string = false;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_string) {
            if (c == '\\' && i + 1 < text.size()) {
                char next = text[++i];
                current.push_back(next);  // keep escaped char literally
            } else if (c == '"') {
                out.push_back(current);
                current.clear();
                in_string = false;
            } else {
                current.push_back(c);
            }
        } else if (c == '"') {
            in_string = true;
        }
    }
    return out;
}

std::string read_stream(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out = read_stream(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string keywords_arg;
    std::string prefs_path = "preferences.json";
    std::string logfile;
    bool have_keywords_arg = false;
    bool have_prefs_arg = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--keywords" && i + 1 < argc) {
            keywords_arg = argv[++i];
            have_keywords_arg = true;
        } else if (a == "--prefs" && i + 1 < argc) {
            prefs_path = argv[++i];
            have_prefs_arg = true;
        } else if (a == "-h" || a == "--help") {
            std::cout <<
                "Usage: chatlogparser [--keywords \"a,b,c\"] "
                "[--prefs preferences.json] [LOGFILE]\n";
            return 0;
        } else {
            logfile = a;
        }
    }

    // Resolve the keyword list.
    std::vector<std::string> keywords;
    if (have_keywords_arg) {
        keywords = split_csv(keywords_arg);
    } else {
        std::string prefs_text;
        if (read_file(prefs_path, prefs_text)) {
            keywords = parse_json_string_array(prefs_text);
        } else if (have_prefs_arg) {
            std::cerr << "Error: could not read preferences file: "
                      << prefs_path << "\n";
            return 2;
        }
    }

    if (keywords.empty()) {
        std::cerr << "Error: no keywords provided. Use --keywords or supply a "
                     "preferences.json containing a JSON array of strings.\n";
        return 2;
    }

    // Precompile one escaped, case-insensitive, word-bounded pattern each.
    std::vector<std::pair<std::string, std::regex>> patterns;
    patterns.reserve(keywords.size());
    for (const auto& kw : keywords) {
        if (kw.empty()) continue;
        try {
            std::regex re("\\b" + escape_regex(kw) + "\\b",
                          std::regex::icase | std::regex::ECMAScript);
            patterns.emplace_back(kw, std::move(re));
        } catch (const std::regex_error& e) {
            std::cerr << "Warning: skipping keyword '" << kw
                      << "': " << e.what() << "\n";
        }
    }

    // Read the log (file or stdin).
    std::string log;
    if (!logfile.empty()) {
        if (!read_file(logfile, log)) {
            std::cerr << "Error: could not read log file: " << logfile << "\n";
            return 2;
        }
    } else {
        log = read_stream(std::cin);
    }

    // Scan line by line.
    std::istringstream ls(log);
    std::string line;
    std::size_t line_no = 0;
    std::set<std::string> found;
    bool any = false;
    while (std::getline(ls, line)) {
        ++line_no;
        for (const auto& [kw, re] : patterns) {
            if (std::regex_search(line, re)) {
                found.insert(kw);
                any = true;
                std::cout << "Keyword '" << kw << "' found at line "
                          << line_no << "\n";
            }
        }
    }

    if (!any) {
        std::cout << "Clean\n";
    }
    return any ? 1 : 0;
}
