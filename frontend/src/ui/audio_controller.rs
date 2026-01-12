//! Audio controller Qt bridge for audio playback.
//!
//! This module handles:
//! - Reading PCM audio from the driver
//! - Playing audio via the system audio API (ALSA/PipeWire)
//! - Volume control and mute state

use std::cell::RefCell;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use rising_sun_common::ioctl::{AudioFormat, AudioStatus, AudioVolume, audio_status_flags};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, audio_available)]
        #[qproperty(bool, audio_enabled)]
        #[qproperty(bool, audio_playing)]
        #[qproperty(bool, audio_muted)]
        #[qproperty(i32, volume_left)]
        #[qproperty(i32, volume_right)]
        #[qproperty(i32, volume_master)]
        #[qproperty(i32, sample_rate)]
        #[qproperty(i32, channels)]
        #[qproperty(i32, bits_per_sample)]
        #[qproperty(i32, driver_fd)]
        #[qproperty(QString, status_text)]
        type AudioController = super::AudioControllerRust;

        /// Initialize audio with driver file descriptor
        #[qinvokable]
        fn init_audio(self: Pin<&mut AudioController>, fd: i32) -> bool;

        /// Start audio playback
        #[qinvokable]
        fn start_playback(self: Pin<&mut AudioController>) -> bool;

        /// Stop audio playback
        #[qinvokable]
        fn stop_playback(self: Pin<&mut AudioController>);

        /// Toggle mute state
        #[qinvokable]
        fn toggle_mute(self: Pin<&mut AudioController>);

        /// Set volume (0-100)
        #[qinvokable]
        fn set_volume(self: Pin<&mut AudioController>, volume: i32);

        /// Set left/right volume independently (0-255)
        #[qinvokable]
        fn set_volume_stereo(self: Pin<&mut AudioController>, left: i32, right: i32);

        /// Poll for audio status updates
        #[qinvokable]
        fn poll_status(self: Pin<&mut AudioController>);

        /// Get volume as percentage (0-100)
        #[qinvokable]
        fn get_volume_percent(self: &AudioController) -> i32;

        /// Check if audio is currently outputting
        #[qinvokable]
        fn is_active(self: &AudioController) -> bool;
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;

/// Audio playback state
struct PlaybackState {
    /// Whether playback is running
    running: Arc<AtomicBool>,
    /// Audio thread handle (if using threaded approach)
    thread_handle: Option<std::thread::JoinHandle<()>>,
}

impl Default for PlaybackState {
    fn default() -> Self {
        Self {
            running: Arc::new(AtomicBool::new(false)),
            thread_handle: None,
        }
    }
}

/// Rust implementation of the AudioController
pub struct AudioControllerRust {
    /// Whether audio hardware is available
    audio_available: bool,
    /// Whether audio output is enabled
    audio_enabled: bool,
    /// Whether guest is currently playing audio
    audio_playing: bool,
    /// Whether audio is muted
    audio_muted: bool,
    /// Left channel volume (0-255)
    volume_left: i32,
    /// Right channel volume (0-255)
    volume_right: i32,
    /// Master volume (0-255) - average of L/R
    volume_master: i32,
    /// Current sample rate
    sample_rate: i32,
    /// Number of channels
    channels: i32,
    /// Bits per sample
    bits_per_sample: i32,
    /// Driver file descriptor
    driver_fd: i32,
    /// Status text for UI
    status_text: QString,
    /// Playback state
    playback: RefCell<PlaybackState>,
    /// Cached audio format
    format: RefCell<Option<AudioFormat>>,
}

impl Default for AudioControllerRust {
    fn default() -> Self {
        Self {
            audio_available: false,
            audio_enabled: true,
            audio_playing: false,
            audio_muted: false,
            volume_left: 200,
            volume_right: 200,
            volume_master: 200,
            sample_rate: 44100,
            channels: 2,
            bits_per_sample: 16,
            driver_fd: -1,
            status_text: QString::from("Not initialized"),
            playback: RefCell::new(PlaybackState::default()),
            format: RefCell::new(None),
        }
    }
}

