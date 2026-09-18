#ifndef KEEPASSXC_REMOTESYNCMANAGER_H
#define KEEPASSXC_REMOTESYNCMANAGER_H

#include <memory>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>
#include <QTimer>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class Database;

class RemoteSyncManager : public QObject
{
    Q_OBJECT

public:
    enum class SyncState
    {
        Idle,
        Pulling,
        Pushing,
        Error
    };

    explicit RemoteSyncManager(QObject* parent = nullptr);
    ~RemoteSyncManager() override;

    SyncState state() const;
    bool isPushInProgress() const;

public slots:
    void onDatabaseUnlocked(QSharedPointer<Database> db);
    void onDatabaseLocked();
    void onDatabaseSaved(QSharedPointer<Database> db);

    void pullAndMerge(std::function<void(bool success)> completion = nullptr);
    void pushDatabase(std::function<void(bool success)> completion = nullptr);
    void testConnection(const RemoteSyncSettings& settings, SyncCallback cb);

signals:
    void syncStatusChanged(RemoteSyncManager::SyncState state, const QString& message);
    void syncProgress(int progress, const QString& message);

private:
    struct ProviderEntry
    {
        RemoteSyncSettings::Protocol protocol;
        QString name;
        ISyncProvider* provider = nullptr;
        QString lastPushedETag;
    };

    void startTimer();
    void stopTimer();
    void pullFromProviders(int index, std::function<void(bool success)> completion);
    void pushToProviders(int index,
                         const QString& localTempPath,
                         std::function<void(bool success)> completion,
                         std::shared_ptr<QStringList> failedTargets = nullptr,
                         std::shared_ptr<QStringList> succeededTargets = nullptr);

    QSharedPointer<Database> m_db;
    RemoteSyncSettings m_settings;
    QList<ProviderEntry> m_providers;
    SyncState m_state = SyncState::Idle;
    bool m_pushInProgress = false;
    QTimer m_syncTimer;
    QTimer m_saveDebounceTimer;
};

#endif // KEEPASSXC_REMOTESYNCMANAGER_H
