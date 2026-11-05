// acronym2text_gui - C++/Qt port of acronym2Text.py.
//
// Faithful desktop reimplementation of the Tkinter + pynput + pystray app:
//   - a window to add/remove abbreviations and toggle the listener,
//   - a system-tray icon (Show / Quit),
//   - clipboard monitoring that pops up an explanation when copied text
//     matches a stored acronym,
//   - a global keyboard hook that expands a typed abbreviation into its full
//     text (the C++ equivalent of the pynput keyboard listener), implemented
//     with the X11 RECORD and XTEST extensions.
//
// The keyboard hook needs an X11 session with the RECORD and XTEST extensions
// (as the Python version needs an active desktop); if they are unavailable it
// degrades to a no-op and the rest of the app still works.
//
// Build via CMake (Qt5Widgets + X11 + Xtst).

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QClipboard>
#include <QIcon>
#include <QPixmap>
#include <QCloseEvent>

#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/record.h>
#include <X11/XKBlib.h>

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

}  // namespace

// ---- Global keyboard expander (X11 RECORD + XTEST) -------------------------
class KeyExpander {
public:
    explicit KeyExpander(const std::map<std::string, std::string>* dict,
                         std::mutex* mtx)
        : dict_(dict), mtx_(mtx) {}

    ~KeyExpander() { stop(); }

    bool running() const { return running_.load(); }

