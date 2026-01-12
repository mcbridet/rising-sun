//! Input controller Qt bridge for keyboard and mouse event handling.
//!
//! This module handles:
//! - Converting Qt key codes to PC XT scancodes
//! - Mouse movement and button tracking
//! - Input capture state management

use std::cell::RefCell;
use std::collections::HashSet;

use rising_sun_common::ioctl::{KeyEvent, MouseEvent, key_flags, mouse_buttons};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, keyboard_captured)]
        #[qproperty(bool, mouse_captured)]
        #[qproperty(i32, guest_width)]
        #[qproperty(i32, guest_height)]
        #[qproperty(i32, driver_fd)]
        type InputController = super::InputControllerRust;

        /// Set the driver file descriptor
        #[qinvokable]
        fn set_driver(self: Pin<&mut InputController>, fd: i32);

        /// Toggle keyboard capture mode
        #[qinvokable]
        fn toggle_keyboard_capture(self: Pin<&mut InputController>);

        /// Toggle mouse capture mode
        #[qinvokable]
        fn toggle_mouse_capture(self: Pin<&mut InputController>);

        /// Release all input capture
        #[qinvokable]
        fn release_capture(self: Pin<&mut InputController>);

        /// Handle a Qt key press event
        /// Returns true if the event was handled
        #[qinvokable]
        fn handle_key_press(self: Pin<&mut InputController>, qt_key: i32, modifiers: i32, native_scancode: i32) -> bool;

        /// Handle a Qt key release event
        /// Returns true if the event was handled
        #[qinvokable]
        fn handle_key_release(self: Pin<&mut InputController>, qt_key: i32, modifiers: i32, native_scancode: i32) -> bool;

        /// Handle mouse button press
        /// button: 1=left, 2=right, 4=middle
        #[qinvokable]
        fn handle_mouse_press(self: Pin<&mut InputController>, button: i32);

        /// Handle mouse button release
        #[qinvokable]
        fn handle_mouse_release(self: Pin<&mut InputController>, button: i32);

        /// Handle mouse movement (relative mode)
        #[qinvokable]
        fn handle_mouse_move(self: Pin<&mut InputController>, dx: i32, dy: i32);

        /// Handle mouse wheel
        #[qinvokable]
        fn handle_mouse_wheel(self: Pin<&mut InputController>, delta: i32);

        /// Check if Ctrl+Alt is currently pressed (for release combo)
        #[qinvokable]
        fn is_release_combo_pressed(self: &InputController) -> bool;

        /// Send Ctrl+Alt+Del to guest
        #[qinvokable]
        fn send_ctrl_alt_del(self: Pin<&mut InputController>);

        /// Send Ctrl+Alt+Backspace to guest
        #[qinvokable]
        fn send_ctrl_alt_backspace(self: Pin<&mut InputController>);
    }
}

use std::pin::Pin;

/// Rust implementation of the InputController
pub struct InputControllerRust {
    /// Whether keyboard input is captured
    keyboard_captured: bool,
    /// Whether mouse input is captured
    mouse_captured: bool,
    /// Guest display width for mouse scaling
    guest_width: i32,
    /// Guest display height for mouse scaling
    guest_height: i32,
    /// Driver file descriptor
    driver_fd: i32,
    /// Currently pressed keys (for tracking modifier state)
    pressed_keys: RefCell<HashSet<u32>>,
    /// Current mouse button state
    button_state: RefCell<u32>,
    /// Driver handle (created from fd)
    handle: RefCell<Option<std::os::unix::io::RawFd>>,
}

impl Default for InputControllerRust {
    fn default() -> Self {
        Self {
            keyboard_captured: false,
            mouse_captured: false,
            guest_width: 640,
            guest_height: 480,
            driver_fd: -1,
            pressed_keys: RefCell::new(HashSet::new()),
            button_state: RefCell::new(0),
            handle: RefCell::new(None),
        }
    }
}

impl qobject::InputController {
    /// Set the driver file descriptor
    pub fn set_driver(mut self: Pin<&mut Self>, fd: i32) {
        self.as_mut().set_driver_fd(fd);
        if fd >= 0 {
            *self.handle.borrow_mut() = Some(fd);
        } else {
            *self.handle.borrow_mut() = None;
        }
    }

