//! Framebuffer image provider for QML.
//!
//! This provides framebuffer access for rendering in QML.
//! The framebuffer data comes from the kernel driver via mmap.
//!
//! Note: These functions are prepared for future ImageProvider integration.

#![allow(dead_code)]

use std::os::unix::io::RawFd;
use std::ptr;
use std::sync::{Arc, Mutex};

/// Shared state for the framebuffer provider
pub struct FramebufferProviderState {
    /// Driver file descriptor
    pub driver_fd: RawFd,
    /// Framebuffer width
    pub width: u32,
    /// Framebuffer height
    pub height: u32,
    /// Bytes per row
    pub stride: u32,
    /// Pixel format
    pub format: u32,
    /// Buffer size
    pub size: usize,
    /// Mapped pointer (managed externally)
    pub mapped_ptr: Option<*const u8>,
}

impl Default for FramebufferProviderState {
    fn default() -> Self {
        Self {
            driver_fd: -1,
            width: 640,
            height: 480,
            stride: 640,
            format: 0,
            size: 0,
            mapped_ptr: None,
        }
    }
}

// Safety: The mapped_ptr is only accessed from the main thread
// and the Qt event loop serializes access
unsafe impl Send for FramebufferProviderState {}
unsafe impl Sync for FramebufferProviderState {}

/// Global state shared between SessionController and the image provider
pub static FRAMEBUFFER_STATE: std::sync::LazyLock<Arc<Mutex<FramebufferProviderState>>> =
    std::sync::LazyLock::new(|| Arc::new(Mutex::new(FramebufferProviderState::default())));

/// Update the framebuffer state (called from SessionController)
pub fn update_framebuffer_state(
    fd: RawFd,
    width: u32,
    height: u32,
    stride: u32,
    format: u32,
    size: usize,
) {
    if let Ok(mut state) = FRAMEBUFFER_STATE.lock() {
        // Unmap old if fd changed
        if state.driver_fd != fd {
            if let Some(ptr) = state.mapped_ptr.take() {
                if state.size > 0 {
                    unsafe {
                        libc::munmap(ptr as *mut libc::c_void, state.size);
                    }
                }
            }
        }

        state.driver_fd = fd;
        state.width = width;
        state.height = height;
        state.stride = stride;
        state.format = format;
        state.size = size;

        // Map new framebuffer if needed
        if fd >= 0 && size > 0 && state.mapped_ptr.is_none() {
            let ptr = unsafe {
                libc::mmap(
                    ptr::null_mut(),
                    size,
                    libc::PROT_READ,
                    libc::MAP_SHARED,
                    fd,
                    0,
                )
            };

            if ptr != libc::MAP_FAILED {
                state.mapped_ptr = Some(ptr as *const u8);
            }
        }
    }
}

/// Clear the framebuffer state (called when session stops)
pub fn clear_framebuffer_state() {
    if let Ok(mut state) = FRAMEBUFFER_STATE.lock() {
        if let Some(ptr) = state.mapped_ptr.take() {
            if state.size > 0 {
                unsafe {
                    libc::munmap(ptr as *mut libc::c_void, state.size);
                }
            }
        }
        *state = FramebufferProviderState::default();
    }
}

/// Get a snapshot of the current framebuffer as RGBA pixels
/// 
/// Returns (width, height, rgba_data) or None if not available
pub fn get_framebuffer_rgba() -> Option<(u32, u32, Vec<u8>)> {
    let state = FRAMEBUFFER_STATE.lock().ok()?;
    let ptr = state.mapped_ptr?;

    if state.width == 0 || state.height == 0 || state.size == 0 {
        return None;
    }

    let width = state.width;
    let height = state.height;
    let stride = state.stride as usize;
    let format = state.format;

    // Allocate RGBA output buffer
    let mut rgba = vec![0u8; (width * height * 4) as usize];

    unsafe {
        match format {
            0 => {
                // Indexed8 - TODO: Need palette from driver
                // For now, treat as grayscale
                for y in 0..height as usize {
                    let src_row = ptr.add(y * stride);
                    let dst_row = &mut rgba[y * width as usize * 4..];
                    for x in 0..width as usize {
                        let pixel = *src_row.add(x);
                        dst_row[x * 4] = pixel;     // R
                        dst_row[x * 4 + 1] = pixel; // G
                        dst_row[x * 4 + 2] = pixel; // B
                        dst_row[x * 4 + 3] = 255;   // A
                    }
                }
            }
            1 => {
                // RGB565
                for y in 0..height as usize {
                    let src_row = ptr.add(y * stride) as *const u16;
                    let dst_row = &mut rgba[y * width as usize * 4..];
                    for x in 0..width as usize {
                        let pixel = *src_row.add(x);
                        let r = ((pixel >> 11) & 0x1F) as u8;
                        let g = ((pixel >> 5) & 0x3F) as u8;
                        let b = (pixel & 0x1F) as u8;
                        dst_row[x * 4] = (r << 3) | (r >> 2);     // R
                        dst_row[x * 4 + 1] = (g << 2) | (g >> 4); // G
                        dst_row[x * 4 + 2] = (b << 3) | (b >> 2); // B
                        dst_row[x * 4 + 3] = 255;                 // A
                    }
                }
            }
            2 => {
                // RGB888
                for y in 0..height as usize {
                    let src_row = ptr.add(y * stride);
                    let dst_row = &mut rgba[y * width as usize * 4..];
                    for x in 0..width as usize {
                        dst_row[x * 4] = *src_row.add(x * 3 + 2);     // R (BGR order)
                        dst_row[x * 4 + 1] = *src_row.add(x * 3 + 1); // G
                        dst_row[x * 4 + 2] = *src_row.add(x * 3);     // B
                        dst_row[x * 4 + 3] = 255;                     // A
                    }
                }
            }
            3 => {
                // XRGB8888
                for y in 0..height as usize {
                    let src_row = ptr.add(y * stride) as *const u32;
                    let dst_row = &mut rgba[y * width as usize * 4..];
                    for x in 0..width as usize {
                        let pixel = *src_row.add(x);
                        dst_row[x * 4] = ((pixel >> 16) & 0xFF) as u8;     // R
                        dst_row[x * 4 + 1] = ((pixel >> 8) & 0xFF) as u8;  // G
                        dst_row[x * 4 + 2] = (pixel & 0xFF) as u8;         // B
                        dst_row[x * 4 + 3] = 255;                          // A
                    }
                }
            }
            _ => {
                // Unknown format - fill with magenta
                for pixel in rgba.chunks_mut(4) {
                    pixel[0] = 255;
                    pixel[1] = 0;
                    pixel[2] = 255;
                    pixel[3] = 255;
                }
            }
        }
    }

    Some((width, height, rgba))
}
