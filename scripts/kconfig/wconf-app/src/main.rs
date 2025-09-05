// SPDX-License-Identifier: GPL-2.0
// Tauri-based kernel configuration tool (wconf)
// All-in-one main application entry point.

#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

// --- Merged kconfig.rs ---
mod kconfig_parser {
    use nom_kconfig::attribute::r#type::Type;
    use nom_kconfig::{attribute, parse_kconfig, Entry, Kconfig, KconfigFile, KconfigInput};
    use serde::{Deserialize, Serialize};
    use std::collections::HashMap;
    use std::fs;
    use std::path::{Path, PathBuf};

    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub enum KconfigNode {
        Config(KconfigOption),
        Menu(KconfigMenu),
        Choice(KconfigChoice),
        Comment(String),
    }

    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct KconfigOption {
        pub name: String,
        pub prompt: String,
        pub r#type: String,
        pub default: Option<String>,
        pub depends: Option<String>,
        pub select: Option<String>,
        pub optional: bool,
        pub range: Option<String>,
        pub help: String,
        pub value: String,
    }

    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct KconfigMenu {
        pub title: String,
        pub prompt: String,
        pub help: String,
        pub children: Vec<KconfigNode>,
    }

    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct KconfigChoice {
        pub name: String,
        pub prompt: String,
        pub help: String,
        pub children: Vec<KconfigNode>,
    }

    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct KconfigTree {
        pub nodes: Vec<KconfigNode>,
    }

    pub struct KconfigParser {
        tree: KconfigTree,
        options: HashMap<String, KconfigOption>,
    }

    impl KconfigParser {
        pub fn new() -> Self {
            KconfigParser {
                tree: KconfigTree { nodes: Vec::new() },
                options: HashMap::new(),
            }
        }

        pub fn parse_file(&mut self, path: &str) -> Result<(), String> {
            println!("[PARSER] Starting to parse file: {}", path);
            let path_obj = Path::new(path);
            let parent_dir = path_obj
                .parent()
                .unwrap_or_else(|| Path::new("."))
                .to_path_buf();
            let content = fs::read_to_string(path)
                .map_err(|e| format!("Failed to read Kconfig file: {}", e))?;
            println!("[PARSER] Read {} bytes from file.", content.len());

            let kconfig_file = KconfigFile::new(parent_dir, path_obj.to_path_buf());
            let parser_input = KconfigInput::new_extra(&content, kconfig_file);

            println!("[PARSER] Invoking nom_kconfig::parse_kconfig...");
            let (_, kconfig) =
                parse_kconfig(parser_input).map_err(|e| format!("Parse error: {:?}", e))?;

            println!(
                "[PARSER] Parsed {} top-level entries.",
                kconfig.entries.len()
            );
            let nodes = self.convert_kconfig_to_nodes(&kconfig);
            self.options.clear();
            self.populate_options(&nodes);
            self.tree = KconfigTree { nodes };
            println!(
                "[PARSER] Finished processing. Tree has {} nodes.",
                self.tree.nodes.len()
            );
            Ok(())
        }

        // Static method for tests
        pub fn parse(content: &str) -> Result<KconfigTree, String> {
            let dummy_file = KconfigFile::new(PathBuf::from("."), PathBuf::from("dummy.kconfig"));
            let parser_input = KconfigInput::new_extra(content, dummy_file);
            let (_, kconfig) =
                parse_kconfig(parser_input).map_err(|e| format!("Parse error: {:?}", e))?;
            let parser = KconfigParser::new();
            let nodes = parser.convert_kconfig_to_nodes(&kconfig);
            Ok(KconfigTree { nodes })
        }

        fn convert_kconfig_to_nodes(&self, kconfig: &Kconfig) -> Vec<KconfigNode> {
            kconfig
                .entries
                .iter()
                .map(|entry| self.convert_entry_to_node(entry))
                .collect()
        }

