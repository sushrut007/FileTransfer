#include "FileTransfer.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Set application properties for proper cleanup
    app.setApplicationName("File Transfer Manager");
    app.setApplicationVersion("1.0.0");
    //app.setQuitOnLastWindowClosed(false);  // Allow tray to keep app alive
    LogHandler::instance()->install();
    FileTransfer window;
    window.show();

    return app.exec();
}
