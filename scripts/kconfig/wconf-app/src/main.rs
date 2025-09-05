// SPDX-License-Identifier: GPL-2.0
// Tauri-based kernel configuration tool (wconf)
// All-in-one main application entry point.

// --- Merged kconfig.rs ---
mod kconfig_parser {
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

        pub fn parse_config_file(&mut self, path: &str) -> Result<(), String> {
            println!("[PARSER] Loading config from: {}", path);
            
            // Clear existing options
            self.options.clear();
            self.tree = KconfigTree { nodes: Vec::new() };
            
            // Load the .config file
            let config_path = Path::new(path);
            if !config_path.exists() {
                return Err(format!("Config file not found: {}", path));
            }
            
            self.load_config(config_path)
                .map_err(|e| format!("Failed to load config: {}", e))?;
                
            println!("[PARSER] Successfully loaded {} options from config", self.options.len());
            
            // Convert options to tree nodes
            self.tree.nodes = self.options.values()
                .map(|opt| KconfigNode::Config(opt.clone()))
                .collect();
            Ok(())
        }

        // Convert options to tree nodes - simplified for .config files
        fn options_to_nodes(&self) -> Vec<KconfigNode> {
            self.options
                .values()
                .map(|opt| KconfigNode::Config(opt.clone()))
                .collect()
        }

        pub fn get_tree(&self) -> Vec<KconfigNode> {
            // Always return fresh nodes from current options
            self.options_to_nodes()
        }
        
        fn populate_options(&mut self, nodes: &[KconfigNode]) {
            for node in nodes {
                if let KconfigNode::Config(option) = node {
                    self.options.insert(option.name.clone(), option.clone());
                }
            }
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
            let mut current_comment = String::new();
            
            for line in content.lines() {
                let line = line.trim();
                
                // Save comments for the next config option
                if line.starts_with('#') {
                    if !current_comment.is_empty() {
                        current_comment.push('\n');
                    }
                    current_comment.push_str(line.trim_start_matches('#'));
                    continue;
                }
                
                // Skip empty lines
                if line.is_empty() {
                    continue;
                }
                
                // Handle CONFIG_* options
                if let Some(stripped) = line.strip_prefix("CONFIG_") {
                    if let Some((key, value)) = stripped.split_once('=') {
                        // Determine the type based on the value
                        let (value_type, clean_value) = if value.starts_with('"') && value.ends_with('"') {
                            ("string", value.trim_matches('"').to_string())
                        } else if value == "y" || value == "m" || value == "n" {
                            ("tristate", value.to_string())
                        } else if value.parse::<i64>().is_ok() {
                            ("int", value.to_string())
                        } else if value.starts_with("0x") && i64::from_str_radix(&value[2..], 16).is_ok() {
                            ("hex", value.to_string())
                        } else {
                            ("string", value.to_string())
                        };
                        
                        let option = KconfigOption {
                            name: key.to_string(),
                            prompt: key.to_string(),
                            r#type: value_type.to_string(),
                            default: None,
                            depends: None,
                            select: None,
                            optional: false,
                            range: None,
                            help: current_comment.trim().to_string(),
                            value: clean_value,
                        };
                        
                        self.options.insert(key.to_string(), option);
                        current_comment.clear();
                    }
                } 
                // Handle disabled options (# CONFIG_FOO is not set)
                else if let Some(stripped) = line.strip_prefix("# CONFIG_") {
                    if let Some((key, _)) = stripped.split_once(" is not set") {
                        let option = KconfigOption {
                            name: key.to_string(),
                            prompt: key.to_string(),
                            r#type: "bool".to_string(),
                            default: None,
                            depends: None,
                            select: None,
                            optional: false,
                            range: None,
                            help: current_comment.trim().to_string(),
                            value: "n".to_string(),
                        };
                    
                        self.options.insert(key.to_string(), option);
                        current_comment.clear();
                    }
                }
            }
            
            Ok(())
        }

