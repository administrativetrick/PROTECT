use crate::db;
use regex::RegexBuilder;
use serde::Serialize;
use tauri::AppHandle;

const PREFS: &str = "preferences_chatlog.json";

#[derive(Serialize)]
pub struct ResultRow { chat_log: String, result: String }

#[derive(Serialize)]
pub struct ScanOutcome { summary: String, saved_path: String, matched: bool }

fn load_keywords(app: &AppHandle) -> anyhow::Result<Vec<String>> {
    let p = db::data_dir(app)?.join(PREFS);
    if let Ok(txt) = std::fs::read_to_string(&p) {
        if let Ok(v) = serde_json::from_str::<Vec<String>>(&txt) {
            return Ok(v);
        }
    }
    Ok(vec!["keyword1".into(), "keyword2".into()])
}

#[tauri::command]
pub fn chatlog_get_keywords(app: AppHandle) -> Result<Vec<String>, String> {
    load_keywords(&app).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn chatlog_set_keywords(app: AppHandle, keywords: Vec<String>) -> Result<(), String> {
    let dir = db::data_dir(&app).map_err(|e| e.to_string())?;
    let json = serde_json::to_string(&keywords).map_err(|e| e.to_string())?;
    std::fs::write(dir.join(PREFS), json).map_err(|e| e.to_string())
}

fn scan_impl(app: &AppHandle, text: &str) -> anyhow::Result<ScanOutcome> {
    let keywords = load_keywords(app)?;
    // Precompile one escaped, case-insensitive, word-bounded pattern per keyword.
    let mut patterns = Vec::new();
    for kw in &keywords {
        if kw.is_empty() { continue; }
        let re = RegexBuilder::new(&format!(r"\b{}\b", regex::escape(kw)))
            .case_insensitive(true)
            .build()?;
        patterns.push((kw.clone(), re));
    }
    let mut summary = String::new();
    let mut matched = false;
    for (i, line) in text.lines().enumerate() {
        for (kw, re) in &patterns {
            if re.is_match(line) {
                matched = true;
                summary += &format!("Keyword '{}' found at line {}\n", kw, i + 1);
            }
        }
    }
    if !matched { summary = "Clean\n".into(); }

    let dir = db::data_dir(app)?;
    let chats = dir.join("chats");
    std::fs::create_dir_all(&chats)?;
    let mut idx = 1;
    let path = loop {
        let p = chats.join(format!("chat_{idx}.txt"));
        if !p.exists() { break p; }
        idx += 1;
    };
    std::fs::write(&path, text)?;

    let conn = db::open_db(app, "keyword_results.db")?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS results (id INTEGER PRIMARY KEY, chat_log TEXT, result TEXT)",
        [],
    )?;
    conn.execute(
        "INSERT INTO results (chat_log, result) VALUES (?1, ?2)",
        rusqlite::params![path.to_string_lossy(), summary],
    )?;
    Ok(ScanOutcome { summary, saved_path: path.to_string_lossy().into_owned(), matched })
}

#[tauri::command]
pub async fn chatlog_scan(app: AppHandle, text: String) -> Result<ScanOutcome, String> {
    tauri::async_runtime::spawn_blocking(move || scan_impl(&app, &text))
        .await
        .map_err(|e| e.to_string())?
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn chatlog_list(app: AppHandle) -> Result<Vec<ResultRow>, String> {
    let conn = db::open_db(&app, "keyword_results.db").map_err(|e| e.to_string())?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS results (id INTEGER PRIMARY KEY, chat_log TEXT, result TEXT)",
        [],
    ).map_err(|e| e.to_string())?;
    let mut stmt = conn.prepare("SELECT chat_log, result FROM results").map_err(|e| e.to_string())?;
    let rows = stmt.query_map([], |r| Ok(ResultRow { chat_log: r.get(0)?, result: r.get(1)? }))
        .map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for r in rows { out.push(r.map_err(|e| e.to_string())?); }
    Ok(out)
}

#[tauri::command]
pub fn chatlog_delete(app: AppHandle, chat_log: String, result: String) -> Result<(), String> {
    let conn = db::open_db(&app, "keyword_results.db").map_err(|e| e.to_string())?;
    conn.execute("DELETE FROM results WHERE chat_log = ?1 AND result = ?2",
        rusqlite::params![chat_log, result]).map_err(|e| e.to_string())?;
    Ok(())
}
