/*
 * src/main_qt.cpp - Main entry point for the Qt application
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include <QApplication>
#include "gui/main_window_qt.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Epstein Files Grabber");
    app.setApplicationVersion("1.0.0");

    efgrabber::MainWindow window;
    window.show();

    return app.exec();
}
