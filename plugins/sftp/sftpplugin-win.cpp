/**
 * SPDX-FileCopyrightText: 2019 Piyush Aggarwal <piyushaggarwal002@gmail.com>
 * SPDX-FileCopyrightText: 2024 Tarun <DistantMyth@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
#include "sftpplugin-win.h"

#include <QDesktopServices>
#include <QSet>
#include <QUrl>

#include <KLocalizedString>
#include <KPluginFactory>

#include "daemon.h"
#include "mounter-win.h"
#include "networkpacket.h"
#include "plugin_sftp_debug.h"

K_PLUGIN_CLASS_WITH_JSON(SftpPlugin, "kdeconnect_sftp.json")

SftpPlugin::SftpPlugin(QObject *parent, const QVariantList &args)
    : KdeConnectPlugin(parent, args)
    , m_mounter(nullptr)
    , deviceId(device()->id())
{
    // Auto-unmount when the paired device becomes unreachable, so the drive
    // letter is released as soon as the connection drops.
    connect(device(), &Device::reachableChanged, this, &SftpPlugin::onReachabilityChanged);
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Created device:" << device()->name();
}

SftpPlugin::~SftpPlugin()
{
    unmount();
}

void SftpPlugin::connected()
{
    // The paired device just became reachable: mount its filesystem so the drive
    // shows up in File Explorer automatically (Windows-specific behaviour).
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Device connected, auto-mounting" << device()->name();
    mount();
}

void SftpPlugin::onReachabilityChanged(bool reachable)
{
    if (!reachable) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "Device unreachable, unmounting" << device()->name();
        unmount();
    }
}

void SftpPlugin::mount()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << "Mount device:" << device()->name();
    if (m_mounter) {
        return;
    }

    m_mounter = new WinMounter(this);
    connect(m_mounter, &WinMounter::mounted, this, &SftpPlugin::onMounted);
    connect(m_mounter, &WinMounter::unmounted, this, &SftpPlugin::onUnmounted);
    connect(m_mounter, &WinMounter::failed, this, &SftpPlugin::onFailed);
}

void SftpPlugin::unmount()
{
    if (m_mounter) {
        m_mounter->deleteLater();
        m_mounter = nullptr;
    }
}

bool SftpPlugin::mountAndWait()
{
    mount();
    return m_mounter->wait();
}

bool SftpPlugin::isMounted() const
{
    return m_mounter && m_mounter->isMounted();
}

QString SftpPlugin::getMountError()
{
    if (!mountError.isEmpty()) {
        return mountError;
    }
    return QString();
}

bool SftpPlugin::startBrowsing()
{
    if (mountAndWait()) {
        // Open File Explorer at the mounted drive letter.
        QDesktopServices::openUrl(QUrl::fromLocalFile(mountPoint()));
        return true;
    }
    return false;
}

void SftpPlugin::receivePacket(const NetworkPacket &np)
{
    if (np.has(QStringLiteral("errorMessage"))) {
        const QString errorMessage = np.get<QString>(QStringLiteral("errorMessage"));
        if (m_mounter) {
            Q_EMIT m_mounter->failed(errorMessage);
        } else {
            onFailed(errorMessage);
        }
        return;
    }

    static const QSet<QString> expectedFields{QStringLiteral("user"), QStringLiteral("port"), QStringLiteral("path"), QStringLiteral("password")};
    const QStringList receivedFieldsList = np.body().keys();
    const QSet<QString> receivedFields(receivedFieldsList.begin(), receivedFieldsList.end());
    if (!(expectedFields - receivedFields).isEmpty()) {
        qCWarning(KDECONNECT_PLUGIN_SFTP) << "Invalid sftp packet received";
        return;
    }

    // If a packet arrives before mounting or after the mount timed out, ignore it.
    if (!m_mounter) {
        qCDebug(KDECONNECT_PLUGIN_SFTP) << "Received network packet but no mount is active, ignoring";
        return;
    }

    m_mounter->onPacketReceived(np);

    remoteDirectories.clear();
    if (np.has(QStringLiteral("multiPaths"))) {
        QStringList paths = np.get<QStringList>(QStringLiteral("multiPaths"), QStringList());
        QStringList names = np.get<QStringList>(QStringLiteral("pathNames"), QStringList());
        int size = qMin<int>(names.size(), paths.size());
        for (int i = 0; i < size; i++) {
            remoteDirectories.insert(mountPoint() + paths.at(i), names.at(i));
        }
    } else {
        remoteDirectories.insert(mountPoint(), i18n("All files"));
        remoteDirectories.insert(mountPoint() + QStringLiteral("/DCIM/Camera"), i18n("Camera pictures"));
    }
}

QString SftpPlugin::mountPoint()
{
    if (m_mounter) {
        return m_mounter->mountPoint();
    }
    return QString();
}

void SftpPlugin::onMounted()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << device()->name() << QStringLiteral("Remote filesystem mounted at %1").arg(mountPoint());

    Q_EMIT mounted();
}

void SftpPlugin::onUnmounted()
{
    qCDebug(KDECONNECT_PLUGIN_SFTP) << device()->name() << "Remote filesystem unmounted";

    unmount();

    Q_EMIT unmounted();
}

void SftpPlugin::onFailed(const QString &message)
{
    mountError = message;
    Daemon::instance()->reportError(device()->name(), message);
    unmount();

    Q_EMIT unmounted();
}

QVariantMap SftpPlugin::getDirectories()
{
    return remoteDirectories;
}

#include "moc_sftpplugin-win.cpp"
#include "sftpplugin-win.moc"
