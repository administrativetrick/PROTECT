use anyhow::Result;
use rusqlite::Connection;
use std::path::PathBuf;
use tauri::{AppHandle, Manager};

pub fn data_dir(app: &AppHandle) -> Result<PathBuf> {
    let dir = app.path().app_data_dir()?;
    std::fs::create_dir_all(&dir)?;
    Ok(dir)
}

pub fn open_db(app: &AppHandle, name: &str) -> Result<Connection> {
    Ok(Connection::open(data_dir(app)?.join(name))?)
}
