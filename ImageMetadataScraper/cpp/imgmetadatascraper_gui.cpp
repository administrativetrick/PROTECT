// imgmetadatascraper_gui - C++/Qt port of ImageMetadataScraper/imgmetadatascraper.py.
//
// Enter a page URL and optional notes, then Add to scrape its <img> tags,
// fetch each image, and record the image format and dimensions (read from the
// file header) in image_metadata.db. Edit updates the notes on a selected row;
// Remove deletes a row; Filter limits the view by URL or metadata key. The
// images themselves are not stored. (Embedded EXIF tags would need libexif.)
// HTTP uses the system curl binary.

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QMessageBox>
#include <QInputDialog>

#include <array>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace {
std::string shq(const std::string& s){std::string o="'";for(char c:s){if(c=='\'')o+="'\\''";else o.push_back(c);}o.push_back('\'');return o;}
bool http_get(const std::string& url, std::string& body){
    std::string cmd="curl -sL --max-time 15 -A 'PROTECT-ImageMetadataScraper' -- "+shq(url);
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
std::vector<std::string> img_src(const std::string& html){
    std::vector<std::string> o; std::regex re(R"RX(<img\b[^>]*?\bsrc\s*=\s*("([^"]*)"|'([^']*)'))RX",std::regex::icase);
    for(auto it=std::sregex_iterator(html.begin(),html.end(),re);it!=std::sregex_iterator();++it){const auto&m=*it;std::string s=m[2].matched?m[2].str():m[3].str(); if(!s.empty()) o.push_back(s);}
    return o;
}
uint16_t be16(const unsigned char*p){return (uint16_t(p[0])<<8)|p[1];}
uint32_t be32(const unsigned char*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
uint16_t le16(const unsigned char*p){return (uint16_t(p[1])<<8)|p[0];}
std::string image_info(const std::string& data){
    const auto* b=reinterpret_cast<const unsigned char*>(data.data()); size_t n=data.size();
    static const unsigned char sig[8]={0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    if(n>=24 && std::equal(sig,sig+8,b)) return "PNG "+std::to_string(be32(b+16))+"x"+std::to_string(be32(b+20));
    if(n>=10 && b[0]=='G'&&b[1]=='I'&&b[2]=='F') return "GIF "+std::to_string(le16(b+6))+"x"+std::to_string(le16(b+8));
    if(n>=4 && b[0]==0xFF&&b[1]==0xD8){ size_t i=2; while(i+9<n){ if(b[i]!=0xFF){++i;continue;} unsigned char mk=b[i+1];
        if(mk==0xD8||mk==0xD9||mk==0x01||(mk>=0xD0&&mk<=0xD7)){i+=2;continue;} uint16_t seg=be16(b+i+2);
        bool sof=(mk>=0xC0&&mk<=0xCF)&&mk!=0xC4&&mk!=0xC8&&mk!=0xCC;
        if(sof) return "JPEG "+std::to_string(be16(b+i+7))+"x"+std::to_string(be16(b+i+5));
        i+=2+seg;
    } }
    return "unknown, "+std::to_string(n)+" bytes";
}
}  // namespace

class MetaScraper : public QWidget {
public:
    MetaScraper() {
        setWindowTitle("Website Image Metadata Scraper"); resize(760, 520);
        sqlite3_open("image_metadata.db", &db_);
        sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, metadata_key TEXT, metadata_value TEXT, notes TEXT)", nullptr, nullptr, nullptr);
        auto* layout=new QVBoxLayout(this);
        tree_=new QTreeWidget; tree_->setColumnCount(4); tree_->setHeaderLabels({"URL","Metadata Key","Metadata Value","Notes"});
        layout->addWidget(tree_,1);
        layout->addWidget(new QLabel("URL:")); url_=new QLineEdit; layout->addWidget(url_);
        layout->addWidget(new QLabel("Notes:")); notes_=new QLineEdit; layout->addWidget(notes_);
        layout->addWidget(new QLabel("Filter:")); filter_=new QLineEdit; layout->addWidget(filter_);
        auto* add=new QPushButton("Add"); auto* edit=new QPushButton("Edit");
        auto* rem=new QPushButton("Remove"); auto* filt=new QPushButton("Filter");
        layout->addWidget(add); layout->addWidget(edit); layout->addWidget(rem); layout->addWidget(filt);
        connect(add,&QPushButton::clicked,this,[this]{ do_add(); });
        connect(edit,&QPushButton::clicked,this,[this]{ do_edit(); });
        connect(rem,&QPushButton::clicked,this,[this]{ do_remove(); });
        connect(filt,&QPushButton::clicked,this,[this]{ update_tree(filter_->text()); });
        update_tree("");
    }
    ~MetaScraper() override { if(db_) sqlite3_close(db_); }
private:
    void do_add() {
        std::string page=url_->text().trimmed().toStdString();
        std::string notes=notes_->text().toStdString();
        if(page.empty()){ QMessageBox::critical(this,"Error","Please enter a URL"); return; }
        std::string html;
        if(!http_get(page,html)){ QMessageBox::critical(this,"Error",QString::fromStdString("Failed to fetch "+page)); return; }
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"INSERT INTO images (url, metadata_key, metadata_value, notes) VALUES (?,?,?,?)",-1,&st,nullptr);
        int found=0;
        for(const auto& src: img_src(html)){ std::string iu=urljoin(page,src); std::string bytes;
            if(!http_get(iu,bytes)) continue;
            std::string val=image_info(bytes);
            sqlite3_bind_text(st,1,iu.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(st,2,"Dimensions",-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(st,3,val.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(st,4,notes.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(st); sqlite3_reset(st); ++found; }
        sqlite3_finalize(st);
        update_tree(filter_->text());
        QMessageBox::information(this,"Success",QString("%1 image(s) added.").arg(found));
    }
    void do_edit() {
        auto items=tree_->selectedItems();
        if(items.isEmpty()){ QMessageBox::warning(this,"No selection","No image selected."); return; }
        bool ok=false; QString notes=QInputDialog::getText(this,"Input","Enter new notes:",QLineEdit::Normal,"",&ok);
        if(!ok) return;
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"UPDATE images SET notes=? WHERE url=?",-1,&st,nullptr);
        sqlite3_bind_text(st,1,notes.toUtf8().constData(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(st,2,items[0]->text(0).toUtf8().constData(),-1,SQLITE_TRANSIENT);
        sqlite3_step(st); sqlite3_finalize(st);
        update_tree(filter_->text());
    }
    void do_remove() {
        for(auto* it: tree_->selectedItems()){
            sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"DELETE FROM images WHERE url=?",-1,&st,nullptr);
            sqlite3_bind_text(st,1,it->text(0).toUtf8().constData(),-1,SQLITE_TRANSIENT);
            sqlite3_step(st); sqlite3_finalize(st); }
        update_tree(filter_->text());
    }
    void update_tree(const QString& filter) {
        tree_->clear();
        sqlite3_stmt* st=nullptr;
        if(filter.isEmpty()) sqlite3_prepare_v2(db_,"SELECT url, metadata_key, metadata_value, notes FROM images",-1,&st,nullptr);
        else { sqlite3_prepare_v2(db_,"SELECT url, metadata_key, metadata_value, notes FROM images WHERE url LIKE ? OR metadata_key LIKE ?",-1,&st,nullptr);
            std::string like="%"+filter.toStdString()+"%"; sqlite3_bind_text(st,1,like.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(st,2,like.c_str(),-1,SQLITE_TRANSIENT); }
        while(sqlite3_step(st)==SQLITE_ROW){ auto* it=new QTreeWidgetItem(tree_);
            for(int c=0;c<4;++c) it->setText(c,reinterpret_cast<const char*>(sqlite3_column_text(st,c))); }
        sqlite3_finalize(st);
    }
    sqlite3* db_=nullptr; QTreeWidget* tree_=nullptr; QLineEdit* url_=nullptr; QLineEdit* notes_=nullptr; QLineEdit* filter_=nullptr;
};

int main(int argc, char** argv){ QApplication app(argc,argv); MetaScraper w; w.show(); return app.exec(); }
