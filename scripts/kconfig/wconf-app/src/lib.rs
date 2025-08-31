// SPDX-License-Identifier: GPL-2.0
// Kconfig parsing library for the Tauri-based kernel configuration tool

pub mod kconfig;

pub use kconfig::{KconfigMenu, KconfigNode, KconfigOption, KconfigParser};
