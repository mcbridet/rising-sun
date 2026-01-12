//! Rising Sun Qt Frontend

mod bridge;
mod ui;

use anyhow::Result;
use cxx_qt_lib::{QGuiApplication, QQmlApplicationEngine, QString, QUrl};

fn main() -> Result<()> {
    // Initialize Qt application
    let mut app = QGuiApplication::new();
    
    // Set application metadata (required for QSettings used by QtQuick.Dialogs)
    if let Some(mut app_ref) = app.as_mut() {
        let org_name = QString::from("RisingSun");
        let org_domain = QString::from("risingsun.local");
        let app_name = QString::from("Rising Sun");
        app_ref.as_mut().set_organization_name(&org_name);
        app_ref.as_mut().set_organization_domain(&org_domain);
        app_ref.as_mut().set_application_name(&app_name);
    }
    
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
