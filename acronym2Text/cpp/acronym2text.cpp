// acronym2text - C++ port of the acronym store/expansion core of
// acronym2Text.py.
//
// The Python program is a Tkinter tray app that also hooks the keyboard to
// expand acronyms as you type - behaviour that is inherently platform- and
// GUI-specific. This port extracts the portable, testable core: loading the
// acronym dictionary, expanding acronyms found in text, and explaining a
// single term. It has no third-party dependencies.
//
// Keywords/acronyms are matched as literal whole words, case-insensitively.
//
// Usage:
//   acronym2text [--prefs preferences.json] --list
//   acronym2text [--prefs preferences.json] --explain ASL
//   acronym2text [--prefs preferences.json] [--expand] [TEXTFILE]
//
//   --list      Print every abbreviation and its explanation.
//   --explain X Print the explanation for abbreviation X (exit 0), or exit 1.
//   --expand    Read text (TEXTFILE or stdin) and replace each whole-word
//               acronym with its explanation. This is the default mode.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -o acronym2text acronym2text.cpp

#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace {

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

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

std::string escape_regex(const std::string& in) {
    static const std::string specials = R"(\^$.|?*+()[]{})";
    std::string out;
    for (char c : in) {
        if (specials.find(c) != std::string::npos) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Parse the preferences.json schema used by the Python tool:
//   {"acronyms":[{"abbreviation":"ASL","explanation":"Age/Sex/Location"}, ...]}
// Pulls the abbreviation/explanation string values in document order and zips
// them. Tolerant of whitespace; handles \" and \\ escapes inside strings.
std::map<std::string, std::string> parse_prefs(const std::string& text) {
    std::map<std::string, std::string> result;
    std::regex field(R"RX("(abbreviation|explanation)"\s*:\s*"((?:[^"\\]|\\.)*)")RX");
    auto begin = std::sregex_iterator(text.begin(), text.end(), field);
    auto end = std::sregex_iterator();
    std::string pending_abbr;
    bool have_abbr = false;
    for (auto it = begin; it != end; ++it) {
        const std::string key = (*it)[1].str();
        std::string val = (*it)[2].str();
        // Unescape \" and \\ (enough for this schema).
        std::string unescaped;
        for (std::size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size()) unescaped.push_back(val[++i]);
            else unescaped.push_back(val[i]);
        }
        if (key == "abbreviation") {
            pending_abbr = unescaped;
            have_abbr = true;
        } else if (key == "explanation" && have_abbr) {
            result[to_lower(pending_abbr)] = unescaped;
            have_abbr = false;
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    std::string prefs_path = "preferences.json";
    std::string textfile;
    std::string explain_term;
    bool do_list = false, do_explain = false, do_expand = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--prefs" && i + 1 < argc) prefs_path = argv[++i];
        else if (a == "--list") do_list = true;
        else if (a == "--explain" && i + 1 < argc) { do_explain = true; explain_term = argv[++i]; }
        else if (a == "--expand") do_expand = true;
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: acronym2text [--prefs preferences.json] "
                         "[--list | --explain X | [--expand] [TEXTFILE]]\n";
            return 0;
        } else textfile = a;
    }

    std::string prefs_text;
    if (!read_file(prefs_path, prefs_text)) {
        std::cerr << "Error: could not read preferences file: " << prefs_path << "\n";
        return 2;
    }
    auto dict = parse_prefs(prefs_text);
    if (dict.empty()) {
        std::cerr << "Error: no acronyms found in " << prefs_path << "\n";
        return 2;
    }

    if (do_list) {
        for (const auto& [abbr, exp] : dict)
            std::cout << abbr << " = " << exp << "\n";
        return 0;
    }

    if (do_explain) {
        auto it = dict.find(to_lower(explain_term));
        if (it == dict.end()) {
            std::cerr << "No explanation for '" << explain_term << "'\n";
            return 1;
        }
        std::cout << explain_term << ": " << it->second << "\n";
        return 0;
    }

    // Default: expand acronyms in the provided text (file or stdin).
    (void)do_expand;
    std::string text;
    if (!textfile.empty()) {
        if (!read_file(textfile, text)) {
            std::cerr << "Error: could not read text file: " << textfile << "\n";
            return 2;
        }
    } else {
        text = read_stream(std::cin);
    }

    for (const auto& [abbr, exp] : dict) {
        if (abbr.empty()) continue;
        std::regex re("\\b" + escape_regex(abbr) + "\\b",
                      std::regex::icase | std::regex::ECMAScript);
        text = std::regex_replace(text, re, exp);
    }
    std::cout << text;
    return 0;
}
