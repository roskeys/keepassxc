#include "RemoteSyncManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUuid>
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
    if (!m_settings.isAnyEnabled()) {
        return;
    }

    // 1. WebDAV Provider
    if (m_settings.webdav.enabled && !m_settings.webdav.url.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::WebDAV, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::WebDAV, QStringLiteral("WebDAV"), p, QString()});
    }

    // 2. SFTP Provider
    if (m_settings.sftp.enabled && !m_settings.sftp.host.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::SFTP, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::SFTP, QStringLiteral("SFTP"), p, QString()});
    }

    // 3. S3 Provider
    if (m_settings.s3.enabled && !m_settings.s3.endpoint.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::S3, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::S3, QStringLiteral("S3"), p, QString()});
    }

    if (m_providers.isEmpty()) {
        return;
    }

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

    for (auto& entry : m_providers) {
        if (entry.provider) {
            entry.provider->cancelAll();
            entry.provider->deleteLater();
        }
    }
    m_providers.clear();

    m_db.clear();
    m_state = SyncState::Idle;
    m_pushInProgress = false;
}

void RemoteSyncManager::onDatabaseSaved(QSharedPointer<Database> db)
{
    if (!m_db || m_db != db) {
        return;
    }

    if (m_pushInProgress) {
        return;
    }

    m_settings = RemoteSyncSettings::fromDatabase(m_db.data());
    if (!m_settings.isAnyEnabled()) {
        return;
    }

    m_saveDebounceTimer.start();
}

void RemoteSyncManager::startTimer()
{
    int interval = m_settings.intervalSeconds;
    if (interval < 60) {
        interval = 300;
    }
    m_syncTimer.setInterval(interval * 1000);
    m_syncTimer.start();
}

void RemoteSyncManager::stopTimer()
{
    m_syncTimer.stop();
}

void RemoteSyncManager::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    auto* provider = SyncProviderFactory::create(settings.protocol, this);
    provider->testConnection(settings, [provider, cb](const SyncResult& result) {
        if (cb) {
            cb(result);
        }
        provider->deleteLater();
    });
}

void RemoteSyncManager::pullAndMerge(std::function<void(bool success)> completion)
{
    if (m_state != SyncState::Idle || m_providers.isEmpty() || !m_db) {
        if (completion) {
            completion(false);
        }
        return;
    }

    m_state = SyncState::Pulling;
    emit syncStatusChanged(m_state, tr("Checking for remote updates..."));
    emit syncProgress(0, tr("Checking for remote updates..."));

    pullFromProviders(0, completion);
}

void RemoteSyncManager::pullFromProviders(int index, std::function<void(bool success)> completion)
{
    if (index >= m_providers.size() || !m_db) {
        m_state = SyncState::Idle;
        emit syncStatusChanged(m_state, tr("Remote sync up-to-date"));
        emit syncProgress(100, tr("Remote sync up-to-date"));
        QTimer::singleShot(2500, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(true);
        }
        return;
    }

    auto& entry = m_providers[index];
    int pct = 10 + (index * 80) / m_providers.size();
    emit syncStatusChanged(m_state, tr("Syncing with %1...").arg(entry.name));
    emit syncProgress(pct, tr("Syncing with %1...").arg(entry.name));

    QString remotePath;
    switch (entry.protocol) {
    case RemoteSyncSettings::Protocol::SFTP:
        remotePath = m_settings.sftp.remotePath;
        break;
    case RemoteSyncSettings::Protocol::S3:
        remotePath = m_settings.s3.remotePath;
        break;
    case RemoteSyncSettings::Protocol::WebDAV:
    default:
        remotePath = m_settings.webdav.fullRemoteUrl(QFileInfo(m_db->filePath()).fileName());
        break;
    }

    entry.provider->fetchMetadata(remotePath, [this, index, remotePath, completion](const SyncResult& metaResult) {
        if (!m_db || m_state != SyncState::Pulling) {
            return;
        }

        auto& currentEntry = m_providers[index];

        if (metaResult.status == SyncResult::Status::NotFound) {
            // Remote file doesn't exist yet, continue to next provider
            pullFromProviders(index + 1, completion);
            return;
        }

        if (!metaResult.isSuccess()) {
            emit syncStatusChanged(SyncState::Error, tr("%1 sync check failed: %2").arg(currentEntry.name, metaResult.errorMessage));
            pullFromProviders(index + 1, completion);
            return;
        }

        if (!currentEntry.lastPushedETag.isEmpty() && currentEntry.lastPushedETag == metaResult.etag) {
            // Remote file hasn't changed since last push
            pullFromProviders(index + 1, completion);
            return;
        }

        auto* tempFile = new QTemporaryFile(this);
        if (!tempFile->open()) {
            delete tempFile;
            pullFromProviders(index + 1, completion);
            return;
        }
        QString tempPath = tempFile->fileName();
        tempFile->close();

        currentEntry.provider->downloadFile(remotePath, tempPath, [this, index, tempFile, tempPath, metaResult, completion](const SyncResult& dlResult) {
            auto& dlEntry = m_providers[index];

            if (dlResult.isSuccess() && m_db) {
                QSharedPointer<Database> remoteDb(new Database());
                QString errorStr;
                if (remoteDb->open(tempPath, m_db->key(), &errorStr)) {
                    m_pushInProgress = true;
                    Merger merger(remoteDb.data(), m_db.data());
                    merger.merge();
                    m_pushInProgress = false;

                    dlEntry.lastPushedETag = metaResult.etag;
                    emit syncStatusChanged(m_state, tr("Merged updates from %1").arg(dlEntry.name));
                }
            }

            delete tempFile;
            pullFromProviders(index + 1, completion);
        });
    });
}

