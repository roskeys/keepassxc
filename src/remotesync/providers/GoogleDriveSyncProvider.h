#ifndef KEEPASSXC_GOOGLEDRIVESYNCPROVIDER_H
#define KEEPASSXC_GOOGLEDRIVESYNCPROVIDER_H

#include <QHash>
#include <QList>
#include <QPointer>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class QNetworkReply;
class QNetworkRequest;

class GoogleDriveSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit GoogleDriveSyncProvider(QObject* parent = nullptr);
    ~GoogleDriveSyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void cancelAll() override;

private:
    QString normalizeFileName(const QString& rawPath) const;
    void applyAuth(QNetworkRequest& req) const;
    void refreshTokenIfNeeded(std::function<void(bool success, const QString& errorMsg)> callback);
    void resolveFileId(const QString& fileName, std::function<void(bool found, const QString& fileId, const QString& errorMsg)> callback);

    RemoteSyncSettings m_settings;
    QList<QPointer<QNetworkReply>> m_activeReplies;
    QHash<QString, QString> m_fileIdCache;
};

#endif // KEEPASSXC_GOOGLEDRIVESYNCPROVIDER_H
