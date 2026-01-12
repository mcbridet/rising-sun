//! Network controller Qt bridge for network configuration and monitoring.
//!
//! This module handles:
//! - Enabling/disabling the virtual network adapter
//! - Configuring host interface bridging
//! - MAC address configuration
//! - Network statistics display

use std::cell::RefCell;

use rising_sun_common::ioctl::{NetworkConfig, NetworkStatus, net_flags};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, network_enabled)]
        #[qproperty(bool, network_connected)]
        #[qproperty(i32, driver_fd)]
        #[qproperty(QString, interface_name)]
        #[qproperty(QString, mac_address)]
        #[qproperty(i64, rx_packets)]
        #[qproperty(i64, tx_packets)]
        #[qproperty(i64, rx_bytes)]
        #[qproperty(i64, tx_bytes)]
        #[qproperty(QString, status_text)]
        type NetworkController = super::NetworkControllerRust;

        /// Initialize network controller with driver file descriptor
        #[qinvokable]
        fn init_network(self: Pin<&mut NetworkController>, fd: i32) -> bool;

        /// Enable or disable the network adapter
        #[qinvokable]
        fn set_enabled(self: Pin<&mut NetworkController>, enabled: bool) -> bool;

        /// Configure the host interface to bridge to
        #[qinvokable]
        fn set_interface(self: Pin<&mut NetworkController>, interface: QString) -> bool;

        /// Set the MAC address (empty string for auto-generate)
        #[qinvokable]
        fn set_mac(self: Pin<&mut NetworkController>, mac: QString) -> bool;

        /// Apply all pending configuration changes
        #[qinvokable]
        fn apply_config(self: Pin<&mut NetworkController>) -> bool;

        /// Poll for network status updates
        #[qinvokable]
        fn poll_status(self: Pin<&mut NetworkController>);

        /// Get list of available host network interfaces (semicolon-separated)
        #[qinvokable]
        fn get_available_interfaces(self: &NetworkController) -> QString;

        /// Get formatted statistics string
        #[qinvokable]
        fn get_stats_text(self: &NetworkController) -> QString;

        /// Get formatted byte count (KB, MB, GB)
        #[qinvokable]
        fn format_bytes(self: &NetworkController, bytes: i64) -> QString;

        /// Signal emitted when network status changes
        #[qsignal]
        fn status_changed(self: Pin<&mut NetworkController>);

        /// Signal emitted when configuration fails
        #[qsignal]
        fn config_error(self: Pin<&mut NetworkController>, message: QString);
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;
use rising_sun_common::ioctl::{sunpci_set_network, sunpci_get_network};

/// Rust implementation of the NetworkController
pub struct NetworkControllerRust {
    /// Whether network is enabled
    network_enabled: bool,
    /// Whether network is connected (TAP device active)
    network_connected: bool,
    /// Driver file descriptor
    driver_fd: i32,
    /// Host interface name
    interface_name: QString,
    /// MAC address string (XX:XX:XX:XX:XX:XX)
    mac_address: QString,
    /// Received packets
    rx_packets: i64,
    /// Transmitted packets
    tx_packets: i64,
    /// Received bytes
    rx_bytes: i64,
    /// Transmitted bytes
    tx_bytes: i64,
    /// Current status text
    status_text: QString,
    /// Pending configuration (not yet applied)
    pending_config: RefCell<NetworkConfig>,
    /// Last applied configuration
    last_config: RefCell<NetworkConfig>,
}

impl Default for NetworkControllerRust {
    fn default() -> Self {
        Self {
            network_enabled: false,
            network_connected: false,
            driver_fd: -1,
            interface_name: QString::from(""),
            mac_address: QString::from(""),
            rx_packets: 0,
            tx_packets: 0,
            rx_bytes: 0,
            tx_bytes: 0,
            status_text: QString::from("Network disabled"),
            pending_config: RefCell::new(NetworkConfig::default()),
            last_config: RefCell::new(NetworkConfig::default()),
        }
    }
}

impl qobject::NetworkController {
    /// Initialize network controller with driver file descriptor
    pub fn init_network(mut self: Pin<&mut Self>, fd: i32) -> bool {
        if fd < 0 {
            tracing::warn!("NetworkController: invalid driver fd");
            self.as_mut().set_status_text(QString::from("No driver connection"));
            return false;
        }

        self.as_mut().set_driver_fd(fd);

        // Get current network status
        self.as_mut().poll_status();

        tracing::info!("NetworkController initialized with fd={}", fd);
        true
    }

