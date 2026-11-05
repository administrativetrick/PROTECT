// websitecloner_gui - C++/Qt port of websiteCloner.py.
//
// Enter a URL and Clone to save the page HTML under WebsiteClones/ (images are
// intentionally not downloaded); the (url, path) pair is stored in
// clone_data.db and shown in a tree. Double-click a row to open the saved
// HTML. HTTP uses the system curl binary; files are written UTF-8 with a
// sanitised name.

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>

#include <array>
#include <cstdio>
#include <fstream>
#include <regex>
#include <string>
#include <sqlite3.h>

namespace {
std::string shq(const std::string& s){std::string o="'";for(char c:s){if(c=='\'')o+="'\\''";else o.push_back(c);}o.push_back('\'');return o;}
bool http_get(const std::string& url, std::string& body){
    std::string cmd="curl -sL --max-time 15 -A 'PROTECT-websiteCloner' -- "+shq(url);
    FILE* p=popen(cmd.c_str(),"r"); if(!p) return false; body.clear();
    std::array<char,8192> b; size_t n; while((n=fread(b.data(),1,b.size(),p))>0) body.append(b.data(),n);
    return pclose(p)==0 && !body.empty();
}
std::string sanitize(const std::string& url){
    std::string s=std::regex_replace(url,std::regex(R"RX(^https?://)RX"),"");
    std::string out; for(char c:s){ if(std::isalnum((unsigned char)c)||c=='.'||c=='_'||c=='-') out.push_back(c); else out.push_back('_'); }
    if(out.size()>150) out=out.substr(0,150);
    if(out.empty()) out="clone";
    return out+".html";
}
}  // namespace

class WebsiteCloner : public QWidget {
public:
    WebsiteCloner() {
        setWindowTitle("Website Cloner"); resize(460, 460);
        sqlite3_open("clone_data.db", &db_);
        sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS clones (url TEXT PRIMARY KEY, path TEXT)", nullptr, nullptr, nullptr);
        auto* layout=new QVBoxLayout(this);
        layout->addWidget(new QLabel("Website URL:"));
        url_=new QLineEdit; layout->addWidget(url_);
        auto* clone=new QPushButton("Clone"); layout->addWidget(clone);
        tree_=new QTreeWidget; tree_->setColumnCount(2); tree_->setHeaderLabels({"URL","File Path"});
        layout->addWidget(tree_,1);
        connect(clone,&QPushButton::clicked,this,[this]{ do_clone(); });
        connect(tree_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* it,int){
            if(!it) return;
            QString path=it->text(1);
            if(path.isEmpty()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
        });
        load();
    }
    ~WebsiteCloner() override { if(db_) sqlite3_close(db_); }
private:
    void do_clone() {
        std::string url=url_->text().trimmed().toStdString();
        if(url.empty()){ QMessageBox::critical(this,"Error","Please enter a URL"); return; }
        std::string html;
        if(!http_get(url,html)){ QMessageBox::critical(this,"Error",QString::fromStdString("Failed to fetch "+url)); return; }
        QDir().mkpath("WebsiteClones");
        std::string path="WebsiteClones/"+sanitize(url);
        std::ofstream(path,std::ios::binary)<<html;
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"INSERT OR REPLACE INTO clones VALUES (?,?)",-1,&st,nullptr);
        sqlite3_bind_text(st,1,url.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(st,2,path.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_step(st); sqlite3_finalize(st);
        load();
    }
    void load() {
        tree_->clear();
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"SELECT url, path FROM clones",-1,&st,nullptr);
        while(sqlite3_step(st)==SQLITE_ROW){ auto* it=new QTreeWidgetItem(tree_);
            it->setText(0,reinterpret_cast<const char*>(sqlite3_column_text(st,0)));
            it->setText(1,reinterpret_cast<const char*>(sqlite3_column_text(st,1))); }
        sqlite3_finalize(st);
    }
    sqlite3* db_=nullptr; QLineEdit* url_=nullptr; QTreeWidget* tree_=nullptr;
};

int main(int argc, char** argv){ QApplication app(argc,argv); WebsiteCloner w; w.show(); return app.exec(); }
