#include "FileTransfer.h"
#include "FirstRunInstaller.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Set application properties for proper cleanup
    app.setApplicationName("FileTransfer");
    app.setOrganizationName("FileMitra");
    app.setApplicationVersion("1.0.0");
    //app.setQuitOnLastWindowClosed(false);  // Allow tray to keep app alive

    if (!FirstRunInstaller::ensureInstalled(argc, argv))
        return 0;

    LogHandler::instance()->install();
    FileTransfer window;
    window.show();

    return app.exec();
}