    /// Toggle keyboard capture
    pub fn toggle_keyboard_capture(self: Pin<&mut Self>) {
        let current = *self.as_ref().keyboard_captured();
        self.set_keyboard_captured(!current);
    }

    /// Toggle mouse capture
    pub fn toggle_mouse_capture(self: Pin<&mut Self>) {
        let current = *self.as_ref().mouse_captured();
        self.set_mouse_captured(!current);
    }

    /// Release all capture
    pub fn release_capture(mut self: Pin<&mut Self>) {
        self.as_mut().set_keyboard_captured(false);
        self.set_mouse_captured(false);
    }

    /// Handle key press event
    pub fn handle_key_press(
        mut self: Pin<&mut Self>,
        qt_key: i32,
        modifiers: i32,
        native_scancode: i32,
    ) -> bool {
        // Check for release combo (Right Ctrl alone, or Ctrl+Alt)
        if self.check_release_combo(qt_key, modifiers) {
            self.as_mut().release_capture();
            return true;
        }

        // Only process if captured
        if !*self.as_ref().keyboard_captured() {
            return false;
        }

        // Convert to XT scancode
        let (scancode, extended) = qt_key_to_scancode(qt_key, native_scancode);
        if scancode == 0 {
            return false;
        }

        // Track pressed key
        self.pressed_keys.borrow_mut().insert(scancode);

        // Send to driver
        self.send_key_event(scancode, true, extended);
        true
    }

    /// Handle key release event
    pub fn handle_key_release(
        self: Pin<&mut Self>,
        qt_key: i32,
        _modifiers: i32,
        native_scancode: i32,
    ) -> bool {
        if !*self.as_ref().keyboard_captured() {
            return false;
        }

        let (scancode, extended) = qt_key_to_scancode(qt_key, native_scancode);
        if scancode == 0 {
            return false;
        }

        // Remove from pressed keys
        self.pressed_keys.borrow_mut().remove(&scancode);

        // Send to driver
        self.send_key_event(scancode, false, extended);
        true
    }

    /// Handle mouse button press
    pub fn handle_mouse_press(self: Pin<&mut Self>, button: i32) {
        if !*self.as_ref().mouse_captured() {
            return;
        }

        let mut state = self.button_state.borrow_mut();
        match button {
            1 => *state |= mouse_buttons::LEFT,   // Left
            2 => *state |= mouse_buttons::RIGHT,  // Right
            4 => *state |= mouse_buttons::MIDDLE, // Middle
            _ => {}
        }

        self.send_mouse_event(0, 0, 0);
    }

    /// Handle mouse button release
    pub fn handle_mouse_release(self: Pin<&mut Self>, button: i32) {
        if !*self.as_ref().mouse_captured() {
            return;
        }

        let mut state = self.button_state.borrow_mut();
        match button {
            1 => *state &= !mouse_buttons::LEFT,
            2 => *state &= !mouse_buttons::RIGHT,
            4 => *state &= !mouse_buttons::MIDDLE,
            _ => {}
        }

        self.send_mouse_event(0, 0, 0);
    }

    /// Handle mouse movement
    pub fn handle_mouse_move(self: Pin<&mut Self>, dx: i32, dy: i32) {
        if !*self.as_ref().mouse_captured() {
            return;
        }

        self.send_mouse_event(dx, dy, 0);
    }

    /// Handle mouse wheel
    pub fn handle_mouse_wheel(self: Pin<&mut Self>, delta: i32) {
        if !*self.as_ref().mouse_captured() {
            return;
        }

        // Convert wheel delta (Qt gives 120 units per notch)
        let dz = delta / 120;
        self.send_mouse_event(0, 0, dz);
    }

    /// Check if Ctrl+Alt is pressed
    pub fn is_release_combo_pressed(&self) -> bool {
        let keys = self.pressed_keys.borrow();
        // Check for Ctrl (0x1D) and Alt (0x38)
        keys.contains(&0x1D) && keys.contains(&0x38)
    }

    /// Send Ctrl+Alt+Del to guest
    pub fn send_ctrl_alt_del(self: Pin<&mut Self>) {
        // Send Ctrl press
        self.send_key_event(0x1D, true, false);
        // Send Alt press
        self.send_key_event(0x38, true, false);
        // Send Del press (extended)
        self.send_key_event(0x53, true, true);
        // Send Del release
        self.send_key_event(0x53, false, true);
        // Send Alt release
        self.send_key_event(0x38, false, false);
        // Send Ctrl release
        self.send_key_event(0x1D, false, false);
    }

