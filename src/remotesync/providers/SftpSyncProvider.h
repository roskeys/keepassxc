#ifndef KEEPASSXC_SFTPSYNCPROVIDER_H
#define KEEPASSXC_SFTPSYNCPROVIDER_H

#include <QList>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class SftpSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit SftpSyncProvider(QObject* parent = nullptr);
    ~SftpSyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void cancelAll() override;

private:
    void executeSftpCommands(const QStringList& commands,
                             const QString& expectedHost,
                             int port,
                             const QString& user,
                             const QString& keyPath,
                             const QString& password,
                             std::function<void(bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr)> onFinished);

    RemoteSyncSettings m_settings;
    QList<QPointer<QProcess>> m_activeProcesses;
};

#endif // KEEPASSXC_SFTPSYNCPROVIDER_H