impl qobject::AudioController {
    /// Initialize audio with driver file descriptor
    pub fn init_audio(mut self: Pin<&mut Self>, fd: i32) -> bool {
        self.as_mut().set_driver_fd(fd);

        if fd < 0 {
            self.as_mut().set_audio_available(false);
            self.set_status_text(QString::from("No driver connection"));
            return false;
        }

        // Query audio status from driver
        match self.query_audio_status(fd) {
            Ok(status) => {
                let available = status.flags & audio_status_flags::AVAILABLE != 0;
                self.as_mut().set_audio_available(available);
                
                if available {
                    // Query format
                    if let Ok(format) = self.query_audio_format(fd) {
                        self.as_mut().set_sample_rate(format.sample_rate as i32);
                        self.as_mut().set_channels(format.channels as i32);
                        self.as_mut().set_bits_per_sample(format.bits_per_sample as i32);
                        *self.format.borrow_mut() = Some(format);
                    }
                    
                    // Query volume
                    if let Ok(volume) = self.query_volume(fd) {
                        self.as_mut().set_volume_left(volume.left as i32);
                        self.as_mut().set_volume_right(volume.right as i32);
                        self.as_mut().set_volume_master((volume.left as i32 + volume.right as i32) / 2);
                        self.as_mut().set_audio_muted(volume.muted != 0);
                    }
                    
                    self.set_status_text(QString::from("Audio ready"));
                    true
                } else {
                    self.set_status_text(QString::from("No audio hardware"));
                    false
                }
            }
            Err(e) => {
                self.as_mut().set_audio_available(false);
                self.set_status_text(QString::from(&format!("Error: {}", e)));
                false
            }
        }
    }

    /// Start audio playback
    pub fn start_playback(mut self: Pin<&mut Self>) -> bool {
        if !*self.as_ref().audio_available() || !*self.as_ref().audio_enabled() {
            return false;
        }

        let fd = *self.as_ref().driver_fd();
        if fd < 0 {
            return false;
        }

        // Mark playback as running
        {
            let playback = self.playback.borrow_mut();
            if playback.running.load(Ordering::SeqCst) {
                return true; // Already running
            }
            playback.running.store(true, Ordering::SeqCst);
        }

        // Get format info
        let format = match self.format.borrow().clone() {
            Some(f) => f,
            None => {
                self.playback.borrow().running.store(false, Ordering::SeqCst);
                return false;
            }
        };

        // Start audio playback thread
        let running = self.playback.borrow().running.clone();
        let sample_rate = format.sample_rate;
        let channels = format.channels;
        let bits = format.bits_per_sample;

        let handle = std::thread::spawn(move || {
            audio_playback_thread(fd, running, sample_rate, channels, bits);
        });

        self.playback.borrow_mut().thread_handle = Some(handle);
        self.as_mut().set_audio_playing(true);
        self.set_status_text(QString::from("Playing"));
        true
    }

    /// Stop audio playback
    pub fn stop_playback(mut self: Pin<&mut Self>) {
        {
            let mut playback = self.playback.borrow_mut();
            playback.running.store(false, Ordering::SeqCst);
            
            // Wait for thread to finish
            if let Some(handle) = playback.thread_handle.take() {
                let _ = handle.join();
            }
        }
        
        self.as_mut().set_audio_playing(false);
        self.set_status_text(QString::from("Stopped"));
    }

    /// Toggle mute state
    pub fn toggle_mute(mut self: Pin<&mut Self>) {
        let muted = !*self.as_ref().audio_muted();
        self.as_mut().set_audio_muted(muted);
        
        let fd = *self.as_ref().driver_fd();
        if fd >= 0 {
            let left = *self.as_ref().volume_left() as u8;
            let right = *self.as_ref().volume_right() as u8;
            let _ = self.set_driver_volume(fd, left, right, muted);
        }
    }

    /// Set master volume (0-100)
    pub fn set_volume(mut self: Pin<&mut Self>, volume: i32) {
        // Convert 0-100 to 0-255
        let vol_255 = ((volume.clamp(0, 100) as f32 / 100.0) * 255.0) as i32;
        self.as_mut().set_volume_left(vol_255);
        self.as_mut().set_volume_right(vol_255);
        self.as_mut().set_volume_master(vol_255);
        
        let fd = *self.as_ref().driver_fd();
        let muted = *self.as_ref().audio_muted();
        if fd >= 0 {
            let _ = self.set_driver_volume(fd, vol_255 as u8, vol_255 as u8, muted);
        }
    }

    /// Set stereo volume (0-255 each)
    pub fn set_volume_stereo(mut self: Pin<&mut Self>, left: i32, right: i32) {
        let left = left.clamp(0, 255);
        let right = right.clamp(0, 255);
        self.as_mut().set_volume_left(left);
        self.as_mut().set_volume_right(right);
        self.as_mut().set_volume_master((left + right) / 2);
        
        let fd = *self.as_ref().driver_fd();
        let muted = *self.as_ref().audio_muted();
        if fd >= 0 {
            let _ = self.set_driver_volume(fd, left as u8, right as u8, muted);
        }
    }