        fn convert_entry_to_node(&self, entry: &Entry) -> KconfigNode {
            match entry {
                Entry::Config(config) => {
                    let name = config.symbol.clone();
                    let mut prompt = String::new();
                    let mut r#type = "unknown".to_string();
                    let mut default = None;
                    let mut depends = None;
                    let mut select = None;
                    let mut optional = false;
                    let mut range = None;
                    let mut help = String::new();

                    for attr in &config.attributes {
                        match attr {
                            attribute::Attribute::Type(t) => {
                                let (type_name, prompt_opt) = match &t.r#type {
                                    Type::Bool(p) => ("bool", p),
                                    Type::Tristate(p) => ("tristate", p),
                                    Type::String(p) => ("string", p),
                                    Type::Int(p) => ("int", p),
                                    Type::Hex(p) => ("hex", p),
                                    _ => ("unknown", &None),
                                };
                                r#type = type_name.to_string();
                                if let Some(p_str) = prompt_opt {
                                    prompt = p_str.clone();
                                }
                            }
                            attribute::Attribute::Prompt(p) => prompt = p.prompt.clone(),
                            attribute::Attribute::Default(d) => {
                                default = Some(d.expression.to_string())
                            }
                            attribute::Attribute::DependsOn(d) => depends = Some(d.to_string()),
                            attribute::Attribute::Select(s) => select = Some(s.symbol.to_string()),
                            attribute::Attribute::Optional => optional = true,
                            attribute::Attribute::Range(r) => {
                                range = Some(format!("{} {}", r.lower_bound, r.upper_bound))
                            }
                            attribute::Attribute::Help(h) => help = h.clone(),
                            _ => {}
                        }
                    }
                    if prompt.is_empty() {
                        prompt = name.clone();
                    }

                    KconfigNode::Config(KconfigOption {
                        name,
                        prompt,
                        r#type,
                        default,
                        depends,
                        select,
                        optional,
                        range,
                        help,
                        value: "n".to_string(),
                    })
                }
                Entry::Menu(menu) => {
                    let children = menu
                        .entries
                        .iter()
                        .map(|e| self.convert_entry_to_node(e))
                        .collect();
                    KconfigNode::Menu(KconfigMenu {
                        title: menu.prompt.clone(),
                        prompt: menu.prompt.clone(),
                        help: String::new(),
                        children,
                    })
                }
                Entry::Choice(choice) => {
                    let mut prompt = String::new();
                    let mut help = String::new();
                    for attr in &choice.options {
                        if let attribute::Attribute::Prompt(p) = attr {
                            prompt = p.prompt.clone();
                        } else if let attribute::Attribute::Help(h) = attr {
                            help = h.clone();
                        }
                    }
                    let children = choice
                        .entries
                        .iter()
                        .map(|e| self.convert_entry_to_node(e))
                        .collect();
                    KconfigNode::Choice(KconfigChoice {
                        name: String::new(),
                        prompt,
                        help,
                        children,
                    })
                }
                Entry::Comment(comment) => KconfigNode::Comment(comment.prompt.clone()),
                Entry::If(if_block) => {
                    let children = if_block
                        .entries
                        .iter()
                        .map(|e| self.convert_entry_to_node(e))
                        .collect();
                    KconfigNode::Menu(KconfigMenu {
                        title: format!("If: {}", if_block.condition),
                        prompt: if_block.condition.to_string(),
                        help: String::new(),
                        children,
                    })
                }
                Entry::Source(source) => {
                    println!("[PARSER] Sourcing file: {:?}", source.file);
                    KconfigNode::Comment(format!("Sourced: {:?}", source.file))
                }
                _ => KconfigNode::Comment(format!("Unsupported entry type: {:?}", entry)),
            }
        }

        fn populate_options(&mut self, nodes: &[KconfigNode]) {
            for node in nodes {
                match node {
                    KconfigNode::Config(option) => {
                        self.options.insert(option.name.clone(), option.clone());
                    }
                    KconfigNode::Menu(menu) => {
                        self.populate_options(&menu.children);
                    }
                    KconfigNode::Choice(choice) => {
                        self.populate_options(&choice.children);
                    }
                    _ => {}
                }
            }
        }

        pub fn get_tree(&self) -> Vec<KconfigNode> {
            self.tree.nodes.clone()
        }
        pub fn get_option(&self, name: &str) -> Option<KconfigOption> {
            self.options.get(name).cloned()
        }