        pub fn get_config_content(&self) -> String {
            let mut content = String::new();
            content.push_str("#\n# Automatically generated by wconf\n#\n\n");
            
            // Group options by their help text to keep related options together
            let mut options_by_help: HashMap<String, Vec<&KconfigOption>> = HashMap::new();
            for option in self.options.values() {
                let help = option.help.trim();
                options_by_help.entry(help.to_string()).or_default().push(option);
            }
            
            // Sort the groups to maintain consistent output
            let mut sorted_groups: Vec<_> = options_by_help.into_iter().collect();
            sorted_groups.sort_by_key(|(_, opts)| opts[0].name.clone());
            
            // Generate the config content
            for (help, options) in sorted_groups {
                // Add the help text as a comment if it exists
                if !help.is_empty() {
                    for line in help.lines() {
                        content.push_str("# ");
                        content.push_str(line);
                        content.push('\n');
                    }
                }
                
                // Add the options
                for option in options {
                    let name = option.name.clone();
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
    config_path: PathBuf,
    parser: KconfigParser,
}

impl KconfigState {
    fn new(config_path: String) -> Self {
        Self {
            config_path: PathBuf::from(config_path),
            parser: KconfigParser::new(),
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
    // First, get the config path while holding the lock briefly
    let config_path = {
        let state_guard = state.lock().unwrap();
        state_guard.config_path.clone()
    };
    
    println!("[WRAPPER] Loading config from: {}", config_path.display());
    
    // Verify the config file exists before proceeding
    if !config_path.exists() {
        eprintln!("Config file not found: {}", config_path.display());
        return Err(CommandError {
            message: format!("Config file not found: {}", config_path.display()),
        });
    }
    
    // Now load the config with a new mutable borrow
    let mut state_guard = state.lock().unwrap();
    if let Err(e) = state_guard.parser.load_config(&config_path) {
        eprintln!("Failed to load config: {}", e);
        return Err(CommandError {
            message: format!("Failed to load config: {}", e),
        });
    }
    
    println!("[WRAPPER] Successfully loaded config from: {}", config_path.display());
    
    // Return the parsed config as nodes
    let tree = state_guard.parser.get_tree();
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
fn save_kconfig(
    state: tauri::State<'_, Arc<Mutex<KconfigState>>>,
) -> Result<(), CommandError> {
    let state_guard = state.lock().unwrap();
    let content = state_guard.parser.get_config_content();
    let config_path = &state_guard.config_path;
    
    println!("[WRAPPER] Saving config to: {}", config_path.display());
    
    // Create parent directories if they don't exist
    if let Some(parent) = config_path.parent() {
        if !parent.exists() {
            std::fs::create_dir_all(parent).map_err(|e| CommandError {
                message: format!("Failed to create config directory: {}", e),
            })?;
        }
    }
    
    // Create a backup of the existing config if it exists
    if config_path.exists() {
        let backup_path = config_path.with_extension("config.old");
        if let Err(e) = std::fs::copy(config_path, &backup_path) {
            eprintln!("Warning: Failed to create backup: {}", e);
        } else {
            println!("[WRAPPER] Created backup at: {}", backup_path.display());
        }
    }
    
    // Write the new config
    std::fs::write(config_path, content).map_err(|e| CommandError {
        message: format!("Failed to write config file: {}", e),
    })?;
    
    println!("[WRAPPER] Config saved successfully to: {}", config_path.display());
    
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

use tauri::utils::config::{Config, WindowConfig};
use std::path::PathBuf;

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
        
        // Get config file path from environment or use default .config
        let config_path = std::env::var("KCONFIG_CONFIG")
            .unwrap_or_else(|_| ".config".to_string());
        
        println!("[WRAPPER] Using config file: {}", config_path);
        
        let state = Arc::new(Mutex::new(KconfigState::new(config_path)));
        
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
                    // Load Tauri configuration
                    let config_path = std::env::current_exe()?
                        .parent()
                        .map(|p| p.join("tauri.conf.json"))
                        .unwrap_or_else(|| PathBuf::from("tauri.conf.json"));
                    
                    // Default window configuration
                    let mut window_config = WindowConfig {
                        width: Some(800),
                        height: Some(600),
                        resizable: Some(true),
                        decorations: Some(true),
                        center: Some(true),
                        ..Default::default()
                    };
                    
                    if config_path.exists() {
                        match std::fs::read_to_string(&config_path) {
                            Ok(config_str) => {
                                if let Ok(config) = serde_json::from_str::<serde_json::Value>(&config_str) {
                                    if let Some(windows) = config.get("windows").and_then(|w| w.as_array()) {
                                        if let Some(window) = windows.get(0) {
                                            if let Some(title) = window.get("title").and_then(|t| t.as_str()) {
                                                window_config.title = Some(title.to_string());
                                            }
                                            if let Some(width) = window.get("width").and_then(|w| w.as_i64()) {
                                                window_config.width = Some(width as f64);
                                            }
                                            if let Some(height) = window.get("height").and_then(|h| h.as_i64()) {
                                                window_config.height = Some(height as f64);
                                            }
                                            if let Some(resizable) = window.get("resizable").and_then(|r| r.as_bool()) {
                                                window_config.resizable = Some(resizable);
                                            }
                                            if let Some(decorations) = window.get("decorations").and_then(|d| d.as_bool()) {
                                                window_config.decorations = Some(decorations);
                                            }
                                            if let Some(center) = window.get("center").and_then(|c| c.as_bool()) {
                                                window_config.center = Some(center);
                                            }
                                        }
                                    }
                                } else {
                                    log::error!("Failed to parse tauri.conf.json");
                                }
                            }
                            Err(e) => {
                                log::error!("Failed to read tauri.conf.json: {}", e);
                            }
                        }
                    } else {
                        log::warn!("tauri.conf.json not found, using default configuration");
                    }

                    // Create window builder with configuration
                    let mut builder = tauri::WebviewWindowBuilder::new(
                        app,
                        WINDOW_LABEL,
                        tauri::WebviewUrl::App("index.html".into())
                    )
                    .title(window_config.title.as_deref().unwrap_or("Kernel Configuration"))
                    .resizable(window_config.resizable.unwrap_or(true))
                    .decorations(window_config.decorations.unwrap_or(true));
                    
                    // Apply window size
                    if let (Some(width), Some(height)) = (window_config.width, window_config.height) {
                        builder = builder.inner_size(width, height);
                    }
                    
                    // Center window if specified
                    if window_config.center.unwrap_or(true) {
                        builder = builder.center();
                    }
                    
                    let webview = match builder.build() {
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
    use super::kconfig_parser::{KconfigParser, KconfigOption};
    use std::fs;
    use std::collections::HashMap;

    #[test]
    fn test_parse_config_file() {
        let mut parser = KconfigParser::new();
        
        // Create a temporary .config file for testing
        let test_config = r#"
# This is a test config
CONFIG_TEST_BOOL=y
CONFIG_TEST_STRING="test value"
CONFIG_TEST_INT=42
# CONFIG_TEST_DISABLED is not set
CONFIG_TEST_HEX=0xDEADBEEF
"#;
        
        let test_file = "test.config";
        fs::write(test_file, test_config).expect("Failed to write test config");
        
        // Test parsing
        let result = parser.parse_config_file(test_file);
        assert!(result.is_ok(), "Failed to parse test config: {:?}", result.err());
        
        // Verify the parsed options
        assert_eq!(parser.options.len(), 4, "Should parse 4 options");
        
        // Clean up
        fs::remove_file(test_file).expect("Failed to remove test config");
    }
    
    #[test]
    fn test_get_config_content() {
        let mut parser = KconfigParser::new();
        
        // Add some test options
        parser.options.insert("TEST_BOOL".to_string(), KconfigOption {
            name: "TEST_BOOL".to_string(),
            prompt: "Test boolean".to_string(),
            r#type: "bool".to_string(),
            value: "y".to_string(),
            ..Default::default()
        });
        
        parser.options.insert("TEST_STRING".to_string(), KconfigOption {
            name: "TEST_STRING".to_string(),
            prompt: "Test string".to_string(),
            r#type: "string".to_string(),
            value: "test value".to_string(),
            ..Default::default()
        });
        
        let content = parser.get_config_content();
        
        // Verify the output format
        assert!(content.contains("CONFIG_TEST_BOOL=y"), "Should contain boolean option");
        assert!(content.contains("CONFIG_TEST_STRING=\"test value\""), "Should contain string option");
    }
}
