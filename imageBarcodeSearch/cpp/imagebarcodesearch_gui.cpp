// imagebarcodesearch_gui - C++/Qt port of imageBarcodeSearch.py.
//
// Browse to a JPEG/PNG image; barcodes/QR codes are decoded with ZBar and
// listed and stored in barcodes.db. Remove deletes the selected row. Numeric
// EAN/UPC payloads are zero-padded to 13 digits; other payloads are kept
// verbatim. Images are decoded to grayscale with libjpeg/libpng (no OpenCV).

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QFileDialog>
#include <QMessageBox>

#include <cctype>
#include <cstdio>
#include <csetjmp>
#include <string>
#include <vector>
#include <sqlite3.h>
#include <zbar.h>
#include <jpeglib.h>
#include <png.h>

namespace {
struct Gray { int w=0,h=0; std::vector<unsigned char> px;
    bool ok() const { return w>0&&h>0&&px.size()==(size_t)w*h; } };
bool ends(const std::string& s,const std::string& e){ if(s.size()<e.size()) return false; std::string a=s.substr(s.size()-e.size()); for(char&c:a)c=(char)std::tolower((unsigned char)c); return a==e; }
struct JErr{ jpeg_error_mgr m; jmp_buf jb; };
void jerr_exit(j_common_ptr c){ longjmp(reinterpret_cast<JErr*>(c->err)->jb,1); }
Gray load_jpeg(const std::string& path){ Gray g; FILE* f=fopen(path.c_str(),"rb"); if(!f) return g;
    jpeg_decompress_struct ci; JErr je; ci.err=jpeg_std_error(&je.m); je.m.error_exit=jerr_exit;
    if(setjmp(je.jb)){ jpeg_destroy_decompress(&ci); fclose(f); return Gray{}; }
    jpeg_create_decompress(&ci); jpeg_stdio_src(&ci,f); jpeg_read_header(&ci,TRUE);
    ci.out_color_space=JCS_GRAYSCALE; jpeg_start_decompress(&ci);
    g.w=ci.output_width; g.h=ci.output_height; g.px.resize((size_t)g.w*g.h);
    while(ci.output_scanline<ci.output_height){ unsigned char* row=g.px.data()+(size_t)ci.output_scanline*g.w; jpeg_read_scanlines(&ci,&row,1); }
    jpeg_finish_decompress(&ci); jpeg_destroy_decompress(&ci); fclose(f); return g; }
Gray load_png(const std::string& path){ Gray g; FILE* f=fopen(path.c_str(),"rb"); if(!f) return g;
    png_structp p=png_create_read_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr); if(!p){fclose(f);return g;}
    png_infop i=png_create_info_struct(p); if(!i){png_destroy_read_struct(&p,nullptr,nullptr);fclose(f);return g;}
    if(setjmp(png_jmpbuf(p))){ png_destroy_read_struct(&p,&i,nullptr); fclose(f); return Gray{}; }
    png_init_io(p,f); png_read_info(p,i); png_set_expand(p); png_set_strip_16(p); png_set_strip_alpha(p);
    png_set_rgb_to_gray_fixed(p,1,-1,-1); png_set_packing(p); png_read_update_info(p,i);
    g.w=png_get_image_width(p,i); g.h=png_get_image_height(p,i); g.px.resize((size_t)g.w*g.h);
    std::vector<png_bytep> rows(g.h); for(int y=0;y<g.h;++y) rows[y]=g.px.data()+(size_t)y*g.w;
    png_read_image(p,rows.data()); png_read_end(p,nullptr); png_destroy_read_struct(&p,&i,nullptr); fclose(f); return g; }
Gray load_image(const std::string& p){ if(ends(p,".png")) return load_png(p); if(ends(p,".jpg")||ends(p,".jpeg")) return load_jpeg(p); Gray g=load_png(p); return g.ok()?g:load_jpeg(p); }
std::string normalize(const std::string& d){ bool num=!d.empty(); for(char c:d) if(!std::isdigit((unsigned char)c)){num=false;break;} if(num&&d.size()<13) return std::string(13-d.size(),'0')+d; return d; }
}  // namespace

class BarcodeSearch : public QWidget {
public:
    BarcodeSearch() {
        setWindowTitle("Image Barcode Search"); resize(420, 420);
        sqlite3_open("barcodes.db", &db_);
        sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS barcodes (data TEXT)", nullptr, nullptr, nullptr);
        auto* layout=new QVBoxLayout(this);
        tree_=new QTreeWidget; tree_->setColumnCount(1); tree_->setHeaderLabels({"Barcode"});
        layout->addWidget(tree_,1);
        auto* browse=new QPushButton("Browse"); auto* rem=new QPushButton("Remove selected");
        layout->addWidget(browse); layout->addWidget(rem);
        connect(browse,&QPushButton::clicked,this,[this]{ do_browse(); });
        connect(rem,&QPushButton::clicked,this,[this]{ do_remove(); });
        load();
    }
    ~BarcodeSearch() override { if(db_) sqlite3_close(db_); }
private:
    void do_browse() {
        QString fn=QFileDialog::getOpenFileName(this,"Open image","",
            "Images (*.png *.jpg *.jpeg);;All files (*)");
        if(fn.isEmpty()) return;
        Gray img=load_image(fn.toStdString());
        if(!img.ok()){ QMessageBox::warning(this,"Error","Could not read image."); return; }
        zbar::ImageScanner sc; sc.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
        zbar::Image zi(img.w, img.h, "Y800", img.px.data(), img.px.size());
        if (sc.scan(zi) <= 0) { QMessageBox::information(this,"Result","No barcodes found"); return; }
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"INSERT INTO barcodes VALUES (?)",-1,&st,nullptr);
        for(auto s=zi.symbol_begin(); s!=zi.symbol_end(); ++s){
            std::string d=normalize(s->get_data());
            auto* it=new QTreeWidgetItem(tree_); it->setText(0,QString::fromStdString(d));
            sqlite3_bind_text(st,1,d.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(st); sqlite3_reset(st);
        }
        sqlite3_finalize(st);
    }
    void do_remove() {
        auto items=tree_->selectedItems();
        if(items.isEmpty()){ QMessageBox::information(this,"Info","No item selected"); return; }
        for(auto* it:items){ std::string d=normalize(it->text(0).toStdString());
            sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"DELETE FROM barcodes WHERE data=?",-1,&st,nullptr);
            sqlite3_bind_text(st,1,d.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(st); sqlite3_finalize(st);
            delete it; }
    }
    void load() {
        tree_->clear();
        sqlite3_stmt* st=nullptr; sqlite3_prepare_v2(db_,"SELECT data FROM barcodes",-1,&st,nullptr);
        while(sqlite3_step(st)==SQLITE_ROW){ auto* it=new QTreeWidgetItem(tree_); it->setText(0,reinterpret_cast<const char*>(sqlite3_column_text(st,0))); }
        sqlite3_finalize(st);
    }
    sqlite3* db_=nullptr; QTreeWidget* tree_=nullptr;
};

int main(int argc, char** argv){ QApplication app(argc,argv); BarcodeSearch w; w.show(); return app.exec(); }
