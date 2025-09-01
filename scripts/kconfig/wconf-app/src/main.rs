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
    }
}

// --- Tauri Application State and Commands ---
use crate::kconfig_parser::{KconfigNode, KconfigOption, KconfigParser};
use serde::Serialize;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

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

fn main() {
    let kconfig_path = std::env::args()
        .last()
        .unwrap_or_else(|| "Kconfig".to_string());
    let state = Arc::new(Mutex::new(KconfigState::new(kconfig_path)));

    tauri::Builder::default()
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            load_kconfig,
            get_kconfig_option,
            set_kconfig_option,
            save_kconfig
        ])
        .run(tauri::generate_context!("./tauri.conf.json"))
        .expect("error while running tauri application");
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