    void start() {
        if (running_.load()) return;
        running_.store(true);
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        if (rec_ctrl_ && rec_ctx_) {
            XRecordDisableContext(rec_ctrl_, rec_ctx_);
            XFlush(rec_ctrl_);
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    static void callback(XPointer closure, XRecordInterceptData* data) {
        reinterpret_cast<KeyExpander*>(closure)->handle(data);
    }

    void handle(XRecordInterceptData* data) {
        if (data->category == XRecordFromServer && data->data_len > 0) {
            const unsigned char* d = reinterpret_cast<unsigned char*>(data->data);
            int type = d[0] & 0x7F;      // 2 = KeyPress, 3 = KeyRelease
            int keycode = d[1];
            if (type == KeyPress && !injecting_.load())
                on_keypress(keycode);
        }
        XRecordFreeData(data);
    }

    // Map a keycode to a keysym honouring the current shift state.
    KeySym keysym_for(int keycode) {
        int level = shift_ ? 1 : 0;
        return XkbKeycodeToKeysym(inj_, keycode, 0, level);
    }

    void on_keypress(int keycode) {
        KeySym ks = keysym_for(keycode);
        if (ks == XK_Shift_L || ks == XK_Shift_R) { shift_ = true; return; }
        if (ks == XK_BackSpace) { if (!buffer_.empty()) buffer_.pop_back(); return; }
        if (ks == XK_space || ks == XK_Return || ks == XK_KP_Enter) {
            try_expand(true);
            buffer_.clear();
            return;
        }
        if (ks >= 0x20 && ks <= 0x7e) {
            buffer_.push_back(static_cast<char>(ks));
            try_expand(false);
        } else {
            buffer_.clear();  // non-text key resets the run
        }
    }

    void try_expand(bool /*word_boundary*/) {
        std::string key = to_lower(buffer_);
        std::string expansion;
        {
            std::lock_guard<std::mutex> lock(*mtx_);
            auto it = dict_->find(key);
            if (it == dict_->end() || it->second.empty()) return;
            expansion = it->second;
        }
        inject_replacement(static_cast<int>(buffer_.size()), expansion);
        buffer_.clear();
    }

    void send_key(KeySym ks, bool shift) {
        KeyCode kc = XKeysymToKeycode(inj_, ks);
        if (!kc) return;
        KeyCode shift_kc = XKeysymToKeycode(inj_, XK_Shift_L);
        if (shift) XTestFakeKeyEvent(inj_, shift_kc, True, 0);
        XTestFakeKeyEvent(inj_, kc, True, 0);
        XTestFakeKeyEvent(inj_, kc, False, 0);
        if (shift) XTestFakeKeyEvent(inj_, shift_kc, False, 0);
        XFlush(inj_);
    }

    void inject_replacement(int backspaces, const std::string& text) {
        injecting_.store(true);
        for (int i = 0; i < backspaces; ++i) send_key(XK_BackSpace, false);
        for (char c : text) {
            KeySym ks = static_cast<unsigned char>(c);
            // Decide whether shift is needed by comparing levels.
            KeyCode kc = XKeysymToKeycode(inj_, ks);
            bool shift = false;
            if (kc) {
                KeySym low = XkbKeycodeToKeysym(inj_, kc, 0, 0);
                if (low != ks) shift = true;
            }
            send_key(ks, shift);
        }
        injecting_.store(false);
    }

    void run() {
        inj_ = XOpenDisplay(nullptr);
        rec_ctrl_ = XOpenDisplay(nullptr);
        if (!inj_ || !rec_ctrl_) { running_.store(false); return; }
        int major, minor;
        if (!XRecordQueryVersion(rec_ctrl_, &major, &minor)) {
            running_.store(false); return;  // RECORD extension missing
        }
        XRecordRange* range = XRecordAllocRange();
        range->device_events.first = KeyPress;
        range->device_events.last = KeyRelease;
        XRecordClientSpec clients = XRecordAllClients;
        rec_ctx_ = XRecordCreateContext(rec_ctrl_, 0, &clients, 1, &range, 1);
        XFree(range);
        XSync(rec_ctrl_, False);
        // Blocking; returns when XRecordDisableContext is called from stop().
        XRecordEnableContext(rec_ctrl_, rec_ctx_, &KeyExpander::callback,
                             reinterpret_cast<XPointer>(this));
        XRecordFreeContext(rec_ctrl_, rec_ctx_);
        rec_ctx_ = 0;
        XCloseDisplay(rec_ctrl_); rec_ctrl_ = nullptr;
        XCloseDisplay(inj_); inj_ = nullptr;
    }

    const std::map<std::string, std::string>* dict_;
    std::mutex* mtx_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> injecting_{false};
    Display* inj_ = nullptr;
    Display* rec_ctrl_ = nullptr;
    XRecordContext rec_ctx_ = 0;
    std::string buffer_;
    bool shift_ = false;
};

// ---- Main window -----------------------------------------------------------
class Acronym2TextWindow : public QWidget {
public:
    Acronym2TextWindow() : expander_(&dict_, &mtx_) {
        setWindowTitle("Acronym2Text");
        resize(360, 220);
        load();

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Abbreviation:"));
        abbr_ = new QLineEdit; layout->addWidget(abbr_);
        layout->addWidget(new QLabel("Expansion:"));
        exp_ = new QLineEdit; layout->addWidget(exp_);

        auto* add = new QPushButton("Add Abbreviation");
        auto* rem = new QPushButton("Remove Abbreviation");
        toggle_ = new QPushButton("Start Listener");
        layout->addWidget(add);
        layout->addWidget(rem);
        layout->addWidget(toggle_);

        connect(add, &QPushButton::clicked, this, [this] {
            add_abbr(abbr_->text().toStdString(), exp_->text().toStdString());
        });
        connect(rem, &QPushButton::clicked, this, [this] {
            remove_abbr(abbr_->text().toStdString());
        });
        connect(toggle_, &QPushButton::clicked, this, [this] { toggle(); });

        setup_tray();

        clip_timer_ = new QTimer(this);
        connect(clip_timer_, &QTimer::timeout, this, [this] { check_clipboard(); });
        clip_timer_->start(500);
    }

private:
    QString prefs_path() const { return "preferences.json"; }

    void load() {
        std::lock_guard<std::mutex> lock(mtx_);
        dict_.clear();
        std::ifstream f(prefs_path().toStdString(), std::ios::binary);
        if (!f) return;
        std::string text((std::istreambuf_iterator<char>(f)), {});
        // Pull "abbreviation"/"explanation" string values in order and zip.
        std::string pending; bool have = false; std::size_t i = 0;
        auto read_string = [&](std::size_t& p) -> std::string {
            std::string out; bool in = false;
            for (; p < text.size(); ++p) {
                char c = text[p];
                if (!in) { if (c == '"') in = true; }
                else if (c == '\\' && p + 1 < text.size()) out.push_back(text[++p]);
                else if (c == '"') { ++p; return out; }
                else out.push_back(c);
            }
            return out;
        };
        while (i < text.size()) {
            std::size_t key_pos = text.find('"', i);
            if (key_pos == std::string::npos) break;
            std::size_t kp = key_pos;
            std::string key = read_string(kp);
            if (key == "abbreviation" || key == "explanation") {
                std::size_t vp = kp;
                std::string val = read_string(vp);
                if (key == "abbreviation") { pending = val; have = true; }
                else if (have) { dict_[to_lower(pending)] = val; have = false; }
                i = vp;
            } else {
                i = kp;
            }
        }
    }

    void save() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ofstream f(prefs_path().toStdString(), std::ios::binary);
        f << "{\n    \"acronyms\": [\n";
        std::size_t n = 0;
        for (const auto& [a, e] : dict_) {
            f << "        {\n            \"abbreviation\": \"" << a
              << "\",\n            \"explanation\": \"" << e << "\"\n        }";
            if (++n < dict_.size()) f << ",";
            f << "\n";
        }
        f << "    ]\n}\n";
    }

    void add_abbr(const std::string& a, const std::string& e) {
        if (a.empty()) return;
        { std::lock_guard<std::mutex> lock(mtx_); dict_[to_lower(a)] = e; }
        save();
    }

    void remove_abbr(const std::string& a) {
        { std::lock_guard<std::mutex> lock(mtx_); dict_.erase(to_lower(a)); }
        save();
    }

    void toggle() {
        if (expander_.running()) {
            expander_.stop();
            toggle_->setText("Start Listener");
        } else {
            expander_.start();
            toggle_->setText(expander_.running() ? "Stop Listener"
                                                 : "Start Listener (unavailable)");
        }
    }

    void setup_tray() {
        QPixmap pm(16, 16); pm.fill(Qt::darkCyan);
        tray_ = new QSystemTrayIcon(QIcon(pm), this);
        auto* menu = new QMenu(this);
        menu->addAction("Show", this, [this] { showNormal(); raise(); });
        menu->addAction("Quit", qApp, &QApplication::quit);
        tray_->setContextMenu(menu);
        tray_->setToolTip("Acronym2Text");
        tray_->show();
    }

    void check_clipboard() {
        QString text = QApplication::clipboard()->text();
        if (text.isEmpty()) return;
        std::string key = to_lower(text.toStdString());
        std::string expansion;
        { std::lock_guard<std::mutex> lock(mtx_);
          auto it = dict_.find(key);
          if (it == dict_.end()) return;
          expansion = it->second; }
        if (key == last_shown_) return;
        last_shown_ = key;
        QMessageBox::information(this, "Acronym Explanation",
            text + ": " + QString::fromStdString(expansion));
    }

    void closeEvent(QCloseEvent* e) override { hide(); e->ignore(); }  // minimise to tray

    std::map<std::string, std::string> dict_;
    std::mutex mtx_;
    KeyExpander expander_;
    QLineEdit* abbr_ = nullptr;
    QLineEdit* exp_ = nullptr;
    QPushButton* toggle_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QTimer* clip_timer_ = nullptr;
    std::string last_shown_;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);  // live in the tray
    Acronym2TextWindow w;
    w.show();
    return app.exec();
}
