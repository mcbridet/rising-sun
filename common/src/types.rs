//! Common types used throughout the application.

use thiserror::Error;

/// Errors that can occur in the rising-sun system
#[derive(Debug, Error)]
pub enum SunPciError {
    #[error("Driver not loaded")]
    DriverNotLoaded,

    #[error("Permission denied: {0}")]
    PermissionDenied(String),

    #[error("Device not found: {0}")]
    DeviceNotFound(String),

    #[error("Session already running")]
    AlreadyRunning,

    #[error("Session not running")]
    NotRunning,

    #[error("Invalid configuration: {0}")]
    InvalidConfig(String),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("ioctl error: {0}")]
    Ioctl(#[from] nix::Error),
}