void RemoteSyncManager::pushDatabase(std::function<void(bool success)> completion)
{
    if (m_state != SyncState::Idle || m_providers.isEmpty() || !m_db) {
        if (completion) {
            completion(false);
        }
        return;
    }

    QStringList targetNames;
    for (const auto& p : m_providers) {
        targetNames << p.name;
    }

    m_state = SyncState::Pushing;
    QString startMsg = tr("Pushing database to %1...").arg(targetNames.join(QStringLiteral(", ")));
    emit syncStatusChanged(m_state, startMsg);
    emit syncProgress(0, startMsg);

    // Save database to a temporary local file
    auto* localTemp = new QTemporaryFile(this);
    if (!localTemp->open()) {
        m_state = SyncState::Error;
        delete localTemp;
        if (completion) {
            completion(false);
        }
        return;
    }

    QString localTempPath = localTemp->fileName();
    localTemp->close();

    bool saved = false;
    if (QFile::exists(m_db->filePath())) {
        saved = QFile::copy(m_db->filePath(), localTempPath);
    } else {
        saved = m_db->saveAs(localTempPath, Database::DirectWrite);
    }

    if (!saved) {
        m_state = SyncState::Error;
        delete localTemp;
        if (completion) {
            completion(false);
        }
        return;
    }

    auto failedTargets = std::make_shared<QStringList>();
    pushToProviders(0, localTempPath, [this, localTemp, failedTargets, completion](bool) {
        delete localTemp;
        m_state = SyncState::Idle;

        if (failedTargets->isEmpty()) {
            QString statusMsg = tr("Successfully synced to remote target(s)");
            emit syncStatusChanged(m_state, statusMsg);
            emit syncProgress(100, statusMsg);
        } else {
            QString statusMsg = tr("Sync completed with errors on: %1").arg(failedTargets->join(QStringLiteral(", ")));
            emit syncStatusChanged(SyncState::Error, statusMsg);
            emit syncProgress(100, statusMsg);
        }

        QTimer::singleShot(3000, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(failedTargets->isEmpty());
        }
    }, failedTargets);
}

void RemoteSyncManager::pushToProviders(int index,
                                        const QString& localTempPath,
                                        std::function<void(bool success)> completion,
                                        std::shared_ptr<QStringList> failedTargets)
{
    if (index >= m_providers.size() || !m_db) {
        if (completion) {
            completion(failedTargets ? failedTargets->isEmpty() : true);
        }
        return;
    }

    auto& entry = m_providers[index];
    int pct = 10 + (index * 80) / m_providers.size();
    QString progressMsg = tr("Pushing to %1 (%2 of %3)...").arg(entry.name).arg(index + 1).arg(m_providers.size());
    emit syncStatusChanged(m_state, progressMsg);
    emit syncProgress(pct, progressMsg);

    QString remotePath;
    switch (entry.protocol) {
    case RemoteSyncSettings::Protocol::SFTP:
        remotePath = m_settings.sftp.remotePath;
        break;
    case RemoteSyncSettings::Protocol::S3:
        remotePath = m_settings.s3.remotePath;
        break;
    case RemoteSyncSettings::Protocol::WebDAV:
    default:
        remotePath = m_settings.webdav.fullRemoteUrl(QFileInfo(m_db->filePath()).fileName());
        break;
    }

    QString remoteTmpPath = remotePath + QStringLiteral(".tmp.") + QUuid::createUuid().toString(QUuid::WithoutBraces);

    entry.provider->uploadFile(localTempPath, remoteTmpPath, [this, index, localTempPath, remoteTmpPath, remotePath, completion, failedTargets](const SyncResult& upResult) {
        if (!upResult.isSuccess()) {
            if (failedTargets) {
                failedTargets->append(QStringLiteral("%1 (upload: %2)").arg(m_providers[index].name, upResult.errorMessage));
            }
            emit syncStatusChanged(SyncState::Error, tr("Upload failed for %1: %2").arg(m_providers[index].name, upResult.errorMessage));
            pushToProviders(index + 1, localTempPath, completion, failedTargets);
            return;
        }

        m_providers[index].provider->moveFile(remoteTmpPath, remotePath, [this, index, localTempPath, completion, failedTargets](const SyncResult& mvResult) {
            if (mvResult.isSuccess()) {
                m_providers[index].lastPushedETag.clear();
            } else {
                if (failedTargets) {
                    failedTargets->append(QStringLiteral("%1 (commit: %2)").arg(m_providers[index].name, mvResult.errorMessage));
                }
                emit syncStatusChanged(SyncState::Error, tr("Commit failed for %1: %2").arg(m_providers[index].name, mvResult.errorMessage));
            }
            pushToProviders(index + 1, localTempPath, completion, failedTargets);
        });
    });
}
