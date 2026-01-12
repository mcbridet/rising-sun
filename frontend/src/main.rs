//! Rising Sun Qt Frontend

mod bridge;
mod ui;

use anyhow::Result;
use cxx_qt_lib::{QGuiApplication, QQmlApplicationEngine, QUrl};

fn main() -> Result<()> {
    // Initialize Qt application
    let mut app = QGuiApplication::new();
    let mut engine = QQmlApplicationEngine::new();

    // Load the main QML file
    if let Some(engine) = engine.as_mut() {
        engine.load(&QUrl::from("qrc:/qt/qml/com/risingsun/qml/main.qml"));
    }

    // Run the application
    if let Some(app) = app.as_mut() {
        app.exec();
    }

    Ok(())
}
