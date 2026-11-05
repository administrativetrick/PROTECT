use crate::{db, net};
use serde::Serialize;
use tauri::AppHandle;

#[derive(Serialize)]
pub struct LinkRow { source: String, link: String }

fn scrape_impl(app: &AppHandle, source: &str) -> anyhow::Result<usize> {
    let html = net::http_text(source)?;
    let hrefs = net::extract_hrefs(&html);
    let conn = db::open_db(app, "links.db")?;
    conn.execute("CREATE TABLE IF NOT EXISTS links (source TEXT, link TEXT)", [])?;
    let mut n = 0;
    for h in hrefs {
        let abs = net::join_url(source, &h);
        conn.execute("INSERT INTO links (source, link) VALUES (?1, ?2)",
            rusqlite::params![source, abs])?;
        n += 1;
    }
    Ok(n)
}

#[tauri::command]
pub async fn links_scrape(app: AppHandle, source: String) -> Result<usize, String> {
    tauri::async_runtime::spawn_blocking(move || scrape_impl(&app, &source))
        .await.map_err(|e| e.to_string())?.map_err(|e| e.to_string())
}

#[tauri::command]
pub fn links_list(app: AppHandle) -> Result<Vec<LinkRow>, String> {
    let conn = db::open_db(&app, "links.db").map_err(|e| e.to_string())?;
    conn.execute("CREATE TABLE IF NOT EXISTS links (source TEXT, link TEXT)", [])
        .map_err(|e| e.to_string())?;
    let mut stmt = conn.prepare("SELECT source, link FROM links ORDER BY source")
        .map_err(|e| e.to_string())?;
    let rows = stmt.query_map([], |r| Ok(LinkRow { source: r.get(0)?, link: r.get(1)? }))
        .map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for r in rows { out.push(r.map_err(|e| e.to_string())?); }
    Ok(out)
}

#[tauri::command]
pub fn links_delete_source(app: AppHandle, source: String) -> Result<(), String> {
    let conn = db::open_db(&app, "links.db").map_err(|e| e.to_string())?;
    conn.execute("DELETE FROM links WHERE source = ?1", rusqlite::params![source])
        .map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
pub fn links_delete_link(app: AppHandle, source: String, link: String) -> Result<(), String> {
    // Scoped to the given source (fixes the original's delete-everywhere bug).
    let conn = db::open_db(&app, "links.db").map_err(|e| e.to_string())?;
    conn.execute("DELETE FROM links WHERE source = ?1 AND link = ?2",
        rusqlite::params![source, link]).map_err(|e| e.to_string())?;
    Ok(())
}