    /// Send Ctrl+Alt+Backspace to guest
    pub fn send_ctrl_alt_backspace(self: Pin<&mut Self>) {
        // Send Ctrl press
        self.send_key_event(0x1D, true, false);
        // Send Alt press
        self.send_key_event(0x38, true, false);
        // Send Backspace press (scancode 0x0E)
        self.send_key_event(0x0E, true, false);
        // Send Backspace release
        self.send_key_event(0x0E, false, false);
        // Send Alt release
        self.send_key_event(0x38, false, false);
        // Send Ctrl release
        self.send_key_event(0x1D, false, false);
    }

    // =========================================================================
    // Private helper methods
    // =========================================================================

    /// Check if this key event is the release combo (Right Ctrl)
    fn check_release_combo(&self, qt_key: i32, _modifiers: i32) -> bool {
        // Qt::Key_Control is 0x01000021
        // We check for Right Ctrl specifically
        // Native scancode for Right Ctrl is 0xE01D (extended 0x1D)
        if qt_key == 0x01000021 {
            // For now, any Ctrl release while Ctrl+Alt held releases capture
            if self.is_release_combo_pressed() {
                return true;
            }
        }
        false
    }

    /// Send a keyboard event to the driver
    fn send_key_event(&self, scancode: u32, pressed: bool, extended: bool) {
        let fd = match *self.handle.borrow() {
            Some(fd) => fd,
            None => return,
        };

        let mut flags = 0u32;
        if pressed {
            flags |= key_flags::PRESSED;
        }
        if extended {
            flags |= key_flags::EXTENDED;
        }

        let event = KeyEvent { scancode, flags };

        unsafe {
            use rising_sun_common::ioctl::sunpci_keyboard_event;
            let _ = sunpci_keyboard_event(fd, &event);
        }
    }

    /// Send a mouse event to the driver
    fn send_mouse_event(&self, dx: i32, dy: i32, dz: i32) {
        let fd = match *self.handle.borrow() {
            Some(fd) => fd,
            None => return,
        };

        let buttons = *self.button_state.borrow();
        let event = MouseEvent { dx, dy, dz, buttons };

        unsafe {
            use rising_sun_common::ioctl::sunpci_mouse_event;
            let _ = sunpci_mouse_event(fd, &event);
        }
    }
}

// =============================================================================
// Qt Key to XT Scancode Conversion
// =============================================================================

/// Convert a Qt key code to an XT scancode.
/// Returns (scancode, is_extended).
/// Returns (0, false) if the key is not mappable.
fn qt_key_to_scancode(qt_key: i32, native_scancode: i32) -> (u32, bool) {
    // If we have a valid native scancode from the system, prefer it
    // Linux evdev scancodes are offset by 8 from XT scancodes
    if native_scancode > 8 {
        let xt = (native_scancode - 8) as u32;
        // Extended keys have high bit set in certain ranges
        let extended = xt > 0x7F || is_extended_key(qt_key);
        return (xt & 0x7F, extended);
    }

    // Fall back to Qt key code mapping
    qt_key_to_xt_scancode(qt_key)
}

/// Check if a Qt key is an extended key
fn is_extended_key(qt_key: i32) -> bool {
    matches!(
        qt_key,
        0x01000010  // Key_Home
        | 0x01000011  // Key_End
        | 0x01000012  // Key_Left
        | 0x01000013  // Key_Up
        | 0x01000014  // Key_Right
        | 0x01000015  // Key_Down
        | 0x01000016  // Key_PageUp
        | 0x01000017  // Key_PageDown
        | 0x01000006  // Key_Insert
        | 0x01000007  // Key_Delete
        | 0x01000025  // Key_Print
        | 0x01000026  // Key_ScrollLock (sometimes)
        | 0x01000027  // Key_Pause
    )
}

