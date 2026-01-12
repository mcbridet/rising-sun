//! Main window Qt object bridge.

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, session_running)]
        type MainWindow = super::MainWindowRust;
    }
}

/// Rust implementation of the MainWindow
#[derive(Default)]
pub struct MainWindowRust {
    session_running: bool,
}
