// chatlogparser_gui - C++/Qt port of ChatLogsParser.py.
//
// A faithful Qt desktop reimplementation of the Tkinter keyword-search app:
// a chat-log text area, Submit and Preferences buttons, a results table, and
// Add/Remove controls, backed by the same keyword_results.db SQLite schema
// and chats/ directory. Keywords are matched as literal whole words
// (regex-escaped) so metacharacter keywords cannot crash the scan.
//
// Build via CMake (Qt5Widgets + SQLite3).

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>

#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace {

std::string escape_regex(const std::string& in) {
    static const std::string specials = R"(\^$.|?*+()[]{})";
    std::string out;
    for (char c : in) { if (specials.find(c) != std::string::npos) out.push_back('\\'); out.push_back(c); }
    return out;
}

std::vector<std::string> parse_json_string_array(const std::string& text) {
    std::vector<std::string> out; bool in_str = false; std::string cur;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_str) {
            if (c == '\\' && i + 1 < text.size()) cur.push_back(text[++i]);
            else if (c == '"') { out.push_back(cur); cur.clear(); in_str = false; }
            else cur.push_back(c);
        } else if (c == '"') in_str = true;
    }
    return out;
}

}  // namespace

class KeywordSearchApp : public QMainWindow {
public:
    KeywordSearchApp() {
        setWindowTitle("Chat Logs Parser");
        resize(820, 640);

        load_preferences();

        if (sqlite3_open("keyword_results.db", &db_) != SQLITE_OK) {
            QMessageBox::critical(this, "Error", "Cannot open keyword_results.db");
        }
        sqlite3_exec(db_,
            "CREATE TABLE IF NOT EXISTS results (id INTEGER PRIMARY KEY, "
            "chat_log TEXT, result TEXT)", nullptr, nullptr, nullptr);

        QDir().mkpath("chats");

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);

        text_field_ = new QTextEdit;
        text_field_->setPlaceholderText("Paste chat logs here, one message per line...");
        layout->addWidget(text_field_, 1);

        auto* submit = new QPushButton("Submit");
        auto* prefs = new QPushButton("Preferences");
        layout->addWidget(submit);
        layout->addWidget(prefs);

        tree_ = new QTreeWidget;
        tree_->setColumnCount(2);
        tree_->setHeaderLabels({"Chat Log", "Result"});
        tree_->header()->resizeSection(0, 280);
        layout->addWidget(tree_, 2);

        auto* add = new QPushButton("Add item");
        auto* remove = new QPushButton("Remove item");
        layout->addWidget(add);
        layout->addWidget(remove);

        setCentralWidget(central);

        connect(submit, &QPushButton::clicked, this, &KeywordSearchApp::parse_logs);
        connect(prefs, &QPushButton::clicked, this, &KeywordSearchApp::update_keywords);
        connect(add, &QPushButton::clicked, this, &KeywordSearchApp::add_item);
        connect(remove, &QPushButton::clicked, this, &KeywordSearchApp::remove_item);

        refresh_tree();
    }

    ~KeywordSearchApp() override { if (db_) sqlite3_close(db_); }

