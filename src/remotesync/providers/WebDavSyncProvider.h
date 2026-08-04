#ifndef KEEPASSXC_WEBDAVSYNCPROVIDER_H
#define KEEPASSXC_WEBDAVSYNCPROVIDER_H

#include <QBuffer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include "ISyncProvider.h"

class WebDavSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit WebDavSyncProvider(QObject* parent = nullptr);
    ~WebDavSyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void cancelAll() override;

private:
    void applyAuthHeader(QNetworkRequest& req, const RemoteSyncSettings& settings);
    SyncResult parseReply(QNetworkReply* reply);

    RemoteSyncSettings m_settings;
    QList<QPointer<QNetworkReply>> m_activeReplies;
};

#endif // KEEPASSXC_WEBDAVSYNCPROVIDER_H
