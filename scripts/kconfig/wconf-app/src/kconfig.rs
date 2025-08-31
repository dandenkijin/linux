// SPDX-License-Identifier: GPL-2.0
// Kconfig parser for the Tauri-based kernel configuration tool

use std::collections::HashMap;
use std::fs;

// AST node types for Kconfig
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub enum KconfigNode {
    Config(KconfigOption),
    Menu(KconfigMenu),
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct KconfigOption {
    pub name: String,
    pub prompt: String,
    pub r#type: String,
    pub default: Option<String>,
    pub depends: Vec<String>,
    pub help: String,
    pub value: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct KconfigMenu {
    pub title: String,
    pub prompt: String,
    pub help: String,
    pub children: Vec<KconfigNode>,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct KconfigTree {
    pub nodes: Vec<KconfigNode>,
}

#[derive(Debug)]
pub struct KconfigParser {
    options: HashMap<String, KconfigOption>,
    tree: KconfigTree,
    menu_stack: Vec<usize>, // Stack to track current menu positions
}

impl KconfigParser {
    pub fn new() -> Self {
        KconfigParser {
            options: HashMap::new(),
            tree: KconfigTree { nodes: Vec::new() },
            menu_stack: Vec::new(),
        }
    }

    pub fn parse_file(&mut self, path: &str) -> Result<(), String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("Failed to read Kconfig file {}: {}", path, e))?;

        self.parse_content(&content)
    }

    fn parse_content(&mut self, content: &str) -> Result<(), String> {
        let lines: Vec<&str> = content.lines().collect();
        let mut i = 0;

        while i < lines.len() {
            let line = lines[i].trim();

            if line.starts_with("config ") {
                if let Some(parsed_config) = self.parse_config_block(&lines[i..]) {
                    i += parsed_config.lines_parsed;
                    let config_option = parsed_config.data;
                    self.options
                        .insert(config_option.name.clone(), config_option.clone());

                    // Add to appropriate location in tree
                    let config_node = KconfigNode::Config(config_option);
                    self.add_node_to_tree(config_node);
                    continue;
                }
            } else if line.starts_with("menu ") {
                if let Some(menu_title) = self.parse_menu_declaration(line) {
                    let menu = KconfigMenu {
                        title: menu_title.clone(),
                        prompt: menu_title,
                        help: String::new(),
                        children: Vec::new(),
                    };

                    let menu_node = KconfigNode::Menu(menu);
                    self.add_menu_to_tree(menu_node);
                }
            } else if line == "endmenu" {
                self.menu_stack.pop();
            }

            i += 1;
        }

        Ok(())
    }

    fn parse_menu_declaration(&self, line: &str) -> Option<String> {
        line[5..]
            .trim()
            .strip_prefix('"')
            .and_then(|s| s.strip_suffix('"'))
            .map(|s| s.to_string())
    }

    fn parse_config_block(&self, lines: &[&str]) -> Option<ParsedConfig> {
        if lines.is_empty() || !lines[0].starts_with("config ") {
            return None;
        }

        let config_line = lines[0];
        let name = config_line[7..].trim().to_string(); // Skip "config "

        let mut option = KconfigOption {
            name: name.clone(),
            prompt: String::new(),
            r#type: String::new(),
            default: None,
            depends: Vec::new(),
            help: String::new(),
            value: "n".to_string(), // Default value
        };

        let mut lines_parsed = 1;

        // Parse the config block
        for (i, line) in lines[1..].iter().enumerate() {
            let line = line.trim();

            // End of config block
            if line.is_empty()
                || line.starts_with("config ")
                || line.starts_with("menu ")
                || line.starts_with("end")
            {
                lines_parsed = i + 1;
                break;
            }

            if line.starts_with("bool") || line.starts_with("tristate") {
                option.r#type = line.split_whitespace().next().unwrap_or("").to_string();
                if let Some(prompt) = line.find('"').and_then(|start| {
                    line[start + 1..]
                        .find('"')
                        .map(|end| line[start + 1..start + 1 + end].to_string())
                }) {
                    option.prompt = prompt;
                }
            } else if line.starts_with("string")
                || line.starts_with("hex")
                || line.starts_with("int")
            {
                option.r#type = line.split_whitespace().next().unwrap_or("").to_string();
                if let Some(prompt) = line.find('"').and_then(|start| {
                    line[start + 1..]
                        .find('"')
                        .map(|end| line[start + 1..start + 1 + end].to_string())
                }) {
                    option.prompt = prompt;
                }
            } else if line.starts_with("prompt ") {
                if let Some(prompt) = line.find('"').and_then(|start| {
                    line[start + 1..]
                        .find('"')
                        .map(|end| line[start + 1..start + 1 + end].to_string())
                }) {
                    option.prompt = prompt;
                }
            } else if line.starts_with("default ") {
                option.default = Some(line[8..].trim().to_string());
            } else if line.starts_with("depends on ") {
                let deps_str = line[11..].trim();
                // Simple dependency parsing (would need to be more sophisticated)
                option.depends = deps_str.split("&&").map(|s| s.trim().to_string()).collect();
            } else if line == "help" {
                let mut help_lines = Vec::new();
                let mut j = i + 2; // Start after the "help" line

                // Collect help lines (those with leading spaces)
                while j < lines.len() && (lines[j].starts_with(" ") || lines[j].starts_with("\t")) {
                    // Remove leading whitespace (either spaces or tabs)
                    let help_line = lines[j].trim_start();
                    if !help_line.is_empty() {
                        help_lines.push(help_line.to_string());
                    }
                    j += 1;
                }

                option.help = help_lines.join("\n");
                lines_parsed = j;
                return Some(ParsedConfig {
                    data: option,
                    lines_parsed,
                });
            }

            lines_parsed = i + 2;
        }

        // If no prompt was set, use the config name
        if option.prompt.is_empty() {
            option.prompt = name.clone();
        }

        Some(ParsedConfig {
            data: option,
            lines_parsed,
        })
    }

    // Add a node to the appropriate location in the tree
    fn add_node_to_tree(&mut self, node: KconfigNode) {
        if self.menu_stack.is_empty() {
            // Add to root level
            self.tree.nodes.push(node);
        } else {
            // Add to current menu
            self.add_node_to_current_menu(node);
        }
    }

    // Add a menu to the tree and update the menu stack
    fn add_menu_to_tree(&mut self, menu_node: KconfigNode) {
        if self.menu_stack.is_empty() {
            // Top-level menu
            let pos = self.tree.nodes.len();
            self.tree.nodes.push(menu_node);
            self.menu_stack.push(pos);
        } else {
            // Nested menu
            let pos = self.add_menu_to_current_menu(menu_node);
            self.menu_stack.push(pos);
        }
    }

    // Add a node to the current menu
    fn add_node_to_current_menu(&mut self, node: KconfigNode) {
        let menu_stack = self.menu_stack.clone();
        if let Some(menu) = self.get_menu_mut_by_path(&menu_stack) {
            menu.children.push(node);
        }
    }

    // Add a menu to the current menu and return its position
    fn add_menu_to_current_menu(&mut self, menu_node: KconfigNode) -> usize {
        let menu_stack = self.menu_stack.clone();
        if let Some(menu) = self.get_menu_mut_by_path(&menu_stack) {
            let pos = menu.children.len();
            menu.children.push(menu_node);
            pos
        } else {
            0
        }
    }

    // Get a mutable reference to a menu by path
    fn get_menu_mut_by_path(&mut self, path: &[usize]) -> Option<&mut KconfigMenu> {
        let mut current_nodes = &mut self.tree.nodes;

        for &index in path {
            if index < current_nodes.len() {
                match &mut current_nodes[index] {
                    KconfigNode::Menu(menu) => {
                        current_nodes = &mut menu.children;
                    }
                    _ => return None,
                }
            } else {
                return None;
            }
        }

        // This is a bit tricky - we need to return the parent menu
        // For simplicity, we'll return the last menu in the path
        if !path.is_empty() {
            let mut nodes = &mut self.tree.nodes;
            for &index in &path[..path.len() - 1] {
                if let KconfigNode::Menu(menu) = &mut nodes[index] {
                    nodes = &mut menu.children;
                } else {
                    return None;
                }
            }
            if let KconfigNode::Menu(menu) = &mut nodes[*path.last().unwrap()] {
                Some(menu)
            } else {
                None
            }
        } else {
            None
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
}

struct ParsedConfig {
    data: KconfigOption,
    lines_parsed: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simple_config_parsing() {
        let mut parser = KconfigParser::new();
        let content = r#"config CONFIG_EXPERT
    bool "Configure for advanced users"
    default n
    help
      This option allows to adjust settings for advanced users."#;

        assert!(parser.parse_content(content).is_ok());
        let options = parser.options.values().collect::<Vec<_>>();
        assert_eq!(options.len(), 1);

        let option = options[0];
        assert_eq!(option.name, "CONFIG_EXPERT");
        assert_eq!(option.prompt, "Configure for advanced users");
        assert_eq!(option.r#type, "bool");
        assert_eq!(option.default, Some("n".to_string()));
        assert!(option.help.contains("advanced users"));
    }
}
