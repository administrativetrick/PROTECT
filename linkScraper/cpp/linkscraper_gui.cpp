// linkscraper_gui - C++/Qt port of linkScraper.py.
//
// A Qt desktop app: enter a source URL, Add to scrape its <a href> links
// (resolved to absolute URLs) into a tree grouped by source, and Remove to
// delete a selected source or link. Backed by links.db (SQLite). HTTP is done
// with the system curl binary.

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QMessageBox>

#include <array>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace {
std::string shq(const std::string& s){std::string o="'";for(char c:s){if(c=='\'')o+="'\\''";else o.push_back(c);}o.push_back('\'');return o;}
bool http_get(const std::string& url, std::string& body){
    std::string cmd="curl -sL --max-time 15 -A 'PROTECT-linkScraper' -- "+shq(url);
    FILE* p=popen(cmd.c_str(),"r"); if(!p) return false; body.clear();
    std::array<char,8192> b; size_t n; while((n=fread(b.data(),1,b.size(),p))>0) body.append(b.data(),n);
    return pclose(p)==0 && !body.empty();
}
struct U{std::string scheme,host,path;bool ok=false;};
U parse_url(const std::string& u){U r;std::smatch m;std::regex re(R"RX(^([a-zA-Z][a-zA-Z0-9+.\-]*)://([^/]+)(/[^?#]*)?)RX");if(std::regex_search(u,m,re)){r.scheme=m[1];r.host=m[2];r.path=m[3].matched?m[3].str():"/";r.ok=true;}return r;}
std::string urljoin(const std::string& base,const std::string& link){
    if(std::regex_search(link,std::regex(R"RX(^[a-zA-Z][a-zA-Z0-9+.\-]*://)RX"))) return link;
    U b=parse_url(base); if(!b.ok) return link;
    if(link.rfind("//",0)==0) return b.scheme+":"+link;
    if(!link.empty()&&link[0]=='/') return b.scheme+"://"+b.host+link;
    std::string d=b.path; auto s=d.find_last_of('/'); d=(s==std::string::npos)?"/":d.substr(0,s+1);
    return b.scheme+"://"+b.host+d+link;
}
std::vector<std::string> hrefs(const std::string& html){
    std::vector<std::string> o; std::regex re(R"RX(<a\b[^>]*?\bhref\s*=\s*("([^"]*)"|'([^']*)'))RX",std::regex::icase);
    for(auto it=std::sregex_iterator(html.begin(),html.end(),re);it!=std::sregex_iterator();++it){const auto&m=*it;o.push_back(m[2].matched?m[2].str():m[3].str());}
    return o;
}
}  // namespace

class LinkScraper : public QWidget {
public:
    LinkScraper() {
        setWindowTitle("Link Scraper"); resize(540, 540);
        sqlite3_open("links.db", &db_);
        sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS links (source TEXT, link TEXT)", nullptr, nullptr, nullptr);
        auto* layout = new QVBoxLayout(this);
        source_ = new QLineEdit; source_->setPlaceholderText("https://example.com");
        layout->addWidget(source_);
        auto* add = new QPushButton("Add"); auto* rem = new QPushButton("Remove");
        layout->addWidget(add); layout->addWidget(rem);
        tree_ = new QTreeWidget; tree_->setHeaderHidden(true);
        layout->addWidget(tree_, 1);
        connect(add, &QPushButton::clicked, this, [this]{ fetch(); });
        connect(rem, &QPushButton::clicked, this, [this]{ del(); });
        build();
    }
    ~LinkScraper() override { if (db_) sqlite3_close(db_); }
private:
    void fetch() {
        std::string src = source_->text().trimmed().toStdString();
        if (src.empty()) { QMessageBox::information(this,"Info","Enter a source URL."); return; }
        std::string html;
        if (!http_get(src, html)) { QMessageBox::critical(this,"Error",QString::fromStdString("Failed to fetch "+src)); return; }
        auto hs = hrefs(html);
        if (hs.empty()) { QMessageBox::information(this,"Info","No links found."); return; }
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"INSERT INTO links VALUES (?,?)",-1,&st,nullptr);
        for (auto& h : hs) { std::string abs=urljoin(src,h);
            sqlite3_bind_text(st,1,src.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(st,2,abs.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(st); sqlite3_reset(st); }
        sqlite3_finalize(st);
        build();
    }
    void del() {
        for (auto* it : tree_->selectedItems()) {
            if (it->parent() == nullptr) {  // a source: delete it and its links
                sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"DELETE FROM links WHERE source=?",-1,&st,nullptr);
                sqlite3_bind_text(st,1,it->text(0).toUtf8().constData(),-1,SQLITE_TRANSIENT);
                sqlite3_step(st); sqlite3_finalize(st);
            } else {  // a single link under a specific source (scoped, not global)
                sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"DELETE FROM links WHERE source=? AND link=?",-1,&st,nullptr);
                sqlite3_bind_text(st,1,it->parent()->text(0).toUtf8().constData(),-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(st,2,it->text(0).toUtf8().constData(),-1,SQLITE_TRANSIENT);
                sqlite3_step(st); sqlite3_finalize(st);
            }
        }
        build();
    }
    void build() {
        tree_->clear();
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"SELECT source, link FROM links ORDER BY source",-1,&st,nullptr);
        QString cur; QTreeWidgetItem* parent=nullptr;
        while (sqlite3_step(st)==SQLITE_ROW) {
            QString s=reinterpret_cast<const char*>(sqlite3_column_text(st,0));
            QString l=reinterpret_cast<const char*>(sqlite3_column_text(st,1));
            if (s!=cur || !parent) { parent=new QTreeWidgetItem(tree_); parent->setText(0,s); cur=s; }
            auto* child=new QTreeWidgetItem(parent); child->setText(0,l);
        }
        sqlite3_finalize(st);
        tree_->expandAll();
    }
    sqlite3* db_=nullptr; QLineEdit* source_=nullptr; QTreeWidget* tree_=nullptr;
};

int main(int argc, char** argv) { QApplication app(argc,argv); LinkScraper w; w.show(); return app.exec(); }
