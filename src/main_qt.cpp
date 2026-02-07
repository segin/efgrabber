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