    /// Enable or disable the network adapter
    pub fn set_enabled(mut self: Pin<&mut Self>, enabled: bool) -> bool {
        {
            let mut config = self.pending_config.borrow_mut();
            if enabled {
                config.flags |= net_flags::ENABLED;
            } else {
                config.flags &= !net_flags::ENABLED;
            }
        }

        self.as_mut().set_network_enabled(enabled);
        
        if enabled {
            self.as_mut().set_status_text(QString::from("Network enabled (apply to activate)"));
        } else {
            self.as_mut().set_status_text(QString::from("Network disabled"));
        }

        tracing::info!("Network enabled: {}", enabled);
        true
    }

    /// Configure the host interface to bridge to
    pub fn set_interface(mut self: Pin<&mut Self>, interface: QString) -> bool {
        let iface = interface.to_string();
        
        {
            let mut config = self.pending_config.borrow_mut();
            let bytes = iface.as_bytes();
            let len = bytes.len().min(config.interface.len() - 1);
            config.interface[..len].copy_from_slice(&bytes[..len]);
            config.interface[len] = 0;
        }

        self.as_mut().set_interface_name(interface);
        tracing::info!("Network interface set to: {}", iface);
        true
    }

    /// Set the MAC address
    pub fn set_mac(mut self: Pin<&mut Self>, mac: QString) -> bool {
        let mac_str = mac.to_string();
        
        // Parse MAC address (XX:XX:XX:XX:XX:XX)
        let bytes = parse_mac_address(&mac_str);
        
        {
            let mut config = self.pending_config.borrow_mut();
            if let Some(mac_bytes) = bytes {
                config.mac_address.copy_from_slice(&mac_bytes);
            } else if mac_str.is_empty() {
                // Empty = auto-generate (driver will fill in)
                config.mac_address = [0; 6];
            } else {
                tracing::warn!("Invalid MAC address format: {}", mac_str);
                return false;
            }
        }

        self.as_mut().set_mac_address(mac);
        tracing::info!("MAC address set to: {}", if mac_str.is_empty() { "auto" } else { &mac_str });
        true
    }

    /// Apply all pending configuration changes
    pub fn apply_config(mut self: Pin<&mut Self>) -> bool {
        if self.driver_fd < 0 {
            self.as_mut().set_status_text(QString::from("No driver connection"));
            return false;
        }

        let config = self.pending_config.borrow().clone();
        
        let result = unsafe { sunpci_set_network(self.driver_fd, &config) };

        match result {
            Ok(_) => {
                *self.last_config.borrow_mut() = config;
                
                if config.flags & net_flags::ENABLED != 0 {
                    self.as_mut().set_status_text(QString::from("Network active"));
                    self.as_mut().set_network_connected(true);
                } else {
                    self.as_mut().set_status_text(QString::from("Network disabled"));
                    self.as_mut().set_network_connected(false);
                }
                
                self.as_mut().status_changed();
                tracing::info!("Network configuration applied");
                true
            }
            Err(e) => {
                let msg = format!("Failed to apply network config: {}", e);
                tracing::error!("{}", msg);
                self.as_mut().set_status_text(QString::from(&msg));
                let qmsg = QString::from(&msg);
                self.as_mut().config_error(qmsg);
                false
            }
        }
    }

    /// Poll for network status updates
    pub fn poll_status(mut self: Pin<&mut Self>) {
        if self.driver_fd < 0 {
            return;
        }

        let mut status = NetworkStatus::default();
        let result = unsafe { sunpci_get_network(self.driver_fd, &mut status) };

        match result {
            Ok(_) => {
                let enabled = status.flags & net_flags::ENABLED != 0;
                
                self.as_mut().set_network_connected(enabled);
                self.as_mut().set_rx_packets(status.rx_packets as i64);
                self.as_mut().set_tx_packets(status.tx_packets as i64);
                self.as_mut().set_rx_bytes(status.rx_bytes as i64);
                self.as_mut().set_tx_bytes(status.tx_bytes as i64);
            }
            Err(e) => {
                tracing::trace!("Failed to poll network status: {}", e);
            }
        }
    }

    /// Get list of available host network interfaces as semicolon-separated string
    /// QML can split this with: interfaces.split(";")
    pub fn get_available_interfaces(&self) -> QString {
        let interfaces = enumerate_network_interfaces();
        QString::from(&interfaces.join(";"))
    }

    /// Get formatted statistics string
    pub fn get_stats_text(&self) -> QString {
        QString::from(&format!(
            "RX: {} pkts ({}) | TX: {} pkts ({})",
            self.rx_packets,
            format_byte_size(self.rx_bytes as u64),
            self.tx_packets,
            format_byte_size(self.tx_bytes as u64)
        ))
    }

    /// Get formatted byte count
    pub fn format_bytes(&self, bytes: i64) -> QString {
        QString::from(&format_byte_size(bytes as u64))
    }
}

