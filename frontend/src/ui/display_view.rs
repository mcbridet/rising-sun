//! Display view Qt component for rendering the SunPCi framebuffer.
//!
//! This provides a QObject that manages framebuffer mmap and updates.
//! The actual rendering is done via QML Image + ImageProvider.

use std::cell::RefCell;
use std::ptr;

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(i32, source_width)]
        #[qproperty(i32, source_height)]
        #[qproperty(i32, color_depth)]
        #[qproperty(i32, pixel_format)]
        #[qproperty(i32, stride)]
        #[qproperty(i64, buffer_size)]
        #[qproperty(i32, driver_fd)]
        #[qproperty(bool, text_mode)]
        #[qproperty(bool, maintain_aspect)]
        #[qproperty(bool, integer_scaling)]
        #[qproperty(bool, framebuffer_ready)]
        type DisplayView = super::DisplayViewRust;

        /// Initialize the mmap for the framebuffer
        #[qinvokable]
        fn init_framebuffer(self: Pin<&mut DisplayView>) -> bool;

        /// Release the mmap
        #[qinvokable]
        fn release_framebuffer(self: Pin<&mut DisplayView>);

        /// Check if framebuffer is mapped
        #[qinvokable]
        fn is_mapped(self: &DisplayView) -> bool;
    }
}

use std::pin::Pin;

/// Framebuffer mapping information
struct FramebufferMapping {
    /// Pointer to mapped memory
    ptr: *mut u8,
    /// Size of mapping
    size: usize,
}

impl Drop for FramebufferMapping {
    fn drop(&mut self) {
        if !self.ptr.is_null() && self.size > 0 {
            unsafe {
                libc::munmap(self.ptr as *mut libc::c_void, self.size);
            }
        }
    }
}

/// Rust implementation of the DisplayView
pub struct DisplayViewRust {
    /// Source framebuffer width
    source_width: i32,
    /// Source framebuffer height
    source_height: i32,
    /// Color depth in bits
    color_depth: i32,
    /// Pixel format (from ioctl)
    pixel_format: i32,
    /// Bytes per row
    stride: i32,
    /// Total buffer size
    buffer_size: i64,
    /// Driver file descriptor for mmap
    driver_fd: i32,
    /// Whether in text mode
    text_mode: bool,
    /// Maintain aspect ratio when scaling
    maintain_aspect: bool,
    /// Use integer scaling only
    integer_scaling: bool,
    /// Whether framebuffer is ready
    framebuffer_ready: bool,
    /// Framebuffer mapping
    mapping: RefCell<Option<FramebufferMapping>>,
}

impl Default for DisplayViewRust {
    fn default() -> Self {
        Self {
            source_width: 640,
            source_height: 480,
            color_depth: 8,
            pixel_format: 0,
            stride: 640,
            buffer_size: 0,
            driver_fd: -1,
            text_mode: true,
            maintain_aspect: true,
            integer_scaling: false,
            framebuffer_ready: false,
            mapping: RefCell::new(None),
        }
    }
}

impl qobject::DisplayView {
    /// Initialize the framebuffer mmap
    pub fn init_framebuffer(self: Pin<&mut Self>) -> bool {
        let fd = *self.as_ref().driver_fd();
        let size = *self.as_ref().buffer_size() as usize;

        if fd < 0 || size == 0 {
            return false;
        }

        // Release any existing mapping
        *self.mapping.borrow_mut() = None;

        // mmap the framebuffer
        // The kernel driver exposes the framebuffer at offset 0 for mmap
        let ptr = unsafe {
            libc::mmap(
                ptr::null_mut(),
                size,
                libc::PROT_READ,
                libc::MAP_SHARED,
                fd,
                0, // offset 0 = framebuffer
            )
        };

        if ptr == libc::MAP_FAILED {
            return false;
        }

        *self.mapping.borrow_mut() = Some(FramebufferMapping {
            ptr: ptr as *mut u8,
            size,
        });

        self.set_framebuffer_ready(true);
        true
    }

    /// Release the framebuffer mmap
    pub fn release_framebuffer(self: Pin<&mut Self>) {
        *self.mapping.borrow_mut() = None;
        self.set_framebuffer_ready(false);
    }

    /// Check if the framebuffer is mapped
    pub fn is_mapped(&self) -> bool {
        self.mapping.borrow().is_some()
    }
}