/// Map Qt key constants to XT scancodes
/// This is a fallback when native scancodes aren't available
fn qt_key_to_xt_scancode(qt_key: i32) -> (u32, bool) {
    // Qt key constants (from Qt::Key enum)
    match qt_key {
        // Escape
        0x01000000 => (0x01, false), // Key_Escape
        
        // Function keys
        0x01000030 => (0x3B, false), // Key_F1
        0x01000031 => (0x3C, false), // Key_F2
        0x01000032 => (0x3D, false), // Key_F3
        0x01000033 => (0x3E, false), // Key_F4
        0x01000034 => (0x3F, false), // Key_F5
        0x01000035 => (0x40, false), // Key_F6
        0x01000036 => (0x41, false), // Key_F7
        0x01000037 => (0x42, false), // Key_F8
        0x01000038 => (0x43, false), // Key_F9
        0x01000039 => (0x44, false), // Key_F10
        0x0100003a => (0x57, false), // Key_F11
        0x0100003b => (0x58, false), // Key_F12

        // Number row
        0x31 => (0x02, false), // 1
        0x32 => (0x03, false), // 2
        0x33 => (0x04, false), // 3
        0x34 => (0x05, false), // 4
        0x35 => (0x06, false), // 5
        0x36 => (0x07, false), // 6
        0x37 => (0x08, false), // 7
        0x38 => (0x09, false), // 8
        0x39 => (0x0A, false), // 9
        0x30 => (0x0B, false), // 0
        0x2D => (0x0C, false), // -
        0x3D => (0x0D, false), // =

        // Backspace, Tab, Enter
        0x01000003 => (0x0E, false), // Key_Backspace
        0x01000001 => (0x0F, false), // Key_Tab
        0x01000004 | 0x01000005 => (0x1C, false), // Key_Return / Key_Enter

        // Letter keys (uppercase ASCII)
        0x51 => (0x10, false), // Q
        0x57 => (0x11, false), // W
        0x45 => (0x12, false), // E
        0x52 => (0x13, false), // R
        0x54 => (0x14, false), // T
        0x59 => (0x15, false), // Y
        0x55 => (0x16, false), // U
        0x49 => (0x17, false), // I
        0x4F => (0x18, false), // O
        0x50 => (0x19, false), // P
        0x5B => (0x1A, false), // [
        0x5D => (0x1B, false), // ]

        0x41 => (0x1E, false), // A
        0x53 => (0x1F, false), // S
        0x44 => (0x20, false), // D
        0x46 => (0x21, false), // F
        0x47 => (0x22, false), // G
        0x48 => (0x23, false), // H
        0x4A => (0x24, false), // J
        0x4B => (0x25, false), // K
        0x4C => (0x26, false), // L
        0x3B => (0x27, false), // ;
        0x27 => (0x28, false), // '
        0x60 => (0x29, false), // ` (backtick)
        0x5C => (0x2B, false), // \

        0x5A => (0x2C, false), // Z
        0x58 => (0x2D, false), // X
        0x43 => (0x2E, false), // C
        0x56 => (0x2F, false), // V
        0x42 => (0x30, false), // B
        0x4E => (0x31, false), // N
        0x4D => (0x32, false), // M
        0x2C => (0x33, false), // ,
        0x2E => (0x34, false), // .
        0x2F => (0x35, false), // /

        // Modifier keys
        0x01000020 => (0x2A, false), // Key_Shift (left)
        0x01000021 => (0x1D, false), // Key_Control (left)
        0x01000023 => (0x38, false), // Key_Alt (left)
        0x01000022 => (0x3A, false), // Key_CapsLock
        
        // Space
        0x20 => (0x39, false), // Space

        // Navigation keys (extended)
        0x01000010 => (0x47, true), // Key_Home
        0x01000011 => (0x4F, true), // Key_End
        0x01000016 => (0x49, true), // Key_PageUp
        0x01000017 => (0x51, true), // Key_PageDown
        0x01000012 => (0x4B, true), // Key_Left
        0x01000014 => (0x4D, true), // Key_Right
        0x01000013 => (0x48, true), // Key_Up
        0x01000015 => (0x50, true), // Key_Down
        0x01000006 => (0x52, true), // Key_Insert
        0x01000007 => (0x53, true), // Key_Delete

        // Numpad
        0x01000024 => (0x45, false), // Key_NumLock
        
        // Print Screen, Scroll Lock, Pause
        0x01000025 => (0x37, true),  // Key_Print (SysRq)
        0x01000026 => (0x46, false), // Key_ScrollLock
        0x01000027 => (0x45, true),  // Key_Pause

        _ => (0, false), // Unknown key
    }
}
