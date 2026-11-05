use crate::{db, net};
use serde::Serialize;
use std::io::Cursor;
use tauri::AppHandle;

#[derive(Serialize)]
pub struct MetaRow { url: String, key: String, value: String, notes: String }

fn dimensions(bytes: &[u8]) -> Option<(u32, u32, String)> {
    let reader = image::ImageReader::new(Cursor::new(bytes)).with_guessed_format().ok()?;
    let fmt = reader.format().map(|f| format!("{:?}", f)).unwrap_or_else(|| "image".into());
    let (w, h) = reader.into_dimensions().ok()?;
    Some((w, h, fmt))
}

fn exif_fields(bytes: &[u8]) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let reader = exif::Reader::new();
    let mut cur = Cursor::new(bytes);
    if let Ok(ex) = reader.read_from_container(&mut cur) {
        for f in ex.fields() {
            out.push((f.tag.to_string(), f.display_value().with_unit(&ex).to_string()));
        }
    }
    out
}

fn scrape_impl(app: &AppHandle, page: &str, notes: &str) -> anyhow::Result<usize> {
    let html = net::http_text(page)?;
    let conn = db::open_db(app, "image_metadata.db")?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, metadata_key TEXT, metadata_value TEXT, notes TEXT)",
        [],
    )?;
    let mut count = 0;
    for src in net::extract_imgs(&html) {
        let img_url = net::join_url(page, &src);
        let bytes = match net::http_bytes(&img_url) {
            Ok(b) => b,
            Err(_) => continue,
        };
        let mut rows: Vec<(String, String)> = Vec::new();
        if let Some((w, h, fmt)) = dimensions(&bytes) {
            rows.push(("Dimensions".into(), format!("{} {}x{}", fmt, w, h)));
        } else {
            rows.push(("Dimensions".into(), format!("unknown, {} bytes", bytes.len())));
        }
        for (k, v) in exif_fields(&bytes) {
            rows.push((format!("EXIF:{}", k), v));
        }
        for (k, v) in rows {
            conn.execute(
                "INSERT INTO images (url, metadata_key, metadata_value, notes) VALUES (?1, ?2, ?3, ?4)",
                rusqlite::params![img_url, k, v, notes],
            )?;
        }
        count += 1;
    }
    Ok(count)
}

#[tauri::command]
pub async fn imgmeta_scrape(app: AppHandle, url: String, notes: String) -> Result<usize, String> {
    tauri::async_runtime::spawn_blocking(move || scrape_impl(&app, &url, &notes))
        .await.map_err(|e| e.to_string())?.map_err(|e| e.to_string())
}

#[tauri::command]
pub fn imgmeta_list(app: AppHandle, filter: Option<String>) -> Result<Vec<MetaRow>, String> {
    let conn = db::open_db(&app, "image_metadata.db").map_err(|e| e.to_string())?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, metadata_key TEXT, metadata_value TEXT, notes TEXT)",
        [],
    ).map_err(|e| e.to_string())?;
    let collect = |stmt: &mut rusqlite::Statement, params: &[&dyn rusqlite::ToSql]| -> Result<Vec<MetaRow>, String> {
        let rows = stmt.query_map(params, |r| Ok(MetaRow {
            url: r.get(0)?, key: r.get(1)?, value: r.get(2)?, notes: r.get(3)?,
        })).map_err(|e| e.to_string())?;
        let mut out = Vec::new();
        for r in rows { out.push(r.map_err(|e| e.to_string())?); }
        Ok(out)
    };
    match filter {
        Some(f) if !f.is_empty() => {
            let like = format!("%{}%", f);
            let mut stmt = conn.prepare(
                "SELECT url, metadata_key, metadata_value, notes FROM images WHERE url LIKE ?1 OR metadata_key LIKE ?1"
            ).map_err(|e| e.to_string())?;
            collect(&mut stmt, &[&like])
        }
        _ => {
            let mut stmt = conn.prepare(
                "SELECT url, metadata_key, metadata_value, notes FROM images"
            ).map_err(|e| e.to_string())?;
            collect(&mut stmt, &[])
        }
    }
}

#[tauri::command]
pub fn imgmeta_edit_notes(app: AppHandle, url: String, notes: String) -> Result<(), String> {
    let conn = db::open_db(&app, "image_metadata.db").map_err(|e| e.to_string())?;
    conn.execute("UPDATE images SET notes = ?1 WHERE url = ?2",
        rusqlite::params![notes, url]).map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
pub fn imgmeta_delete(app: AppHandle, url: String) -> Result<(), String> {
    let conn = db::open_db(&app, "image_metadata.db").map_err(|e| e.to_string())?;
    conn.execute("DELETE FROM images WHERE url = ?1", rusqlite::params![url])
        .map_err(|e| e.to_string())?;
    Ok(())
}
