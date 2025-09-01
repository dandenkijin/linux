use kconfig::Kconfig;
use std::env;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // When running `cargo run` from within the `kconfig-test` directory,
    // the current working directory is `linux/kconfig-test`.
    // The main Kconfig file is in the parent directory.
    let kconfig_path = "../Kconfig";

    println!("Attempting to parse '{}'...", kconfig_path);

    // This is the main call that will trigger the build and parsing.
    let kconfig = Kconfig::from_file(kconfig_path)?;

    println!("Successfully parsed Kconfig file!");
    println!("Found {} top-level items.", kconfig.items.len());

    Ok(())
}
