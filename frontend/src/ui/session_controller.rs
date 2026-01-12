//! Session controller Qt bridge for SunPCi session management.
//!
//! This wraps the DriverHandle from common to provide Qt/QML integration
//! for starting, stopping, and monitoring sessions.

use std::cell::RefCell;

use rising_sun_common::{
    is_driver_loaded, DriverHandle, load_config, ClipboardDirection,
    ioctl::{IoctlSessionConfig, FramebufferInfo, flags},
};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, driver_loaded)]
        #[qproperty(bool, session_running)]
        #[qproperty(bool, session_starting)]
        #[qproperty(bool, session_error)]
        #[qproperty(QString, error_message)]
        #[qproperty(i32, display_width)]
        #[qproperty(i32, display_height)]
        #[qproperty(i32, color_depth)]
        #[qproperty(bool, text_mode)]
        #[qproperty(QString, driver_version)]
        type SessionController = super::SessionControllerRust;

        /// Check if the SunPCi driver is loaded and accessible
        #[qinvokable]
        fn check_driver(self: Pin<&mut SessionController>);

        /// Start a session using the current configuration
        #[qinvokable]
        fn start_session(self: Pin<&mut SessionController>);

        /// Stop the running session
        #[qinvokable]
        fn stop_session(self: Pin<&mut SessionController>);

        /// Reset the session (Ctrl+Alt+Del equivalent)
        #[qinvokable]
        fn reset_session(self: Pin<&mut SessionController>);

        /// Get the file descriptor for the driver (for mmap in display view)
        #[qinvokable]
        fn get_driver_fd(self: &SessionController) -> i32;

        /// Poll for display mode changes and update properties
        #[qinvokable]
        fn poll_display(self: Pin<&mut SessionController>);

        /// Get framebuffer stride (bytes per row)
        #[qinvokable]
        fn get_framebuffer_stride(self: &SessionController) -> i32;

        /// Get framebuffer size in bytes
        #[qinvokable]
        fn get_framebuffer_size(self: &SessionController) -> i64;

        /// Get framebuffer pixel format (0=indexed8, 1=rgb565, 2=rgb888, 3=xrgb8888)
        #[qinvokable]
        fn get_framebuffer_format(self: &SessionController) -> i32;
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;

/// Rust implementation of the SessionController
pub struct SessionControllerRust {
    /// Whether the driver is loaded and accessible
    driver_loaded: bool,
    /// Whether a session is currently running
    session_running: bool,
    /// Whether session is in the process of starting
    session_starting: bool,
    /// Whether there was an error
    session_error: bool,
    /// Error message if any
    error_message: QString,
    /// Current display width
    display_width: i32,
    /// Current display height
    display_height: i32,
    /// Current color depth
    color_depth: i32,
    /// Whether in text mode (vs graphics mode)
    text_mode: bool,
    /// Driver version string (e.g., "1.0.0")
    driver_version: QString,
    /// Handle to the driver (None if not opened)
    handle: RefCell<Option<DriverHandle>>,
    /// Cached framebuffer info
    framebuffer: RefCell<Option<FramebufferInfo>>,
}

impl Default for SessionControllerRust {
    fn default() -> Self {
        Self {
            driver_loaded: false,
            session_running: false,
            session_starting: false,
            session_error: false,
            error_message: QString::default(),
            display_width: 640,
            display_height: 480,
            color_depth: 8,
            text_mode: true,
            driver_version: QString::from("Unknown"),
            handle: RefCell::new(None),
            framebuffer: RefCell::new(None),
        }
    }
}

impl qobject::SessionController {
    /// Check if the driver is loaded and try to open it
    pub fn check_driver(mut self: Pin<&mut Self>) {
        let loaded = is_driver_loaded();
        self.as_mut().set_driver_loaded(loaded);

        if loaded {
            // Try to open the driver
            match DriverHandle::open() {
                Ok(handle) => {
                    // Get driver version
                    if let Ok(version) = handle.get_version() {
                        let version_str = format!("{}.{}.{}", version.major, version.minor, version.patch);
                        self.as_mut().set_driver_version(QString::from(&version_str));
                    }
                    
                    // Check current status
                    if let Ok(status) = handle.get_status() {
                        let running = status.state == 2; // Running state
                        self.as_mut().set_session_running(running);
                    }
                    *self.handle.borrow_mut() = Some(handle);
                }
                Err(e) => {
                    self.as_mut().set_session_error(true);
                    self.set_error_message(QString::from(&format!("Failed to open driver: {}", e)));
                }
            }
        } else {
            self.as_mut().set_driver_version(QString::from("Not loaded"));
        }
    }