/// Parse MAC address string (XX:XX:XX:XX:XX:XX) to bytes
fn parse_mac_address(mac: &str) -> Option<[u8; 6]> {
    let parts: Vec<&str> = mac.split(':').collect();
    if parts.len() != 6 {
        return None;
    }

    let mut bytes = [0u8; 6];
    for (i, part) in parts.iter().enumerate() {
        match u8::from_str_radix(part, 16) {
            Ok(b) => bytes[i] = b,
            Err(_) => return None,
        }
    }
    Some(bytes)
}

/// Format MAC address bytes to string
fn format_mac_address(mac: &[u8; 6]) -> String {
    format!(
        "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    )
}

/// Format byte size to human-readable string
fn format_byte_size(bytes: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;

    if bytes >= GB {
        format!("{:.1} GB", bytes as f64 / GB as f64)
    } else if bytes >= MB {
        format!("{:.1} MB", bytes as f64 / MB as f64)
    } else if bytes >= KB {
        format!("{:.1} KB", bytes as f64 / KB as f64)
    } else {
        format!("{} B", bytes)
    }
}

/// Enumerate available network interfaces on the system
fn enumerate_network_interfaces() -> Vec<String> {
    let mut interfaces = Vec::new();

    // Read from /sys/class/net/
    if let Ok(entries) = std::fs::read_dir("/sys/class/net") {
        for entry in entries.flatten() {
            if let Ok(name) = entry.file_name().into_string() {
                // Skip loopback
                if name == "lo" {
                    continue;
                }
                
                // Try to determine interface type
                let operstate_path = format!("/sys/class/net/{}/operstate", name);
                let iface_type = get_interface_type(&name);
                
                // Check if interface is up
                let state = std::fs::read_to_string(&operstate_path)
                    .map(|s| s.trim().to_string())
                    .unwrap_or_else(|_| "unknown".to_string());
                
                let display = if state == "up" {
                    format!("{} - {} (up)", name, iface_type)
                } else {
                    format!("{} - {}", name, iface_type)
                };
                
                interfaces.push(display);
            }
        }
    }

    // Sort: physical interfaces first, then virtual
    interfaces.sort_by(|a, b| {
        let a_is_physical = !a.contains("Virtual") && !a.contains("Bridge") && !a.contains("TAP");
        let b_is_physical = !b.contains("Virtual") && !b.contains("Bridge") && !b.contains("TAP");
        b_is_physical.cmp(&a_is_physical).then(a.cmp(b))
    });

    interfaces
}

/// Determine interface type from name and sysfs
fn get_interface_type(name: &str) -> &'static str {
    // Check for wireless
    let wireless_path = format!("/sys/class/net/{}/wireless", name);
    if std::path::Path::new(&wireless_path).exists() {
        return "Wireless";
    }

    // Check device type
    let type_path = format!("/sys/class/net/{}/type", name);
    if let Ok(type_str) = std::fs::read_to_string(&type_path) {
        if let Ok(type_num) = type_str.trim().parse::<u32>() {
            match type_num {
                1 => {
                    // Ethernet - check if it's a bridge or virtual
                    if name.starts_with("br") || name.starts_with("virbr") {
                        return "Bridge";
                    }
                    if name.starts_with("veth") || name.starts_with("docker") {
                        return "Virtual";
                    }
                    if name.starts_with("tap") || name.starts_with("tun") {
                        return "TAP/TUN";
                    }
                    return "Ethernet";
                }
                772 => return "Loopback",
                _ => {}
            }
        }
    }

    // Check naming convention
    if name.starts_with("en") || name.starts_with("eth") {
        "Ethernet"
    } else if name.starts_with("wl") || name.starts_with("wlan") {
        "Wireless"
    } else if name.starts_with("br") {
        "Bridge"
    } else if name.starts_with("docker") || name.starts_with("veth") {
        "Virtual"
    } else {
        "Unknown"
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_mac_address() {
        assert_eq!(
            parse_mac_address("00:11:22:33:44:55"),
            Some([0x00, 0x11, 0x22, 0x33, 0x44, 0x55])
        );
        assert_eq!(
            parse_mac_address("AA:BB:CC:DD:EE:FF"),
            Some([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])
        );
        assert_eq!(parse_mac_address("invalid"), None);
        assert_eq!(parse_mac_address("00:11:22"), None);
    }

    #[test]
    fn test_format_mac_address() {
        assert_eq!(
            format_mac_address(&[0x00, 0x11, 0x22, 0x33, 0x44, 0x55]),
            "00:11:22:33:44:55"
        );
    }

    #[test]
    fn test_format_byte_size() {
        assert_eq!(format_byte_size(0), "0 B");
        assert_eq!(format_byte_size(512), "512 B");
        assert_eq!(format_byte_size(1024), "1.0 KB");
        assert_eq!(format_byte_size(1536), "1.5 KB");
        assert_eq!(format_byte_size(1048576), "1.0 MB");
        assert_eq!(format_byte_size(1073741824), "1.0 GB");
    }
}
