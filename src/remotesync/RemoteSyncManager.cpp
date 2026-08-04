#include "RemoteSyncManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include "SyncProviderFactory.h"
#include "core/Database.h"
#include "core/Merger.h"

RemoteSyncManager::RemoteSyncManager(QObject* parent)
    : QObject(parent)
{
    m_syncTimer.setSingleShot(false);
    connect(&m_syncTimer, &QTimer::timeout, this, [this]() {
        if (m_state == SyncState::Idle && m_db) {
            pullAndMerge();
        }
    });

    m_saveDebounceTimer.setSingleShot(true);
    m_saveDebounceTimer.setInterval(2000);
}

RemoteSyncManager::~RemoteSyncManager()
{
    onDatabaseLocked();
}

RemoteSyncManager::SyncState RemoteSyncManager::state() const
{
    return m_state;
}

bool RemoteSyncManager::isPushInProgress() const
{
    return m_pushInProgress;
}

void RemoteSyncManager::onDatabaseUnlocked(QSharedPointer<Database> db)
{
    onDatabaseLocked(); // clean up any previous instance

    m_db = db;
    if (!m_db) {
        return;
    }

    m_settings = RemoteSyncSettings::fromDatabase(m_db.data());
    if (!m_settings.enabled || m_settings.url.isEmpty()) {
        return;
    }

    m_provider = SyncProviderFactory::create(m_settings.protocol, this);
    m_provider->configure(m_settings);

    connect(&m_saveDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_db && m_state == SyncState::Idle) {
            pushDatabase();
        }
    });

    startTimer();
    pullAndMerge();
}

void RemoteSyncManager::onDatabaseLocked()
{
    stopTimer();
    m_saveDebounceTimer.stop();

    if (m_provider) {
        m_provider->cancelAll();
        m_provider->deleteLater();
        m_provider = nullptr;
    }

    m_db.clear();
    m_state = SyncState::Idle;
    m_pushInProgress = false;
    m_lastPushedETag.clear();
}

void RemoteSyncManager::onDatabaseSaved(QSharedPointer<Database> db)
{
    if (!m_db || m_db != db) {
        return;
    }

    if (m_pushInProgress) {
        return; // Ignore saves triggered by sync manager itself
    }

    m_settings = RemoteSyncSettings::fromDatabase(m_db.data());
    if (!m_settings.enabled || m_settings.url.isEmpty()) {
        return;
    }

    // Debounce save pushes by 2 seconds
    m_saveDebounceTimer.start();
}

void RemoteSyncManager::startTimer()
{
    if (m_settings.enabled && m_settings.intervalSeconds >= 30) {
        m_syncTimer.start(m_settings.intervalSeconds * 1000);
    }
}

void RemoteSyncManager::stopTimer()
{
    m_syncTimer.stop();
}

void RemoteSyncManager::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    auto* tempProvider = SyncProviderFactory::create(settings.protocol, this);
    tempProvider->configure(settings);
    tempProvider->testConnection(settings, [tempProvider, cb](const SyncResult& res) {
        tempProvider->deleteLater();
        if (cb) cb(res);
    });
}