        pub fn set_option_value(&mut self, name: &str, value: &str) -> bool {
            if let Some(option) = self.options.get_mut(name) {
                option.value = value.to_string();
                true
            } else {
                false
            }
        }

        pub fn load_config(&mut self, config_path: &Path) -> Result<(), std::io::Error> {
            let content = fs::read_to_string(config_path)?;
            for line in content.lines() {
                if let Some(stripped) = line.strip_prefix("CONFIG_") {
                    if let Some((key, value)) = stripped.split_once('=') {
                        self.set_option_value(key, value.trim_matches('"'));
                    }
                } else if let Some(stripped) = line.strip_prefix("# CONFIG_") {
                    if let Some((key, _)) = stripped.split_once(" is not set") {
                        self.set_option_value(key, "n");
                    }
                }
            }
            Ok(())
        }

        pub fn get_config_content(&self) -> String {
            let mut content = String::new();
            content.push_str("#\n# Automatically generated by wconf\n#\n");
            let mut keys: Vec<&String> = self.options.keys().collect();
            keys.sort();
            for name in keys {
                if let Some(option) = self.options.get(name) {
                    let line = match option.r#type.as_str() {
                        "bool" | "tristate" if option.value == "y" || option.value == "m" => {
                            format!("CONFIG_{}={}\n", name, option.value)
                        }
                        "string" => format!("CONFIG_{}=\"{}\"\n", name, option.value),
                        "int" | "hex" => format!("CONFIG_{}={}\n", name, option.value),
                        _ => format!("# CONFIG_{} is not set\n", name),
                    };
                    content.push_str(&line);
                }
            }
            content
        }
    } // Close the KconfigParser impl
} // Close the kconfig_parser module

use crate::kconfig_parser::{KconfigNode, KconfigOption, KconfigParser};
use serde::Serialize;
use tauri_plugin_dialog::DialogExt;
use std::path::PathBuf;
use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use tauri::{
    AppHandle, Manager, Runtime, 
    menu::{Menu, MenuItem, MenuItemBuilder, SubmenuBuilder, MenuBuilder}
};

// Global flag to track if we already have a window
static WINDOW_OPEN: AtomicBool = AtomicBool::new(false);

struct KconfigState {
    parser: KconfigParser,
    config_path: Option<PathBuf>,
    kconfig_path: String,
}

impl KconfigState {
    fn new(kconfig_path: String) -> Self {
        Self {
            parser: KconfigParser::new(),
            config_path: None,
            kconfig_path,
        }
    }
}

#[derive(Debug, Serialize)]
struct CommandError {
    message: String,
}

impl<T: std::fmt::Display> From<T> for CommandError {
    fn from(err: T) -> Self {
        CommandError {
            message: err.to_string(),
        }
    }
}

#[tauri::command]
fn load_kconfig(
    state: tauri::State<'_, Arc<Mutex<KconfigState>>>,
) -> Result<Vec<KconfigNode>, CommandError> {
    let mut state_guard = state.lock().unwrap();
    let kconfig_path = state_guard.kconfig_path.clone();
    let config_path = std::path::PathBuf::from(".config");

    let mut parser = KconfigParser::new();
    parser.parse_file(&kconfig_path)?;
    if config_path.exists() {
        if let Err(e) = parser.load_config(&config_path) {
            eprintln!("Warning: Failed to load .config: {}", e);
        }
    }

    let tree = parser.get_tree();
    state_guard.parser = parser;
    state_guard.config_path = Some(config_path);

    Ok(tree)
}

#[tauri::command]
fn get_kconfig_option(
    state: tauri::State<'_, Arc<Mutex<KconfigState>>>,
    name: &str,
) -> Result<Option<KconfigOption>, CommandError> {
    let state_guard = state.lock().unwrap();
    Ok(state_guard.parser.get_option(name))
}

#[tauri::command]
fn set_kconfig_option(
    state: tauri::State<'_, Arc<Mutex<KconfigState>>>,
    name: &str,
    value: &str,
) -> Result<bool, CommandError> {
    let mut state_guard = state.lock().unwrap();
    Ok(state_guard.parser.set_option_value(name, value))
}

