mod db;
mod net;
mod tools;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&dir)?;
            let shared = tools::acronym::build_shared(&dir);
            app.manage(shared);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            tools::chatlog::chatlog_get_keywords,
            tools::chatlog::chatlog_set_keywords,
            tools::chatlog::chatlog_scan,
            tools::chatlog::chatlog_list,
            tools::chatlog::chatlog_delete,
            tools::acronym::acronym_list,
            tools::acronym::acronym_add,
            tools::acronym::acronym_remove,
            tools::acronym::acronym_expand_text,
            tools::acronym::acronym_hook_start,
            tools::acronym::acronym_hook_stop,
            tools::acronym::acronym_hook_status,
            tools::links::links_scrape,
            tools::links::links_list,
            tools::links::links_delete_source,
            tools::links::links_delete_link,
            tools::clone::clone_site,
            tools::clone::clone_list,
            tools::clone::clone_open,
            tools::imgmeta::imgmeta_scrape,
            tools::imgmeta::imgmeta_list,
            tools::imgmeta::imgmeta_edit_notes,
            tools::imgmeta::imgmeta_delete,
            tools::barcode::barcode_scan,
            tools::barcode::barcode_list,
            tools::barcode::barcode_delete,
        ])
        .run(tauri::generate_context!())
        .expect("error while running PROTECT Suite");
}
