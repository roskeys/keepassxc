#ifndef KEEPASSXC_ISYNCPROVIDER_H
#define KEEPASSXC_ISYNCPROVIDER_H

#include <QObject>
#include "RemoteSyncSettings.h"
#include "SyncResult.h"

class ISyncProvider : public QObject
{
    Q_OBJECT

public:
    explicit ISyncProvider(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ISyncProvider() override = default;

    virtual void configure(const RemoteSyncSettings& settings) = 0;
    virtual void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) = 0;
    virtual void fetchMetadata(const QString& remotePath, SyncCallback cb) = 0;
    virtual void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) = 0;
    virtual void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) = 0;
    virtual void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) = 0;
    virtual void cancelAll() = 0;
};

#endif // KEEPASSXC_ISYNCPROVIDER_H
