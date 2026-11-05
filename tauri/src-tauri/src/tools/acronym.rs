use serde::Serialize;
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tauri::State;

/// Shared state: the acronym dictionary plus keyboard-hook control, managed by
/// Tauri and shared with the background hook thread.
pub struct Shared {
    pub dict: Arc<Mutex<BTreeMap<String, String>>>,
    pub prefs_path: PathBuf,
    pub hook_enabled: Arc<AtomicBool>,
    pub hook_started: Mutex<bool>,
}

#[derive(Serialize)]
pub struct Entry { abbreviation: String, explanation: String }

fn load_dict(path: &PathBuf) -> BTreeMap<String, String> {
    let mut map = BTreeMap::new();
    if let Ok(txt) = std::fs::read_to_string(path) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&txt) {
            if let Some(arr) = v.get("acronyms").and_then(|a| a.as_array()) {
                for item in arr {
                    let a = item.get("abbreviation").and_then(|x| x.as_str()).unwrap_or("");
                    let e = item.get("explanation").and_then(|x| x.as_str()).unwrap_or("");
                    if !a.is_empty() { map.insert(a.to_lowercase(), e.to_string()); }
                }
            }
        }
    }
    map
}

fn save_dict(path: &PathBuf, dict: &BTreeMap<String, String>) {
    let arr: Vec<_> = dict.iter()
        .map(|(a, e)| serde_json::json!({"abbreviation": a, "explanation": e}))
        .collect();
    let v = serde_json::json!({ "acronyms": arr });
    if let Ok(txt) = serde_json::to_string_pretty(&v) {
        let _ = std::fs::write(path, txt);
    }
}

pub fn build_shared(data_dir: &PathBuf) -> Shared {
    let prefs_path = data_dir.join("preferences_acronym.json");
    let dict = load_dict(&prefs_path);
    Shared {
        dict: Arc::new(Mutex::new(dict)),
        prefs_path,
        hook_enabled: Arc::new(AtomicBool::new(false)),
        hook_started: Mutex::new(false),
    }
}

#[tauri::command]
pub fn acronym_list(state: State<Shared>) -> Vec<Entry> {
    state.dict.lock().unwrap().iter()
        .map(|(a, e)| Entry { abbreviation: a.clone(), explanation: e.clone() })
        .collect()
}

#[tauri::command]
pub fn acronym_add(state: State<Shared>, abbreviation: String, explanation: String) {
    if abbreviation.is_empty() { return; }
    {
        let mut d = state.dict.lock().unwrap();
        d.insert(abbreviation.to_lowercase(), explanation);
        save_dict(&state.prefs_path, &d);
    }
}

#[tauri::command]
pub fn acronym_remove(state: State<Shared>, abbreviation: String) {
    let mut d = state.dict.lock().unwrap();
    d.remove(&abbreviation.to_lowercase());
    save_dict(&state.prefs_path, &d);
}

#[tauri::command]
pub fn acronym_expand_text(state: State<Shared>, text: String) -> String {
    let dict = state.dict.lock().unwrap().clone();
    let mut out = text;
    for (abbr, exp) in &dict {
        if abbr.is_empty() { continue; }
        if let Ok(re) = regex::RegexBuilder::new(&format!(r"\b{}\b", regex::escape(abbr)))
            .case_insensitive(true).build()
        {
            out = re.replace_all(&out, exp.as_str()).into_owned();
        }
    }
    out
}

#[tauri::command]
pub fn acronym_hook_status(state: State<Shared>) -> bool {
    state.hook_enabled.load(Ordering::Relaxed)
}

#[tauri::command]
pub fn acronym_hook_stop(state: State<Shared>) {
    state.hook_enabled.store(false, Ordering::Relaxed);
}

/// Start the global keyboard hook. Spawns the listener thread once; later
/// calls just re-enable it. Returns true if the hook is active.
#[tauri::command]
pub fn acronym_hook_start(state: State<Shared>) -> bool {
    state.hook_enabled.store(true, Ordering::Relaxed);
    let mut started = state.hook_started.lock().unwrap();
    if !*started {
        *started = true;
        let dict = state.dict.clone();
        let enabled = state.hook_enabled.clone();
        std::thread::spawn(move || run_hook(dict, enabled));
    }
    true
}

#[cfg(target_os = "linux")]
fn run_hook(dict: Arc<Mutex<BTreeMap<String, String>>>, enabled: Arc<AtomicBool>) {
    use enigo::{Direction, Enigo, Key, Keyboard, Settings};
    use rdev::{listen, EventType, Key as RKey};

    let mut enigo = match Enigo::new(&Settings::default()) {
        Ok(e) => e,
        Err(_) => return,
    };
    let mut buffer = String::new();
    let injecting = Arc::new(AtomicBool::new(false));

    let inj = injecting.clone();
    let _ = listen(move |event| {
        if !enabled.load(Ordering::Relaxed) { buffer.clear(); return; }
        if inj.load(Ordering::Relaxed) { return; } // ignore our own injected keys
        if let EventType::KeyPress(key) = event.event_type {
            match key {
                RKey::Space | RKey::Return | RKey::Tab => { buffer.clear(); }
                RKey::Backspace => { buffer.pop(); }
                _ => {
                    if let Some(name) = event.name {
                        if !name.is_empty() && name.chars().all(|c| !c.is_control()) {
                            buffer.push_str(&name);
                            let key_l = buffer.to_lowercase();
                            let expansion = dict.lock().unwrap().get(&key_l).cloned();
                            if let Some(exp) = expansion {
                                if !exp.is_empty() {
                                    inj.store(true, Ordering::Relaxed);
                                    let count = buffer.chars().count();
                                    for _ in 0..count {
                                        let _ = enigo.key(Key::Backspace, Direction::Click);
                                    }
                                    let _ = enigo.text(&exp);
                                    inj.store(false, Ordering::Relaxed);
                                }
                                buffer.clear();
                            }
                        }
                    }
                }
            }
        }
    });
}

#[cfg(not(target_os = "linux"))]
fn run_hook(dict: Arc<Mutex<BTreeMap<String, String>>>, enabled: Arc<AtomicBool>) {
    // rdev/enigo also support Windows and macOS; the Linux implementation above
    // is the reference. Kept as a no-op placeholder on other targets here.
    let _ = (dict, enabled);
}
