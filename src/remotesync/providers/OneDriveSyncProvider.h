#ifndef KEEPASSXC_ONEDRIVESYNCPROVIDER_H
#define KEEPASSXC_ONEDRIVESYNCPROVIDER_H

#include <QDateTime>
#include <QList>
#include <QPointer>
#include <QUrl>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class QNetworkReply;
class QNetworkRequest;

class OneDriveSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit OneDriveSyncProvider(QObject* parent = nullptr);
    ~OneDriveSyncProvider() override;

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
    QUrl buildItemUrl(const QString& normalizedPath) const;
    QUrl buildContentUrl(const QString& normalizedPath) const;
    void applyAuth(QNetworkRequest& req) const;
    void ensureAuthenticated(std::function<void(bool success, const QString& errorMsg)> callback);
    void refreshToken(std::function<void(bool success, const QString& errorMsg)> callback);

    RemoteSyncSettings m_settings;
    QList<QPointer<QNetworkReply>> m_activeReplies;
    QDateTime m_tokenExpiresAt;
};

#endif // KEEPASSXC_ONEDRIVESYNCPROVIDER_H