void RemoteSyncManager::pullAndMerge(std::function<void(bool success)> completion)
{
    if (!m_db || !m_provider || m_state != SyncState::Idle) {
        if (completion) completion(false);
        return;
    }

    QString targetFullUrl = m_settings.fullRemoteUrl(QFileInfo(m_db->filePath()).fileName());
    if (targetFullUrl.isEmpty()) {
        if (completion) completion(false);
        return;
    }

    m_state = SyncState::Pulling;
    emit syncStatusChanged(m_state, tr("Checking remote database status…"));
    emit syncProgress(0, tr("Syncing remote database…"));

    m_provider->fetchMetadata(targetFullUrl, [this, targetFullUrl, completion](const SyncResult& metaRes) {
        if (m_state != SyncState::Pulling) {
            if (completion) completion(false);
            return;
        }

        if (!metaRes.isSuccess()) {
            if (metaRes.status == SyncResult::Status::NotFound) {
                // Remote file does not exist yet -> push local database
                m_state = SyncState::Idle;
                pushDatabase(completion);
                return;
            }

            m_state = SyncState::Error;
            emit syncStatusChanged(m_state, tr("Remote check failed: %1").arg(metaRes.errorMessage));
            emit syncProgress(100, tr("Sync failed"));
            if (completion) completion(false);
            return;
        }

        // Check if ETag matches last pushed version
        if (!m_lastPushedETag.isEmpty() && metaRes.etag == m_lastPushedETag) {
            m_state = SyncState::Idle;
            emit syncStatusChanged(m_state, tr("Database up to date"));
            emit syncProgress(100, tr("Database up to date"));
            if (completion) completion(true);
            return;
        }

        // Remote file updated -> download to temporary file
        QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/kpxc_remotesync_XXXXXX.kdbx"));
        tempFile.setAutoRemove(false);
        if (!tempFile.open()) {
            m_state = SyncState::Error;
            emit syncStatusChanged(m_state, tr("Failed to create temporary file for sync"));
            if (completion) completion(false);
            return;
        }
        QString tempPath = tempFile.fileName();
        tempFile.close();

        m_provider->downloadFile(targetFullUrl, tempPath, [this, tempPath, metaRes, completion](const SyncResult& dlRes) {
            if (m_state != SyncState::Pulling) {
                QFile::remove(tempPath);
                if (completion) completion(false);
                return;
            }

            if (!dlRes.isSuccess()) {
                QFile::remove(tempPath);
                m_state = SyncState::Error;
                emit syncStatusChanged(m_state, tr("Failed to download remote database: %1").arg(dlRes.errorMessage));
                emit syncProgress(100, tr("Sync failed"));
                if (completion) completion(false);
                return;
            }

            // Open downloaded remote database with local database composite key
            auto remoteDb = QSharedPointer<Database>::create(tempPath);
            if (!remoteDb->open(m_db->key())) {
                QFile::remove(tempPath);
                m_state = SyncState::Error;
                emit syncStatusChanged(m_state, tr("Failed to decrypt downloaded database"));
                emit syncProgress(100, tr("Sync failed"));
                if (completion) completion(false);
                return;
            }

            // Ignore local file watcher during merge
            m_db->setIgnoreFileChangesUntilSaved(true);

            // Execute Merger: source (remote) -> target (local)
            Merger merger(remoteDb.data(), m_db.data());
            merger.setSkipDatabaseCustomData(true);
            auto changes = merger.merge();

            m_db->setIgnoreFileChangesUntilSaved(false);
            QFile::remove(tempPath);

            if (!metaRes.etag.isEmpty()) {
                m_lastPushedETag = metaRes.etag;
            } else if (!dlRes.etag.isEmpty()) {
                m_lastPushedETag = dlRes.etag;
            }

            m_state = SyncState::Idle;
            emit syncStatusChanged(m_state, tr("Sync complete"));
            emit syncProgress(100, tr("Sync complete"));

            if (!changes.isEmpty()) {
                m_db->markAsModified();
            }

            if (completion) completion(true);
        });
    });
}

void RemoteSyncManager::pushDatabase(std::function<void(bool success)> completion)
{
    if (!m_db || !m_provider || m_state != SyncState::Idle) {
        if (completion) completion(false);
        return;
    }

    QString targetFullUrl = m_settings.fullRemoteUrl(QFileInfo(m_db->filePath()).fileName());
    if (targetFullUrl.isEmpty()) {
        if (completion) completion(false);
        return;
    }

    m_state = SyncState::Pushing;
    m_pushInProgress = true;

    emit syncStatusChanged(m_state, tr("Uploading database to remote server…"));
    emit syncProgress(0, tr("Uploading remote database…"));

    QString remoteTmpUrl = targetFullUrl + QStringLiteral(".tmp");

    m_provider->uploadFile(m_db->filePath(), remoteTmpUrl, [this, targetFullUrl, remoteTmpUrl, completion](const SyncResult& upRes) {
        if (m_state != SyncState::Pushing) {
            m_pushInProgress = false;
            if (completion) completion(false);
            return;
        }

        if (!upRes.isSuccess()) {
            m_state = SyncState::Error;
            m_pushInProgress = false;
            emit syncStatusChanged(m_state, tr("Upload failed: %1").arg(upRes.errorMessage));
            emit syncProgress(100, tr("Upload failed"));
            if (completion) completion(false);
            return;
        }

        // Send MOVE command: remoteTmpUrl -> targetFullUrl
        m_provider->moveFile(remoteTmpUrl, targetFullUrl, [this, completion](const SyncResult& moveRes) {
            m_pushInProgress = false;

            if (!moveRes.isSuccess()) {
                m_state = SyncState::Error;
                emit syncStatusChanged(m_state, tr("Atomic move failed: %1").arg(moveRes.errorMessage));
                emit syncProgress(100, tr("Move failed"));
                if (completion) completion(false);
                return;
            }

            if (!moveRes.etag.isEmpty()) {
                m_lastPushedETag = moveRes.etag;
            }

            m_state = SyncState::Idle;
            emit syncStatusChanged(m_state, tr("Upload complete"));
            emit syncProgress(100, tr("Upload complete"));
            if (completion) completion(true);
        });
    });
}