    /// Start a session with the current configuration
    pub fn start_session(mut self: Pin<&mut Self>) {
        self.as_mut().set_session_error(false);
        self.as_mut().set_error_message(QString::default());
        self.as_mut().set_session_starting(true);

        // Ensure driver is open
        if self.handle.borrow().is_none() {
            match DriverHandle::open() {
                Ok(handle) => {
                    *self.handle.borrow_mut() = Some(handle);
                }
                Err(e) => {
                    self.as_mut().set_session_error(true);
                    self.as_mut().set_error_message(QString::from(&format!("Failed to open driver: {}", e)));
                    self.set_session_starting(false);
                    return;
                }
            }
        }

        // Load configuration
        let config = load_config().unwrap_or_default();

        // Build ioctl config (memory is physical on SunPCi card, not configurable)
        let mut ioctl_config = IoctlSessionConfig::default();

        // Set flags based on config
        let mut session_flags = 0u32;
        if config.network.enabled {
            session_flags |= flags::NETWORK_ENABLED;
        }
        if config.clipboard.enabled {
            session_flags |= flags::CLIPBOARD_ENABLED;
            match config.clipboard.direction {
                ClipboardDirection::Bidirectional => {
                    session_flags |= flags::CLIPBOARD_TO_GUEST;
                    session_flags |= flags::CLIPBOARD_TO_HOST;
                }
                ClipboardDirection::HostToGuest => {
                    session_flags |= flags::CLIPBOARD_TO_GUEST;
                }
                ClipboardDirection::GuestToHost => {
                    session_flags |= flags::CLIPBOARD_TO_HOST;
                }
            }
        }
        ioctl_config.flags = session_flags;

        // Set disk paths
        if let Some(ref primary) = config.storage.primary_disk {
            IoctlSessionConfig::set_path(&mut ioctl_config.primary_disk, 
                &primary.path.to_string_lossy());
        }
        if let Some(ref secondary) = config.storage.secondary_disk {
            IoctlSessionConfig::set_path(&mut ioctl_config.secondary_disk,
                &secondary.path.to_string_lossy());
        }

        // Start the session
        let handle_ref = self.handle.borrow();
        if let Some(handle) = handle_ref.as_ref() {
            match handle.start_session(&ioctl_config) {
                Ok(()) => {
                    // Get initial framebuffer info
                    if let Ok(fb) = handle.get_framebuffer() {
                        drop(handle_ref);
                        *self.framebuffer.borrow_mut() = Some(fb);
                    } else {
                        drop(handle_ref);
                    }
                    self.as_mut().set_session_running(true);
                    self.set_session_starting(false);
                }
                Err(e) => {
                    drop(handle_ref);
                    self.as_mut().set_session_error(true);
                    self.as_mut().set_error_message(QString::from(&format!("Failed to start session: {}", e)));
                    self.set_session_starting(false);
                }
            }
        } else {
            drop(handle_ref);
            self.as_mut().set_session_error(true);
            self.as_mut().set_error_message(QString::from("Driver handle not available"));
            self.set_session_starting(false);
        }
    }

    /// Stop the running session
    pub fn stop_session(mut self: Pin<&mut Self>) {
        let handle_ref = self.handle.borrow();
        if let Some(handle) = handle_ref.as_ref() {
            match handle.stop_session() {
                Ok(()) => {
                    drop(handle_ref);
                    self.as_mut().set_session_running(false);
                    *self.framebuffer.borrow_mut() = None;
                }
                Err(e) => {
                    drop(handle_ref);
                    self.as_mut().set_session_error(true);
                    self.set_error_message(QString::from(&format!("Failed to stop session: {}", e)));
                }
            }
        }
    }

    /// Reset the session (warm reboot)
    pub fn reset_session(mut self: Pin<&mut Self>) {
        let handle_ref = self.handle.borrow();
        if let Some(handle) = handle_ref.as_ref() {
            if let Err(e) = handle.reset_session() {
                drop(handle_ref);
                self.as_mut().set_session_error(true);
                self.set_error_message(QString::from(&format!("Failed to reset session: {}", e)));
            }
        }
    }

    /// Get the driver file descriptor for mmap operations
    pub fn get_driver_fd(&self) -> i32 {
        self.handle
            .borrow()
            .as_ref()
            .map(|h| h.as_raw_fd())
            .unwrap_or(-1)
    }

    /// Poll display info and update properties
    pub fn poll_display(mut self: Pin<&mut Self>) {
        let handle_ref = self.handle.borrow();
        if let Some(handle) = handle_ref.as_ref() {
            if let Ok(info) = handle.get_display() {
                let width = info.width as i32;
                let height = info.height as i32;
                let depth = info.color_depth as i32;
                let text = info.mode == 0;
                
                // Update framebuffer info
                if let Ok(fb) = handle.get_framebuffer() {
                    drop(handle_ref);
                    *self.framebuffer.borrow_mut() = Some(fb);
                } else {
                    drop(handle_ref);
                }
                
                self.as_mut().set_display_width(width);
                self.as_mut().set_display_height(height);
                self.as_mut().set_color_depth(depth);
                self.set_text_mode(text);
            }
        }
    }

    /// Get framebuffer stride
    pub fn get_framebuffer_stride(&self) -> i32 {
        self.framebuffer
            .borrow()
            .map(|fb| fb.stride as i32)
            .unwrap_or(0)
    }

    /// Get framebuffer size
    pub fn get_framebuffer_size(&self) -> i64 {
        self.framebuffer
            .borrow()
            .map(|fb| fb.size() as i64)
            .unwrap_or(0)
    }

    /// Get framebuffer format
    pub fn get_framebuffer_format(&self) -> i32 {
        self.framebuffer
            .borrow()
            .map(|fb| fb.format as i32)
            .unwrap_or(0)
    }
}
