/*
*   Copyright (C) 2008 Nicola Gigante <nicola.gigante@gmail.com>
*   Copyright (C) 2009 Dario Freddi <drf@kde.org>
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU Lesser General Public License as published by
*   the Free Software Foundation; either version 2.1 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU Lesser General Public License
*   along with this program; if not, write to the
*   Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .
*/

#include "kauthhelpersupport.h"

#include <cstdlib>

#ifndef Q_OS_WIN
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#else
// Quick hack to replace syslog (just write to stderr)
// TODO: should probably use ReportEvent
#define	LOG_ERR		4
#define	LOG_WARNING	5
#define	LOG_DEBUG	6
#define	LOG_USER	(1<<3)
static inline void openlog(const char*, int, int) {}
static inline void closelog() {}
#define syslog(level, ...) fprintf(stderr, __VA_ARGS__)

#endif

#include <QCoreApplication>
#include <QTimer>

#include "BackendsManager.h"

namespace KAuth
{

namespace HelperSupport
{
void helperDebugHandler(QtMsgType type, const QMessageLogContext &context, const QString &msgStr);
}

static bool remote_dbg = false;

#ifdef Q_OS_UNIX
static void fixEnvironment()
{
    //try correct HOME
    const char *home = "HOME";
    if (getenv(home) == NULL) {
        struct passwd *pw = getpwuid(getuid());
        int overwrite = 0;

        if (pw != NULL) {
            setenv(home, pw->pw_dir, overwrite);
        }
    }
}
#endif

int HelperSupport::helperMain(int argc, char **argv, const char *id, QObject *responder)
{
#ifdef Q_OS_UNIX
    fixEnvironment();
#endif

    openlog(id, 0, LOG_USER);
    qInstallMessageHandler(&HelperSupport::helperDebugHandler);

    // NOTE: The helper proxy might use dbus, and we should have the qapp
    //       before using dbus.
    QCoreApplication app(argc, argv);

    if (!BackendsManager::helperProxy()->initHelper(QString::fromLatin1(id))) {
        syslog(LOG_DEBUG, "Helper initialization failed");
        return -1;
    }

    //closelog();
    remote_dbg = true;

    BackendsManager::helperProxy()->setHelperResponder(responder);

    // Attach the timer
    QTimer *timer = new QTimer(0);
    responder->setProperty("__KAuth_Helper_Shutdown_Timer", QVariant::fromValue(timer));
    timer->setInterval(10000);
    timer->start();
    QObject::connect(timer, SIGNAL(timeout()), &app, SLOT(quit()));
    app.exec(); //krazy:exclude=crashy

    return 0;
}

void HelperSupport::helperDebugHandler(QtMsgType type, const QMessageLogContext &context, const QString &msgStr)
{
    Q_UNUSED(context); // can be used to find out about file, line, function name
    QByteArray msg = msgStr.toLocal8Bit();
    if (!remote_dbg) {
        int level = LOG_DEBUG;
        switch (type) {
        case QtDebugMsg:
            level = LOG_DEBUG;
            break;
        case QtWarningMsg:
            level = LOG_WARNING;
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            level = LOG_ERR;
            break;
        }
        syslog(level, "%s", msg.constData());
    } else {
        BackendsManager::helperProxy()->sendDebugMessage(type, msg.constData());
    }

    // Anyway I should follow the rule:
    if (type == QtFatalMsg) {
        exit(-1);
    }
}

void HelperSupport::progressStep(int step)
{
    BackendsManager::helperProxy()->sendProgressStep(step);
}

void HelperSupport::progressStep(const QVariantMap &data)
{
    BackendsManager::helperProxy()->sendProgressStep(data);
}

bool HelperSupport::isStopped()
{
    return BackendsManager::helperProxy()->hasToStopAction();
}

} // namespace Auth