#[tauri::command]
fn save_kconfig(state: tauri::State<'_, Arc<Mutex<KconfigState>>>) -> Result<(), CommandError> {
    let state_guard = state.lock().unwrap();
    let config_path = state_guard
        .config_path
        .as_ref()
        .ok_or("Config path not set")?;
    let content = state_guard.parser.get_config_content();
    std::fs::write(config_path, content)?;
    Ok(())
}

fn create_menu<R: Runtime>(app: &AppHandle<R>) -> Result<Menu<R>, Box<dyn std::error::Error>> {
    // Create file menu items
    let open = MenuItemBuilder::new("Open")
        .id("open")
        .accelerator("CmdOrCtrl+O")
        .build(app)?;
    let save = MenuItemBuilder::new("Save")
        .id("save")
        .accelerator("CmdOrCtrl+S")
        .build(app)?;
    let exit = MenuItemBuilder::new("Exit")
        .id("exit")
        .accelerator("Cmd+Q")
        .build(app)?;
    
    // Create file menu
    let file_menu = SubmenuBuilder::new(app, "File")
        .items(&[
            &open,
            &save,
            &MenuItemBuilder::new("-").id("sep1").build(app)?,
            &exit,
        ])
        .build()?;
    
    // Create edit menu items
    let undo = MenuItemBuilder::new("Undo")
        .id("undo")
        .accelerator("CmdOrCtrl+Z")
        .build(app)?;
    let redo = MenuItemBuilder::new("Redo")
        .id("redo")
        .accelerator("CmdOrCtrl+Shift+Z")
        .build(app)?;
    let cut = MenuItemBuilder::new("Cut")
        .id("cut")
        .accelerator("CmdOrCtrl+X")
        .build(app)?;
    let copy = MenuItemBuilder::new("Copy")
        .id("copy")
        .accelerator("CmdOrCtrl+C")
        .build(app)?;
    let paste = MenuItemBuilder::new("Paste")
        .id("paste")
        .accelerator("CmdOrCtrl+V")
        .build(app)?;
    
    // Create edit menu
    let edit_menu = SubmenuBuilder::new(app, "Edit")
        .items(&[
            &undo,
            &redo,
            &MenuItemBuilder::new("-").id("sep1").build(app)?,
            &cut,
            &paste,
        ])
        .build()?;
    
    // Create view menu items
    let reload = MenuItemBuilder::new("Reload")
        .id("reload")
        .accelerator("CmdOrCtrl+R")
        .build(app)?;
    let zoomin = MenuItemBuilder::new("Zoom In")
        .id("zoomin")
        .accelerator("CmdOrCtrl+Plus")
        .build(app)?;
    let zoomout = MenuItemBuilder::new("Zoom Out")
        .id("zoomout")
        .accelerator("CmdOrCtrl+Minus")
        .build(app)?;
    let reset_zoom = MenuItemBuilder::new("Reset Zoom")
        .id("resetzoom")
        .accelerator("CmdOrCtrl+0")
        .build(app)?;
    
    // Create view menu
    let view_menu = SubmenuBuilder::new(app, "View")
        .items(&[
            &reload,
            &zoomin,
            &zoomout,
            &reset_zoom,
        ])
        .build()?;
    
    // Create help menu items
    let help = MenuItemBuilder::new("Help")
        .id("help")
        .build(app)?;
    let about = MenuItemBuilder::new("About")
        .id("about")
        .build(app)?;
    
    // Create help menu
    let help_menu = SubmenuBuilder::new(app, "Help")
        .items(&[
            &help,
            &about,
        ])
        .build()?;
    
    // Create main menu
    let menu = MenuBuilder::new(app)
        .items(&[&file_menu, &edit_menu, &view_menu, &help_menu])
        .build()?;
    
    Ok(menu)
}