    /// Poll for status updates
    pub fn poll_status(mut self: Pin<&mut Self>) {
        let fd = *self.as_ref().driver_fd();
        if fd < 0 {
            return;
        }

        if let Ok(status) = self.query_audio_status(fd) {
            let playing = status.flags & audio_status_flags::PLAYING != 0;
            self.as_mut().set_audio_playing(playing);
            self.as_mut().set_sample_rate(status.sample_rate as i32);
            
            // Update format if changed
            if let Ok(format) = self.query_audio_format(fd) {
                if format.sample_rate != 0 {
                    self.as_mut().set_sample_rate(format.sample_rate as i32);
                    self.as_mut().set_channels(format.channels as i32);
                    self.as_mut().set_bits_per_sample(format.bits_per_sample as i32);
                    *self.format.borrow_mut() = Some(format);
                }
            }
        }
    }

    /// Get volume as percentage
    pub fn get_volume_percent(&self) -> i32 {
        ((*self.volume_master() as f32 / 255.0) * 100.0) as i32
    }

    /// Check if audio is actively outputting
    pub fn is_active(&self) -> bool {
        self.playback.borrow().running.load(Ordering::SeqCst)
    }

    // =========================================================================
    // Private helper methods
    // =========================================================================

    fn query_audio_status(&self, fd: i32) -> Result<AudioStatus, String> {
        let mut status = AudioStatus::default();
        unsafe {
            rising_sun_common::ioctl::sunpci_get_audio_status(fd, &mut status)
                .map_err(|e| format!("ioctl failed: {}", e))?;
        }
        Ok(status)
    }

    fn query_audio_format(&self, fd: i32) -> Result<AudioFormat, String> {
        let mut format = AudioFormat::default();
        unsafe {
            rising_sun_common::ioctl::sunpci_get_audio_format(fd, &mut format)
                .map_err(|e| format!("ioctl failed: {}", e))?;
        }
        Ok(format)
    }

    fn query_volume(&self, fd: i32) -> Result<AudioVolume, String> {
        let mut volume = AudioVolume::default();
        unsafe {
            rising_sun_common::ioctl::sunpci_get_audio_volume(fd, &mut volume)
                .map_err(|e| format!("ioctl failed: {}", e))?;
        }
        Ok(volume)
    }

    fn set_driver_volume(&self, fd: i32, left: u8, right: u8, muted: bool) -> Result<(), String> {
        let volume = AudioVolume {
            left,
            right,
            muted: if muted { 1 } else { 0 },
            reserved: 0,
        };
        unsafe {
            rising_sun_common::ioctl::sunpci_set_audio_volume(fd, &volume)
                .map_err(|e| format!("ioctl failed: {}", e))?;
        }
        Ok(())
    }
}

/// Thread-safe ring buffer for audio samples
struct AudioRingBuffer {
    buffer: Vec<i16>,
    capacity: usize,
    read_pos: std::sync::atomic::AtomicUsize,
    write_pos: std::sync::atomic::AtomicUsize,
}

impl AudioRingBuffer {
    fn new(capacity: usize) -> Self {
        Self {
            buffer: vec![0i16; capacity],
            capacity,
            read_pos: std::sync::atomic::AtomicUsize::new(0),
            write_pos: std::sync::atomic::AtomicUsize::new(0),
        }
    }

    /// Returns number of samples available to read
    fn available(&self) -> usize {
        let write = self.write_pos.load(Ordering::Acquire);
        let read = self.read_pos.load(Ordering::Acquire);
        if write >= read {
            write - read
        } else {
            self.capacity - read + write
        }
    }

    /// Returns free space for writing
    fn free_space(&self) -> usize {
        self.capacity - self.available() - 1
    }

    /// Write samples to the ring buffer (called from reader thread)
    /// Returns number of samples actually written
    fn write(&self, samples: &[i16]) -> usize {
        let free = self.free_space();
        let to_write = samples.len().min(free);
        
        if to_write == 0 {
            return 0;
        }

        let write_pos = self.write_pos.load(Ordering::Acquire);
        
        // Get mutable access - safe because we're the only writer
        let buffer_ptr = self.buffer.as_ptr() as *mut i16;
        
        for (i, &sample) in samples[..to_write].iter().enumerate() {
            let pos = (write_pos + i) % self.capacity;
            unsafe {
                *buffer_ptr.add(pos) = sample;
            }
        }

        self.write_pos.store((write_pos + to_write) % self.capacity, Ordering::Release);
        to_write
    }

    /// Read samples from the ring buffer (called from audio callback)
    /// Returns number of samples actually read
    fn read(&self, output: &mut [i16]) -> usize {
        let available = self.available();
        let to_read = output.len().min(available);

        if to_read == 0 {
            return 0;
        }

        let read_pos = self.read_pos.load(Ordering::Acquire);

        for i in 0..to_read {
            let pos = (read_pos + i) % self.capacity;
            output[i] = self.buffer[pos];
        }

        self.read_pos.store((read_pos + to_read) % self.capacity, Ordering::Release);
        to_read
    }
}