private:
    void load_preferences() {
        keywords_.clear();
        std::ifstream f("preferences.json", std::ios::binary);
        if (f) {
            std::string text((std::istreambuf_iterator<char>(f)), {});
            keywords_ = parse_json_string_array(text);
        }
        if (keywords_.empty()) keywords_ = {"keyword1", "keyword2"};
    }

    void save_preferences() {
        std::ofstream f("preferences.json", std::ios::binary);
        f << "[";
        for (std::size_t i = 0; i < keywords_.size(); ++i) {
            if (i) f << ", ";
            f << "\"" << keywords_[i] << "\"";
        }
        f << "]";
    }

    void update_keywords() {
        QString current;
        for (std::size_t i = 0; i < keywords_.size(); ++i) {
            if (i) current += ", ";
            current += QString::fromStdString(keywords_[i]);
        }
        bool ok = false;
        QString text = QInputDialog::getText(this, "Preferences",
            "Enter new keywords separated by commas", QLineEdit::Normal, current, &ok);
        if (!ok) return;
        if (text.trimmed().isEmpty()) {
            QMessageBox::critical(this, "Error", "Input field cannot be empty");
            return;
        }
        keywords_.clear();
        for (const QString& part : text.split(',')) {
            QString t = part.trimmed();
            if (!t.isEmpty()) keywords_.push_back(t.toStdString());
        }
        save_preferences();
    }

    void parse_logs() {
        QString chat_logs = text_field_->toPlainText().trimmed();
        if (chat_logs.isEmpty()) {
            QMessageBox::critical(this, "Error", "Chat log field cannot be empty");
            return;
        }
        std::string text = chat_logs.toStdString();
        std::istringstream ss(text);
        std::string line;
        std::size_t idx = 0;
        std::set<std::string> found;
        std::string result;
        while (std::getline(ss, line)) {
            ++idx;
            for (const auto& kw : keywords_) {
                if (kw.empty()) continue;
                try {
                    std::regex re("\\b" + escape_regex(kw) + "\\b",
                                  std::regex::icase | std::regex::ECMAScript);
                    if (std::regex_search(line, re)) {
                        found.insert(kw);
                        result += "Keyword '" + kw + "' found at line " +
                                  std::to_string(idx) + "\n";
                    }
                } catch (const std::regex_error&) { /* escaped: cannot happen */ }
            }
        }
        if (found.empty()) result = "Clean\n";

        // First unused chat filename (deletion-safe).
        std::string chat_file;
        for (int i = 1;; ++i) {
            chat_file = "chats/chat_" + std::to_string(i) + ".txt";
            if (!QFileInfo::exists(QString::fromStdString(chat_file))) break;
        }
        std::ofstream(chat_file, std::ios::binary) << text;

        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "INSERT INTO results (chat_log, result) VALUES (?, ?)",
                           -1, &st, nullptr);
        sqlite3_bind_text(st, 1, chat_file.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, result.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
        refresh_tree();
    }

    void refresh_tree() {
        tree_->clear();
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT chat_log, result FROM results", -1, &st, nullptr);
        while (sqlite3_step(st) == SQLITE_ROW) {
            auto* item = new QTreeWidgetItem(tree_);
            item->setText(0, reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
            item->setText(1, reinterpret_cast<const char*>(sqlite3_column_text(st, 1)));
        }
        sqlite3_finalize(st);
    }

    void add_item() {
        bool ok = false;
        QString chat_log = QInputDialog::getText(this, "Add item", "Enter chat log",
                                                  QLineEdit::Normal, "", &ok);
        if (!ok) return;
        QString result = QInputDialog::getText(this, "Add item", "Enter result",
                                                QLineEdit::Normal, "", &ok);
        if (!ok) return;
        if (chat_log.isEmpty() || result.isEmpty()) {
            QMessageBox::critical(this, "Error", "Input fields cannot be empty");
            return;
        }
        if (!QFileInfo::exists(chat_log)) {
            QMessageBox::critical(this, "Error", "Chat log file does not exist");
            return;
        }
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "INSERT INTO results (chat_log, result) VALUES (?, ?)",
                           -1, &st, nullptr);
        sqlite3_bind_text(st, 1, chat_log.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, result.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
        refresh_tree();
    }

    void remove_item() {
        auto items = tree_->selectedItems();
        if (items.isEmpty()) {
            QMessageBox::critical(this, "Error", "No item selected");
            return;
        }
        for (auto* it : items) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "DELETE FROM results WHERE chat_log = ? AND result = ?",
                               -1, &st, nullptr);
            sqlite3_bind_text(st, 1, it->text(0).toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, it->text(1).toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        refresh_tree();
    }

    sqlite3* db_ = nullptr;
    std::vector<std::string> keywords_;
    QTextEdit* text_field_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    KeywordSearchApp w;
    w.show();
    return app.exec();
}
