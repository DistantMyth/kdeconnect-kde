/**
 * SPDX-FileCopyrightText: 2019 Piyush Aggarwal <piyushaggarwal002@gmail.com>
 * SPDX-FileCopyrightText: 2024 Tarun <DistantMyth@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QVariantMap>

#include <core/device.h>
#include <core/kdeconnectplugin.h>

#define PACKET_TYPE_SFTP_REQUEST QStringLiteral("kdeconnect.sftp.request")
class WinMounter;

/**
 * Windows counterpart of SftpPlugin. Unlike the previous implementation, which
 * only handed an sftp:// URL to the shell, this mounts the remote filesystem to
 * a drive letter using SSHFS-Win + WinFsp (see WinMounter).
 *
 * To keep the D-Bus interface identical across platforms, this header declares
 * the same Q_SCRIPTABLE surface as the Linux sftpplugin.h. The
 * org.kde.kdeconnect.device.sftp interface is generated from sftpplugin.h on
 * every platform (see dbusinterfaces/CMakeLists.txt) and consumed by the shared
 * QML, so the methods/signals below must match it exactly.
 *
 * In addition, on Windows the filesystem is mounted automatically as soon as a
 * paired device becomes reachable (see connected()) and unmounted when it goes
 * away.
 */
class SftpPlugin : public KdeConnectPlugin
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.sftp")

public:
    explicit SftpPlugin(QObject *parent, const QVariantList &args);
    ~SftpPlugin() override;

    void receivePacket(const NetworkPacket &np) override;
    void connected() override;

    QString dbusPath() const override
    {
        return QLatin1String("/modules/kdeconnect/devices/%1/sftp").arg(deviceId);
    }

    Q_SCRIPTABLE bool startBrowsing();
    Q_SCRIPTABLE void mount();
    Q_SCRIPTABLE void unmount();
    Q_SCRIPTABLE bool mountAndWait();
    Q_SCRIPTABLE bool isMounted() const;
    Q_SCRIPTABLE QString getMountError();
    Q_SCRIPTABLE QString mountPoint();
    Q_SCRIPTABLE QVariantMap getDirectories(); // Actually a QMap<String, String>, but QDBus prefers this

Q_SIGNALS:
    Q_SCRIPTABLE void mounted();
    Q_SCRIPTABLE void unmounted();

private Q_SLOTS:
    void onMounted();
    void onUnmounted();
    void onFailed(const QString &message);
    void onReachabilityChanged(bool reachable);

private:
    WinMounter *m_mounter;
    QString deviceId; // Storing it to avoid accessing device() from the destructor which could cause a crash

    QVariantMap remoteDirectories; // Actually a QMap<String, String>, but QDBus prefers this
    QString mountError;
};