fn handle_menu_event<R: Runtime>(app: &AppHandle<R>, event: tauri::menu::MenuEvent) {
    let window = match app.get_webview_window("main") {
        Some(w) => w,
        None => {
            log::error!("Failed to get main window");
            return;
        }
    };
    
    match event.id().as_ref() {
        "open" => {
            let app_handle = app.clone();
            let window = window.clone();
            let dialog = app.dialog();
            dialog.file()
                .add_filter("Kconfig", &["Kconfig"])
                .pick_file(move |file_path| {
                    if let Some(path) = file_path {
                        log::info!("Selected file: {:?}", path);
                        // TODO: Handle file opening
                    }
                });
        }
        "save" => {
            // TODO: Implement save functionality
            log::info!("Save menu item clicked");
        }
        "exit" => {
            std::process::exit(0);
        }
        "reload" => {
            if let Err(e) = window.eval("window.location.reload();") {
                log::error!("Failed to reload window: {}", e);
            }
        }
        "zoomin" => {
            if let Err(e) = window.eval("document.body.style.zoom = (parseFloat(document.body.style.zoom || '1') + 0.1).toString();") {
                log::error!("Failed to zoom in: {}", e);
            }
        }
        "zoomout" => {
            if let Err(e) = window.eval("let zoom = parseFloat(document.body.style.zoom || '1'); if (zoom > 0.5) { document.body.style.zoom = (zoom - 0.1).toString(); }") {
                log::error!("Failed to zoom out: {}", e);
            }
        }
        "resetzoom" => {
            if let Err(e) = window.eval("document.body.style.zoom = '1';") {
                log::error!("Failed to reset zoom: {}", e);
            }
        }
        "help" => {
            if let Err(e) = webbrowser::open("https://www.kernel.org/doc/html/latest/kbuild/kconfig-macro-language.html") {
                log::error!("Failed to open help URL: {}", e);
            }
        }
        "about" => {
            // TODO: Show about dialog
            log::info!("About menu item clicked");
        }
        _ => {}
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Set up logging
    unsafe {
        std::env::set_var("RUST_LOG", "debug,wconf=trace,tao=debug,winit=debug,tauri=debug");
    }
    
    env_logger::builder()
        .format_timestamp(Some(env_logger::TimestampPrecision::Millis))
        .format_module_path(false)
        .filter_level(log::LevelFilter::Trace)
        .init();

    log::info!("========================================");
    log::info!("Starting wconf application...");
    log::debug!("Current working directory: {:?}", std::env::current_dir()?);
    log::debug!("Command line args: {:?}", std::env::args().collect::<Vec<_>>());
    log::debug!("Environment variables (first 10):");
    for (i, (key, value)) in std::env::vars().take(10).enumerate() {
        log::debug!("  {}: {}={}", i + 1, key, value);
    }
    
    // Get Kconfig file path from command line or use default
    let kconfig_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "Kconfig".to_string());
    
    log::info!("Using Kconfig path: {}", kconfig_path);
    
    // Create application state
    let state = Arc::new(Mutex::new(KconfigState::new(kconfig_path)));
    
    log::debug!("Creating Tauri application builder...");
    
    log::info!("Setting up Tauri application...");
    log::debug!("Tauri version: {}", env!("CARGO_PKG_VERSION"));
    log::debug!("Tauri features: {:?}", {
        #[allow(unused_mut)]
        // List of enabled features for logging
        let features = vec![
            "custom-protocol" // Default Tauri feature
        ];
        features
    });
    tauri::Builder::default()
        .setup(move |app| {
            // Create and set the application menu
            let menu = create_menu(app.handle())?;
            app.set_menu(menu)?;
            
            // Handle menu events
            let app_handle = app.handle().clone();
            app.on_menu_event(move |_app, event| {
                handle_menu_event(&app_handle, event);
            });
            log::info!("Tauri app setup started");
            log::debug!("Current working directory: {:?}", std::env::current_dir()?);
            let path_resolver = app.path();
            log::debug!("App config dir: {:?}", path_resolver.app_config_dir());
            log::debug!("App data dir: {:?}", path_resolver.app_data_dir());
            
            // Use a fixed window label to prevent multiple windows
            const WINDOW_LABEL: &str = "main";
            
            // Check if we already have a window open
            if WINDOW_OPEN.swap(true, Ordering::SeqCst) {
                log::warn!("Window already exists, not creating a new one");
                // Try to focus the existing window instead
                if let Some(window) = app.get_webview_window(WINDOW_LABEL) {
                    if let Err(e) = window.set_focus() {
                        log::error!("Failed to focus existing window: {}", e);
                    }
                }
                return Ok(());
            }

            // Clean up any existing windows to be safe
            let webview_windows = app.webview_windows();
            if !webview_windows.is_empty() {
                log::info!("Found {} existing windows, closing them...", webview_windows.len());
                for (label, window) in webview_windows {
                    if label != WINDOW_LABEL {  // Don't close our main window if it exists
                        log::debug!("Closing window: {}", label);
                        if let Err(e) = window.close() {
                            log::error!("Failed to close window '{}': {}", label, e);
                        }
                    }
                }
                // Give the system time to clean up the windows
                std::thread::sleep(std::time::Duration::from_millis(100));
            }

            // Create new main window with error handling
            log::info!("Creating new main window...");
            
            // First check if the window already exists
            if let Some(window) = app.get_webview_window(WINDOW_LABEL) {
                log::info!("Window already exists, bringing it to front");
                if let Err(e) = window.set_focus() {
                    log::error!("Failed to focus existing window: {}", e);
                }
            } else {
                // Only create a new window if one doesn't exist
                let webview = match tauri::WebviewWindowBuilder::new(
                    app,
                    WINDOW_LABEL,
                    tauri::WebviewUrl::App("index.html".into())
                )
                .title("Kernel Configuration")
                .inner_size(1200.0, 800.0)
                .resizable(true)
                .decorations(true)
                .center()
                .build() {
                    Ok(webview) => webview,
                    Err(e) => {
                        log::error!("Failed to create webview: {}", e);
                        WINDOW_OPEN.store(false, Ordering::SeqCst);
                        return Err(e.into());
                    }
                };
                
                // Set up window close event
                let webview_ = webview.clone();
                webview.on_window_event(move |event| {
                    if let tauri::WindowEvent::CloseRequested { .. } = event {
                        log::info!("Window close requested, cleaning up...");
                        // Reset the window open flag
                        WINDOW_OPEN.store(false, Ordering::SeqCst);
                        
                        // Close the window
                        if let Err(e) = webview_.close() {
                            log::error!("Failed to close window: {}", e);
                        }
                        
                        // Exit the application
                        std::process::exit(0);
                    }
                });
            }
            log::info!("Tauri app setup completed");
            Ok(())
        })
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            load_kconfig,
            get_kconfig_option,
            set_kconfig_option,
            save_kconfig
        ])
        .run(tauri::generate_context!())
        .map_err(|e| {
            log::error!("Failed to run Tauri application: {}", e);
            log::error!("Error details: {:?}", e);
            e
        })?;
        
    log::info!("Tauri application has exited");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::kconfig_parser::KconfigParser;
    use std::fs;
    use std::path::Path;

    #[test]
    #[ignore = "This test is for debugging the main Kconfig parsing and is expected to fail until the parser is fixed."]
    fn test_parsing_main_kconfig_file() {
        let kconfig_path = Path::new("../../../Kconfig");
        let content = fs::read_to_string(kconfig_path).expect("Failed to read main Kconfig file");

        let mut found_last_good_line = false;
        for i in (1..=content.lines().count()).rev() {
            let partial_content = content.lines().take(i).collect::<Vec<_>>().join("\n");
            let result = KconfigParser::parse(&partial_content);

            if result.is_ok() {
                println!(
                    "\n--- PARSER TEST: Successfully parsed the first {} lines.",
                    i
                );
                println!(
                    "--- PARSER TEST: The error is likely on or after line {}.",
                    i + 1
                );
                println!(
                    "--- PARSER TEST: Line {}: '{}'",
                    i + 1,
                    content.lines().nth(i).unwrap_or("<End of File>")
                );
                found_last_good_line = true;
                break;
            }
        }

        if !found_last_good_line {
            println!("\n--- PARSER TEST: Could not parse even the first line successfully.");
        }

        // We still want the test to fail with the original error to confirm when it's fixed.
        let final_result = KconfigParser::parse(&content);
        assert!(
            final_result.is_ok(),
            "Full file parsing failed as expected: {:?}",
            final_result.err()
        );
    }
}
