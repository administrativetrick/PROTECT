use crate::db;
use serde::Serialize;
use tauri::AppHandle;

#[derive(Serialize)]
pub struct Barcode { kind: String, data: String }

fn normalize(data: &str) -> String {
    if !data.is_empty() && data.chars().all(|c| c.is_ascii_digit()) && data.len() < 13 {
        format!("{:0>13}", data)
    } else {
        data.to_string()
    }
}

fn scan_impl(app: &AppHandle, path: &str) -> anyhow::Result<Vec<Barcode>> {
    let dynimg = image::open(path)?;
    let luma = dynimg.to_luma8();
    let (w, h) = (luma.width(), luma.height());
    let results = rxing::helpers::detect_multiple_in_luma(luma.into_raw(), w, h)
        .unwrap_or_default();
    let conn = db::open_db(app, "barcodes.db")?;
    conn.execute("CREATE TABLE IF NOT EXISTS barcodes (data TEXT)", [])?;
    let mut out = Vec::new();
    for r in results {
        let data = normalize(r.getText());
        let kind = format!("{:?}", r.getBarcodeFormat());
        conn.execute("INSERT INTO barcodes (data) VALUES (?1)", rusqlite::params![data])?;
        out.push(Barcode { kind, data });
    }
    Ok(out)
}

#[tauri::command]
pub async fn barcode_scan(app: AppHandle, path: String) -> Result<Vec<Barcode>, String> {
    tauri::async_runtime::spawn_blocking(move || scan_impl(&app, &path))
        .await.map_err(|e| e.to_string())?.map_err(|e| e.to_string())
}

#[tauri::command]
pub fn barcode_list(app: AppHandle) -> Result<Vec<String>, String> {
    let conn = db::open_db(&app, "barcodes.db").map_err(|e| e.to_string())?;
    conn.execute("CREATE TABLE IF NOT EXISTS barcodes (data TEXT)", [])
        .map_err(|e| e.to_string())?;
    let mut stmt = conn.prepare("SELECT data FROM barcodes").map_err(|e| e.to_string())?;
    let rows = stmt.query_map([], |r| r.get::<_, String>(0)).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for r in rows { out.push(r.map_err(|e| e.to_string())?); }
    Ok(out)
}

#[tauri::command]
pub fn barcode_delete(app: AppHandle, data: String) -> Result<(), String> {
    let conn = db::open_db(&app, "barcodes.db").map_err(|e| e.to_string())?;
    conn.execute("DELETE FROM barcodes WHERE data = ?1", rusqlite::params![normalize(&data)])
        .map_err(|e| e.to_string())?;
    Ok(())
}
