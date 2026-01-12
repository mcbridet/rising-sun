//! Configuration file I/O operations.

use crate::config::AppConfig;
use std::fs;
use std::io;
use std::path::Path;

/// Error type for configuration operations
#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("Failed to read configuration file: {0}")]
    ReadError(#[from] io::Error),

    #[error("Failed to parse configuration: {0}")]
    ParseError(#[from] toml::de::Error),

    #[error("Failed to serialize configuration: {0}")]
    SerializeError(#[from] toml::ser::Error),
}

/// Load configuration from the default location
pub fn load_config() -> Result<AppConfig, ConfigError> {
    let config_file = AppConfig::config_file();
    load_config_from(&config_file)
}

/// Load configuration from a specific path
pub fn load_config_from(path: &Path) -> Result<AppConfig, ConfigError> {
    if !path.exists() {
        // Return default config if file doesn't exist
        return Ok(AppConfig::default());
    }

    let contents = fs::read_to_string(path)?;
    let config: AppConfig = toml::from_str(&contents)?;
    Ok(config)
}

/// Save configuration to the default location
pub fn save_config(config: &AppConfig) -> Result<(), ConfigError> {
    let config_file = AppConfig::config_file();
    save_config_to(config, &config_file)
}

/// Save configuration to a specific path
pub fn save_config_to(config: &AppConfig, path: &Path) -> Result<(), ConfigError> {
    // Ensure parent directory exists
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let contents = toml::to_string_pretty(config)?;
    fs::write(path, contents)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use tempfile::tempdir;

    #[test]
    fn test_default_config_roundtrip() {
        let dir = tempdir().unwrap();
        let config_path = dir.path().join("config.toml");

        let config = AppConfig::default();
        save_config_to(&config, &config_path).unwrap();

        let loaded = load_config_from(&config_path).unwrap();
        assert_eq!(loaded.general.auto_start, config.general.auto_start);
        assert_eq!(loaded.keyboard.layout, config.keyboard.layout);
    }

    #[test]
    fn test_load_nonexistent_returns_default() {
        let config = load_config_from(Path::new("/nonexistent/path/config.toml")).unwrap();
        assert!(!config.general.auto_start);
    }
}
