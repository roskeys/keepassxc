#ifndef KEEPASSXC_GITSYNCPROVIDER_H
#define KEEPASSXC_GITSYNCPROVIDER_H

#include <functional>
#include <QList>
#include <QPointer>
#include <QProcess>
#include <QString>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class GitSyncProvider : public ISyncProvider
{
    Q_OBJECT

public:
    explicit GitSyncProvider(QObject* parent = nullptr);
    ~GitSyncProvider() override;

    void configure(const RemoteSyncSettings& settings) override;
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb) override;
    void fetchMetadata(const QString& remotePath, SyncCallback cb) override;
    void downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb) override;
    void uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb) override;
    void moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb) override;
    void cancelAll() override;

private:
    QString getLocalRepoDir() const;
    void runGitCommand(const QStringList& args,
                       const QString& workingDir,
                       std::function<void(bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr)> callback);

    void ensureCloned(std::function<void(bool success, const QString& errorMsg)> onReady);

    RemoteSyncSettings m_settings;
    QList<QPointer<QProcess>> m_activeProcesses;
};

#endif // KEEPASSXC_GITSYNCPROVIDER_H
