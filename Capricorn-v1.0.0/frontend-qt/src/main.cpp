#include "AppData.h"
#include "ChatStore.h"
#include "CoreClient.h"
#include "MainWindow.h"
#include "ProcessSupervisor.h"

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QScreen>
#include <QStringList>
#include <QTimer>

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Capricorn"));
    QCoreApplication::setApplicationName(QStringLiteral("Capricorn-v1.0.0"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setQuitOnLastWindowClosed(false);

    const QStringList arguments = app.arguments();
    const bool isolatedTestInstance = arguments.contains(QStringLiteral("--startup-smoke"))
        || arguments.contains(QStringLiteral("--ui-audit"));
    const QString serverName = QStringLiteral("Capricorn-v1.0.0-Single-Instance");
    const QString lockRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(lockRoot);
    QLockFile instanceLock(QDir(lockRoot).filePath(QStringLiteral("Capricorn-v1.0.0.lock")));
    instanceLock.setStaleLockTime(30000);
    if (!isolatedTestInstance && !instanceLock.tryLock(0)) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(250)) {
            probe.write(QByteArrayLiteral("activate"));
            probe.flush();
            probe.waitForBytesWritten(250);
        }
        QMessageBox::information(nullptr, QStringLiteral("Capricorn"),
                                 QStringLiteral("Capricorn 已在运行，已尝试唤醒现有窗口。"));
        return 0;
    }
    QLocalServer instanceServer;
    if (!isolatedTestInstance) {
        QLocalServer::removeServer(serverName);
        if (!instanceServer.listen(serverName)) return 0;
    }

    QFile stylesheet(QStringLiteral(":/resources/app.qss"));
    if (stylesheet.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));

    AppData data;
    QString dataError;
    data.load(&dataError);

    const int auditIndex = arguments.indexOf(QStringLiteral("--ui-audit"));
    const bool auditMode = auditIndex >= 0;
    const QString auditDirectory = auditMode && auditIndex + 1 < arguments.size()
        ? arguments.at(auditIndex + 1)
        : QDir::current().filePath(QStringLiteral("ui-audit-v128"));
    const int smokeIndex = arguments.indexOf(QStringLiteral("--startup-smoke"));
    const bool startupSmokeMode = smokeIndex >= 0;
    const QString startupSmokeMarker = startupSmokeMode && smokeIndex + 1 < arguments.size()
        ? arguments.at(smokeIndex + 1)
        : QDir::current().filePath(QStringLiteral("v128-startup-smoke.ok"));

    ProcessSupervisor processes;
    QString processError;
    if (!auditMode && !startupSmokeMode) processes.start(&processError);

    CoreClient core;
    core.setEndpoint(processes.coreUrl(), processes.token());
    MainWindow window(&data, &core, &processes);

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&instanceServer, &window] {
        while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
            socket->readAll();
            socket->disconnectFromServer();
            socket->deleteLater();
        }
        window.restoreFromTray();
    });

    window.setWindowState(Qt::WindowNoState);
    if (QScreen *screen = window.screen() ? window.screen() : QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const int safeWidth = qMax(window.minimumWidth(), available.width() - 16);
        const int safeHeight = qMax(window.minimumHeight(), available.height() - 16);
        const QSize target(qMin(safeWidth, qMin(1180, qMax(window.minimumWidth(), available.width() - 140))),
                           qMin(safeHeight, qMin(740, qMax(window.minimumHeight(), available.height() - 120))));
        window.resize(target);
        window.move(available.center() - QPoint(target.width() / 2, target.height() / 2));
    }
    window.showNormal();
    if (auditMode) {
        QTimer::singleShot(250, &window, [&window, auditDirectory] {
            window.startUiAuditCapture(auditDirectory);
        });
    }
    if (startupSmokeMode) {
        QTimer::singleShot(1500, &window, [&window, startupSmokeMarker] {
            QFile marker(startupSmokeMarker);
            if (window.isVisible() && marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                marker.write("Capricorn v1.0.0 startup reached the event loop and remained visible.\n");
                marker.close();
            }
            QCoreApplication::exit(window.isVisible() ? 0 : 3);
        });
    }
    if (!processError.isEmpty() || !dataError.isEmpty()) {
        // Keep startup non-blocking and free of extra dialogs. Existing status areas
        // surface failures when the corresponding feature is used.
    }
    const int code = app.exec();
    ChatStore::instance().close();
    processes.stop();
    return code;
}
