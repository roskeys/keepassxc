#ifndef KEEPASSXC_DROPBOXSYNCPROVIDER_H
#define KEEPASSXC_DROPBOXSYNCPROVIDER_H

#include <QList>
#include <QPointer>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class QNetworkReply;
class QNetworkRequest;

class DropboxSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit DropboxSyncProvider(QObject* parent = nullptr);
    ~DropboxSyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void deleteFile(const QString& remotePath, SyncCallback cb);
    void cancelAll() override;

private:
    QString normalizePath(const QString& rawPath) const;
    void applyAuth(QNetworkRequest& req) const;

    RemoteSyncSettings m_settings;
    QList<QPointer<QNetworkReply>> m_activeReplies;
};

#endif // KEEPASSXC_DROPBOXSYNCPROVIDER_H