/// Audio playback thread
/// 
/// Reads audio samples from the driver and plays them through the system audio.
/// Uses cpal for cross-platform audio output (ALSA/PipeWire/PulseAudio on Linux).
fn audio_playback_thread(
    fd: i32,
    running: Arc<AtomicBool>,
    sample_rate: u32,
    channels: u32,
    bits_per_sample: u32,
) {
    use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
    use rising_sun_common::ioctl::{AudioBuffer, sunpci_read_audio};

    tracing::info!(
        "Audio thread starting: {}Hz, {} channels, {}-bit",
        sample_rate, channels, bits_per_sample
    );

    // Initialize cpal audio host
    let host = cpal::default_host();
    
    let device = match host.default_output_device() {
        Some(d) => d,
        None => {
            tracing::error!("No audio output device found");
            return;
        }
    };

    tracing::info!("Using audio device: {}", device.name().unwrap_or_default());

    // Build stream config matching the guest audio format
    let config = cpal::StreamConfig {
        channels: channels as u16,
        sample_rate: cpal::SampleRate(sample_rate),
        buffer_size: cpal::BufferSize::Default,
    };

    // Create ring buffer - sized for ~200ms of audio (good balance of latency vs. underrun protection)
    // At 44100Hz stereo, that's 44100 * 2 * 0.2 = 17640 samples
    let ring_buffer_size = (sample_rate as usize * channels as usize / 4).max(8192);
    let ring_buffer = Arc::new(AudioRingBuffer::new(ring_buffer_size));
    let ring_buffer_callback = Arc::clone(&ring_buffer);

    // Error callback
    let err_fn = |err| tracing::error!("Audio stream error: {}", err);

    // Build output stream
    let stream = device.build_output_stream(
        &config,
        move |data: &mut [i16], _: &cpal::OutputCallbackInfo| {
            let read = ring_buffer_callback.read(data);
            // Zero-fill any remaining space (underrun)
            for sample in data[read..].iter_mut() {
                *sample = 0;
            }
        },
        err_fn,
        None,
    );

    let stream = match stream {
        Ok(s) => s,
        Err(e) => {
            tracing::error!("Failed to build audio stream: {}", e);
            return;
        }
    };

    // Start the audio stream
    if let Err(e) = stream.play() {
        tracing::error!("Failed to start audio stream: {}", e);
        return;
    }

    tracing::info!("Audio stream started (ring buffer: {} samples)", ring_buffer_size);

    // Pre-allocate conversion buffer to avoid heap allocations in the loop
    let max_samples = 16384 / 2; // AudioBuffer is 16KB, max 8K i16 samples
    let mut sample_buffer: Vec<i16> = Vec::with_capacity(max_samples);

    // Buffer for reading from driver
    let mut buffer = AudioBuffer::default();
    
    // Calculate timing based on ring buffer fill level
    let samples_per_ms = (sample_rate * channels) / 1000;
    
    // Main loop: read from driver and feed to ring buffer
    while running.load(Ordering::SeqCst) {
        // Check ring buffer fill level
        let available = ring_buffer.available();
        let fill_percent = (available * 100) / ring_buffer_size;
        
        // If buffer is >75% full, sleep a bit to let it drain
        if fill_percent > 75 {
            let drain_samples = available - (ring_buffer_size / 2);
            let drain_ms = drain_samples as u64 / samples_per_ms as u64;
            std::thread::sleep(std::time::Duration::from_millis(drain_ms.max(1).min(20)));
            continue;
        }

        // Read from driver
        buffer.size = buffer.data.len() as u32;
        let result = unsafe { sunpci_read_audio(fd, &mut buffer) };
        
        match result {
            Ok(_) => {
                let bytes_read = buffer.size as usize;
                if bytes_read > 0 {
                    // Convert to i16 samples
                    sample_buffer.clear();
                    if bits_per_sample == 16 {
                        // 16-bit signed - reinterpret bytes as i16
                        for chunk in buffer.data[..bytes_read].chunks_exact(2) {
                            sample_buffer.push(i16::from_le_bytes([chunk[0], chunk[1]]));
                        }
                    } else {
                        // 8-bit unsigned - convert to 16-bit signed
                        for &b in buffer.data[..bytes_read].iter() {
                            sample_buffer.push((b as i16 - 128) * 256);
                        }
                    }

                    // Write to ring buffer
                    let written = ring_buffer.write(&sample_buffer);
                    if written < sample_buffer.len() {
                        tracing::trace!("Ring buffer overflow, dropped {} samples", 
                            sample_buffer.len() - written);
                    }
                } else {
                    // No data from driver, brief sleep
                    std::thread::sleep(std::time::Duration::from_millis(2));
                }
            }
            Err(e) => {
                tracing::warn!("Audio read error: {}", e);
                std::thread::sleep(std::time::Duration::from_millis(20));
            }
        }
    }

    // Stop the stream
    drop(stream);
    tracing::info!("Audio thread stopped");
}
