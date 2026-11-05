use crate::{db, net};
use serde::Serialize;
use tauri::AppHandle;

#[derive(Serialize, Clone)]
pub struct CloneRow { url: String, path: String }

fn sanitize(url: &str) -> String {
    let stripped = url.trim_start_matches("https://").trim_start_matches("http://");
    let mut s: String = stripped.chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' { c } else { '_' })
        .collect();
    if s.len() > 150 { s.truncate(150); }
    if s.is_empty() { s = "clone".into(); }
    s + ".html"
}

fn clone_impl(app: &AppHandle, url: &str) -> anyhow::Result<CloneRow> {
    let html = net::http_text(url)?;
    let dir = db::data_dir(app)?.join("WebsiteClones");
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(sanitize(url));
    std::fs::write(&path, html)?;
    let conn = db::open_db(app, "clone_data.db")?;
    conn.execute("CREATE TABLE IF NOT EXISTS clones (url TEXT PRIMARY KEY, path TEXT)", [])?;
    conn.execute("INSERT OR REPLACE INTO clones (url, path) VALUES (?1, ?2)",
        rusqlite::params![url, path.to_string_lossy()])?;
    Ok(CloneRow { url: url.to_string(), path: path.to_string_lossy().into_owned() })
}

#[tauri::command]
pub async fn clone_site(app: AppHandle, url: String) -> Result<CloneRow, String> {
    tauri::async_runtime::spawn_blocking(move || clone_impl(&app, &url))
        .await.map_err(|e| e.to_string())?.map_err(|e| e.to_string())
}

#[tauri::command]
pub fn clone_list(app: AppHandle) -> Result<Vec<CloneRow>, String> {
    let conn = db::open_db(&app, "clone_data.db").map_err(|e| e.to_string())?;
    conn.execute("CREATE TABLE IF NOT EXISTS clones (url TEXT PRIMARY KEY, path TEXT)", [])
        .map_err(|e| e.to_string())?;
    let mut stmt = conn.prepare("SELECT url, path FROM clones").map_err(|e| e.to_string())?;
    let rows = stmt.query_map([], |r| Ok(CloneRow { url: r.get(0)?, path: r.get(1)? }))
        .map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for r in rows { out.push(r.map_err(|e| e.to_string())?); }
    Ok(out)
}

#[tauri::command]
pub fn clone_open(path: String) -> Result<(), String> {
    open::that(&path).map_err(|e| e.to_string())
}
