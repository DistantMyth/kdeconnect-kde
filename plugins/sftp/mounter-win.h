/**
 * SPDX-FileCopyrightText: 2014 Samoilenko Yuri <kinnalru@gmail.com>
 * SPDX-FileCopyrightText: 2024 Tarun <DistantMyth@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QProcess>
#include <QString>
#include <QTimer>

#include "sftpplugin-win.h"

class SftpPlugin;

/**
 * Drives an SSHFS-Win (sshfs.exe + WinFsp) mount of the remote device filesystem
 * to a drive letter, mirroring the behaviour of the Linux Mounter.
 *
 * sshfs.exe is launched in the foreground with the same -o password_stdin trick
 * used on Linux: the session password is written to the process' stdin. WinFsp
 * ties the filesystem lifetime to the mounting process, so the drive letter is
 * released automatically when sshfs.exe is killed.
 */
class WinMounter : public QObject
{
    Q_OBJECT
public:
    explicit WinMounter(SftpPlugin *sftp);
    ~WinMounter() override;

    bool wait();
    bool isMounted() const
    {
        return m_started;
    }
    QString mountPoint() const
    {
        return m_drive;
    }
    void onPacketReceived(const NetworkPacket &np);

Q_SIGNALS:
    void mounted();
    void unmounted();
    void failed(const QString &message);

private Q_SLOTS:
    void start();
    void onStarted();
    void onError(QProcess::ProcessError error);
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onMountTimeout();

private:
    void unmount(bool finished);
    static QChar findFreeDriveLetter();

private:
    SftpPlugin *m_sftp;
    QProcess *m_proc;
    QTimer m_connectTimer;
    QString m_drive; // e.g. "Z:"
    bool m_started;
};
