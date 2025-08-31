// SPDX-License-Identifier: GPL-2.0
// Tauri-based kernel configuration tool (wconf)
// Based on the kernel's Kconfig system

use serde::{Deserialize, Serialize};
use std::path::Path;
use std::sync::{Arc, Mutex};
use wconf_lib::{KconfigNode, KconfigOption, KconfigParser};

#[derive(Debug, Serialize, Deserialize)]
struct KconfigTree {
    items: Vec<KconfigNode>,
}

// Learn more about Tauri commands at https://tauri.app/v1/guides/features/command
#[tauri::command]
fn greet(name: &str) -> String {
    format!(
        "Hello, {}! You've successfully launched the kernel configuration tool.",
        name
    )
}

#[tauri::command]
fn load_kconfig_tree(kconfig_path: &str) -> Result<KconfigTree, String> {
    let kconfig_file = Path::new(kconfig_path);
    if !kconfig_file.exists() {
        return Err(format!("Kconfig file not found: {}", kconfig_path));
    }

    let mut parser = KconfigParser::new();
    parser.parse_file(kconfig_path)?;

    let items = parser.get_tree();

    Ok(KconfigTree { items })
}

#[tauri::command]
fn get_kconfig_option(
    parser_state: tauri::State<Arc<Mutex<KconfigParser>>>,
    name: &str,
) -> Result<KconfigOption, String> {
    let parser = parser_state.lock().unwrap();
    parser
        .get_option(name)
        .ok_or_else(|| format!("Option {} not found", name))
}

#[tauri::command]
fn set_kconfig_option(
    parser_state: tauri::State<Arc<Mutex<KconfigParser>>>,
    name: &str,
    value: &str,
) -> Result<(), String> {
    let mut parser = parser_state.lock().unwrap();
    if parser.set_option_value(name, value) {
        Ok(())
    } else {
        Err(format!("Option {} not found", name))
    }
}

fn main() {
    let parser_state = Arc::new(Mutex::new(KconfigParser::new()));

    tauri::Builder::default()
        .manage(parser_state)
        .invoke_handler(tauri::generate_handler![
            greet,
            load_kconfig_tree,
            get_kconfig_option,
            set_kconfig_option
        ])
        .run(tauri::generate_context!("./tauri.conf.json"))
        .expect("error while running tauri application");
}
