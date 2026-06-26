/**
 * SPDX-FileCopyrightText: 2014 Samoilenko Yuri <kinnalru@gmail.com>
 * SPDX-FileCopyrightText: 2024 Tarun <DistantMyth@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "mounter-win.h"

#include <QHostAddress>
#include <QStandardPaths>

#include <KLocalizedString>

#include "kdeconnectconfig.h"
#include "mountloop.h"
#include "networkpacket.h"
#include "plugin_sftp_debug.h"

#include <windows.h>

WinMounter::WinMounter(SftpPlugin *sftp)
    : QObject(sftp)
    , m_sftp(sftp)
    , m_proc(nullptr)
    , m_started(false)
{
    connect(&m_connectTimer, &QTimer::timeout, this, &WinMounter::onMountTimeout);

    connect(this, &WinMounter::mounted, &m_connectTimer, &QTimer::stop);
    connect(this, &WinMounter::failed, &m_connectTimer, &QTimer::stop);

    m_connectTimer.setInterval(10000);
    m_connectTimer.setSingleShot(true);

    QTimer::singleShot(0, this, &WinMounter::start);
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Created WinMounter";
}

WinMounter::~WinMounter()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Destroy WinMounter";
    unmount(false);
}

bool WinMounter::wait()
{
    if (m_started) {
        return true;
    }

    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Starting loop to wait for mount";

    MountLoop loop;
    connect(this, &WinMounter::mounted, &loop, &MountLoop::succeeded);
    connect(this, &WinMounter::failed, &loop, &MountLoop::failed);
    return loop.exec();
}

void WinMounter::onPacketReceived(const NetworkPacket &np)
{
    unmount(false);

    // A drive letter must be claimed before we can mount to it.
    const QChar driveLetter = findFreeDriveLetter();
    if (driveLetter.isNull()) {
        qCWarning(KDECONNECT_PLUGIN_SFTP) << "No free drive letter available";
        Q_EMIT failed(i18n("Failed to mount filesystem: no free drive letter available"));
        return;
    }
    m_drive = driveLetter + QLatin1String(":");

    // sshfs.exe ships with SSHFS-Win and is expected to be on PATH.
    // QStandardPaths::findExecutable returns an empty string if it is missing.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("sshfs"));
    if (program.isEmpty()) {
        qCWarning(KDECONNECT_PLUGIN_SFTP) << "sshfs.exe not found on PATH (SSHFS-Win/WinFsp not installed?)";
        Q_EMIT failed(i18n("SSHFS-Win (sshfs.exe) was not found. Please install SSHFS-Win and WinFsp to browse remote filesystems on Windows."));
        return;
    }

    QString path;
    if (np.has(QStringLiteral("multiPaths"))) {
        path = QStringLiteral("/");
    } else {
        path = np.get<QString>(QStringLiteral("path"));
    }

    QHostAddress addr = m_sftp->device()->getLocalIpAddress();
    if (addr == QHostAddress::Null) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "Device doesn't have a LanDeviceLink, unable to get IP address";
        return;
    }
    QString ip = addr.toString();
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        ip.prepend(QLatin1Char('['));
        ip.append(QLatin1Char(']'));
    }

    m_proc = new QProcess();
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::started, this, &WinMounter::onStarted);
    connect(m_proc, &QProcess::errorOccurred, this, &WinMounter::onError);
    connect(m_proc, &QProcess::finished, this, &WinMounter::onFinished);

    // clang-format off
    const QStringList arguments =
        QStringList() << QStringLiteral("%1@%2:%3").arg(np.get<QString>(QStringLiteral("user")), ip, path)
                      << m_drive << QStringLiteral("-p") << np.get<QString>(QStringLiteral("port"))
                      << QStringLiteral("-f") << QStringLiteral("-F") << QStringLiteral("NUL") // Do not use ~/.ssh/config
                      << QStringLiteral("-o") << QStringLiteral("IdentityFile=") + KdeConnectConfig::instance().privateKeyPath()
                      << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=no") // Do not ask for confirmation because it is not a known host
                      << QStringLiteral("-o") << QStringLiteral("UserKnownHostsFile=NUL") // Prevent storing as a known host
                      << QStringLiteral("-o") << QStringLiteral("reconnect")
                      << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=30")
                      << QStringLiteral("-o") << QStringLiteral("password_stdin");
    // clang-format on

    m_proc->setProgram(program, arguments);

    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Starting process: " << program << m_proc->arguments().join(QStringLiteral(" "));
    m_proc->start();

    // qCDebug(KDECONNECT_PLUGIN_SFTP) << "Passing password: " << np.get<QString>("password").toLatin1();
    m_proc->write(np.get<QString>(QStringLiteral("password")).toLatin1());
    m_proc->write("\n");
}

void WinMounter::onStarted()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Process started, mounted at" << m_drive;
    m_started = true;
    Q_EMIT mounted();

    auto proc = m_proc;
    connect(m_proc, &QProcess::readyReadStandardError, this, [proc]() {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "stderr: " << proc->readAll();
    });
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [proc]() {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "stdout:" << proc->readAll();
    });
}

void WinMounter::onError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "sshfs process failed to start";
        m_started = false;
        Q_EMIT failed(i18n("Failed to start sshfs"));
    } else if (error == QProcess::ProcessError::Crashed) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "sshfs process crashed";
        m_started = false;
        Q_EMIT failed(i18n("sshfs process crashed"));
    } else {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "sshfs process error" << error;
        m_started = false;
        Q_EMIT failed(i18n("Unknown error in sshfs"));
    }
}

void WinMounter::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "Process finished (exit code: " << exitCode << ")";
        Q_EMIT unmounted();
    } else {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "Process failed (exit code:" << exitCode << ")";
        Q_EMIT failed(i18n("Error when accessing filesystem. sshfs finished with exit code %0").arg(exitCode));
    }

    unmount(true);
}

void WinMounter::onMountTimeout()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Timeout: device not responding";
    Q_EMIT failed(i18n("Failed to mount filesystem: device not responding"));
}

void WinMounter::start()
{
    NetworkPacket np(PACKET_TYPE_SFTP_REQUEST, {{QStringLiteral("startBrowsing"), true}});
    m_sftp->sendPacket(np);

    m_connectTimer.start();
}

void WinMounter::unmount(bool finished)
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Unmount" << m_proc;
    if (m_proc) {
        if (!finished) {
            // Process is still running, we want to stop it.
            // Disconnect everything first to avoid re-entrancy: by the time the
            // finished signal arrives we might already be gone.
            m_proc->disconnect();
            m_proc->kill();

            auto proc = m_proc;
            m_proc = nullptr;
            connect(proc, &QProcess::finished, proc, [proc]() {
                qCDebug(KDECONNECT_PLUGIN_SFTP) << "Free" << proc;
                proc->deleteLater();
            });
            Q_EMIT unmounted();
        } else {
            m_proc->deleteLater();
        }

        // WinFsp ties the filesystem to the sshfs.exe process lifetime, so the
        // drive letter is released automatically once the process terminates.
        // No separate "unmount" command is required (unlike fusermount on Linux).
        m_proc = nullptr;
    }
    m_started = false;
    m_drive.clear();
}

QChar WinMounter::findFreeDriveLetter()
{
    // GetLogicalDrives() returns a bitmask where bit N (0..25) is set if the
    // drive letter ('A' + N) is in use. We scan the upper end of the alphabet
    // first ('Z' down to 'D'), skipping A:/B: (legacy floppy) and C: (system),
    // and return the first unused letter.
    const DWORD mask = GetLogicalDrives();
    for (QChar letter = u'Z'; letter >= u'D'; letter = QChar(letter.unicode() - 1)) {
        const int bit = letter.unicode() - u'A';
        if (!(mask & (DWORD(1) << bit))) {
            return letter;
        }
    }
    return QChar();
}

#include "moc_mounter-win.cpp"
