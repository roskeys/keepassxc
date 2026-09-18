#ifndef KEEPASSXC_S3SYNCPROVIDER_H
#define KEEPASSXC_S3SYNCPROVIDER_H

#include <QList>
#include <QPointer>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class QNetworkReply;

class S3SyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit S3SyncProvider(QObject* parent = nullptr);
    ~S3SyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void cancelAll() override;

private:
    RemoteSyncSettings m_settings;
    QList<QPointer<QNetworkReply>> m_activeReplies;
};

#endif // KEEPASSXC_S3SYNCPROVIDER_H
