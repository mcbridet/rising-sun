//! Common types and definitions shared between frontend and driver.

pub mod config;
pub mod config_storage;
pub mod driver;
pub mod ioctl;
pub mod scsi;
pub mod types;

pub use config::*;
pub use config_storage::*;
pub use driver::{is_driver_loaded, DriverHandle};
// Note: ioctl module is NOT re-exported via `pub use *` to avoid naming conflicts.
// Use `rising_sun_common::ioctl::*` directly for kernel interface types.
pub use types::*;
