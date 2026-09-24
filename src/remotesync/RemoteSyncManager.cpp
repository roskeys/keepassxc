#include "RemoteSyncManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
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
    connect(&m_saveDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_db && m_state == SyncState::Idle) {
            pushDatabase();
        }
    });
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

void RemoteSyncManager::clearProviders()
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
}

void RemoteSyncManager::initProviders()
{
    clearProviders();

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

    // 2. Dropbox Provider
    if (m_settings.dropbox.enabled && !m_settings.dropbox.accessToken.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::Dropbox, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::Dropbox, QStringLiteral("Dropbox"), p, QString()});
    }

    // 3. Google Drive Provider
    if (m_settings.googleDrive.enabled && (!m_settings.googleDrive.accessToken.isEmpty() || !m_settings.googleDrive.refreshToken.isEmpty())) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::GoogleDrive, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::GoogleDrive, QStringLiteral("Google Drive"), p, QString()});
    }

    // 4. SFTP Provider
    if (m_settings.sftp.enabled && !m_settings.sftp.host.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::SFTP, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::SFTP, QStringLiteral("SFTP"), p, QString()});
    }

    // 4. S3 Provider
    if (m_settings.s3.enabled && !m_settings.s3.endpoint.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::S3, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::S3, QStringLiteral("S3"), p, QString()});
    }

    // 5. Git Provider
    if (m_settings.git.enabled && !m_settings.git.repoUrl.isEmpty()) {
        auto* p = SyncProviderFactory::create(RemoteSyncSettings::Protocol::Git, this);
        p->configure(m_settings);
        m_providers.append({RemoteSyncSettings::Protocol::Git, QStringLiteral("Git"), p, QString()});
    }

    if (!m_providers.isEmpty()) {
        startTimer();
    }
}

void RemoteSyncManager::reloadSettings()
{
    if (!m_db) {
        return;
    }

    initProviders();
}

void RemoteSyncManager::onDatabaseUnlocked(QSharedPointer<Database> db)
{
    onDatabaseLocked(); // clean up any previous instance

    m_db = db;
    if (!m_db) {
        return;
    }

    initProviders();
    if (!m_providers.isEmpty()) {
        pullAndMerge();
    }
}

void RemoteSyncManager::onDatabaseLocked()
{
    clearProviders();
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

    if (m_providers.isEmpty()) {
        initProviders();
    }

    if (m_providers.isEmpty()) {
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

void RemoteSyncManager::fullSync(std::function<void(bool success)> completion)
{
    if (!m_db) {
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_providers.isEmpty()) {
        initProviders();
    }

    if (m_providers.isEmpty()) {
        QString msg = tr("No remote sync providers configured.");
        emit syncStatusChanged(SyncState::Idle, msg);
        emit syncProgress(100, msg);
        QTimer::singleShot(2500, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_state == SyncState::Pulling || m_state == SyncState::Pushing) {
        if (completion) {
            completion(false);
        }
        return;
    }

    pullAndMerge([this, completion](bool pullOk) {
        Q_UNUSED(pullOk);
        if (!m_db || m_providers.isEmpty()) {
            if (completion) {
                completion(false);
            }
            return;
        }
        pushDatabase(completion);
    });
}

void RemoteSyncManager::pullAndMerge(std::function<void(bool success)> completion)
{
    if (!m_db) {
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_providers.isEmpty()) {
        initProviders();
    }

    if (m_providers.isEmpty()) {
        QString msg = tr("No remote sync providers configured.");
        emit syncStatusChanged(SyncState::Idle, msg);
        emit syncProgress(100, msg);
        QTimer::singleShot(2500, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_state == SyncState::Pulling || m_state == SyncState::Pushing) {
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
    case RemoteSyncSettings::Protocol::Dropbox:
        remotePath = m_settings.dropbox.remotePath;
        break;
    case RemoteSyncSettings::Protocol::SFTP:
        remotePath = m_settings.sftp.remotePath;
        break;
    case RemoteSyncSettings::Protocol::S3:
        remotePath = m_settings.s3.remotePath;
        break;
    case RemoteSyncSettings::Protocol::Git:
        remotePath = m_settings.git.remotePath.isEmpty() ? QFileInfo(m_db->filePath()).fileName() : m_settings.git.remotePath;
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

        auto* tempDir = new QTemporaryDir();
        if (!tempDir->isValid()) {
            delete tempDir;
            pullFromProviders(index + 1, completion);
            return;
        }
        QString tempPath = tempDir->filePath(QStringLiteral("remote_download.kdbx"));

        currentEntry.provider->downloadFile(remotePath, tempPath, [this, index, tempDir, tempPath, metaResult, completion](const SyncResult& dlResult) {
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

            delete tempDir;
            pullFromProviders(index + 1, completion);
        });
    });
}

void RemoteSyncManager::pushDatabase(std::function<void(bool success)> completion)
{
    if (!m_db) {
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_providers.isEmpty()) {
        initProviders();
    }

    if (m_providers.isEmpty()) {
        QString msg = tr("No remote sync providers configured.");
        emit syncStatusChanged(SyncState::Idle, msg);
        emit syncProgress(100, msg);
        QTimer::singleShot(2500, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(false);
        }
        return;
    }

    if (m_state == SyncState::Pulling || m_state == SyncState::Pushing) {
        if (completion) {
            completion(false);
        }
        return;
    }

    m_state = SyncState::Pushing;

    // Save modified in-memory database to disk first if it has a filePath
    if (m_db->isModified() && !m_db->filePath().isEmpty() && QFile::exists(m_db->filePath())) {
        QString saveErr;
        if (!m_db->save(Database::Atomic, QString(), &saveErr)) {
            qWarning() << "RemoteSyncManager: Failed to save modified database before push:" << saveErr;
        }
    }

    // Prepare database snapshot in a unique temporary directory
    auto* tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        m_state = SyncState::Idle;
        QString err = tr("Cannot create local temporary directory for sync: %1").arg(tempDir->errorString());
        emit syncStatusChanged(SyncState::Error, err);
        emit syncProgress(100, err);
        QTimer::singleShot(3000, this, [this]() { emit syncProgress(-1, QString()); });
        delete tempDir;
        if (completion) {
            completion(false);
        }
        return;
    }

    QString fileName = QFileInfo(m_db->filePath()).fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("database.kdbx");
    }
    QString localTempPath = tempDir->filePath(fileName);

    bool saved = false;
    QString copyError;
    if (!m_db->filePath().isEmpty() && QFile::exists(m_db->filePath())) {
        QFile srcFile(m_db->filePath());
        saved = srcFile.copy(localTempPath);
        if (!saved) {
            copyError = srcFile.errorString();
            qWarning() << "RemoteSyncManager: QFile::copy failed from" << m_db->filePath()
                       << "to" << localTempPath << ":" << copyError;
        }
    }

    if (!saved) {
        QString oldFilePath = m_db->filePath();
        QString saveAsErr;
        saved = m_db->saveAs(localTempPath, Database::DirectWrite, QString(), &saveAsErr);
        if (!oldFilePath.isEmpty()) {
            m_db->setFilePath(oldFilePath);
        }
        if (!saved) {
            if (copyError.isEmpty()) {
                copyError = saveAsErr;
            } else if (!saveAsErr.isEmpty()) {
                copyError += QStringLiteral("; ") + saveAsErr;
            }
            qWarning() << "RemoteSyncManager: Database::saveAs failed to" << localTempPath << ":" << saveAsErr;
        }
    }

    if (!saved) {
        m_state = SyncState::Idle;
        QString saveErr = tr("Failed to prepare database snapshot for upload");
        if (!copyError.isEmpty()) {
            saveErr += QStringLiteral(" (%1)").arg(copyError);
        }
        emit syncStatusChanged(SyncState::Error, saveErr);
        emit syncProgress(100, saveErr);
        QTimer::singleShot(3000, this, [this]() { emit syncProgress(-1, QString()); });
        delete tempDir;
        if (completion) {
            completion(false);
        }
        return;
    }

    auto failedTargets = std::make_shared<QStringList>();
    auto succeededTargets = std::make_shared<QStringList>();
    pushToProviders(0, localTempPath, [this, tempDir, failedTargets, succeededTargets, completion](bool) {
        delete tempDir;
        m_state = SyncState::Idle;

        if (failedTargets->isEmpty()) {
            QString statusMsg = tr("Sync finished: %1 up to date").arg(succeededTargets->join(QStringLiteral(", ")));
            emit syncStatusChanged(m_state, statusMsg);
            emit syncProgress(100, statusMsg);
        } else {
            QString statusMsg = tr("Sync failed on: %1").arg(failedTargets->join(QStringLiteral("; ")));
            emit syncStatusChanged(SyncState::Error, statusMsg);
            emit syncProgress(100, statusMsg);
        }

        QTimer::singleShot(3000, this, [this]() { emit syncProgress(-1, QString()); });
        if (completion) {
            completion(failedTargets->isEmpty());
        }
    }, failedTargets, succeededTargets);
}

void RemoteSyncManager::pushToProviders(int index,
                                        const QString& localTempPath,
                                        std::function<void(bool success)> completion,
                                        std::shared_ptr<QStringList> failedTargets,
                                        std::shared_ptr<QStringList> succeededTargets)
{
    if (index >= m_providers.size() || !m_db) {
        if (completion) {
            completion(failedTargets ? failedTargets->isEmpty() : true);
        }
        return;
    }

    auto& entry = m_providers[index];
    int startPct = (index * 100) / m_providers.size();
    QString progressMsg = tr("[%1] Uploading database (%2/%3)...").arg(entry.name).arg(index + 1).arg(m_providers.size());
    emit syncStatusChanged(m_state, progressMsg);
    emit syncProgress(startPct, progressMsg);

    QString remotePath;
    switch (entry.protocol) {
    case RemoteSyncSettings::Protocol::Dropbox:
        remotePath = m_settings.dropbox.remotePath;
        break;
    case RemoteSyncSettings::Protocol::SFTP:
        remotePath = m_settings.sftp.remotePath;
        break;
    case RemoteSyncSettings::Protocol::S3:
        remotePath = m_settings.s3.remotePath;
        break;
    case RemoteSyncSettings::Protocol::Git:
        remotePath = m_settings.git.remotePath.isEmpty() ? QFileInfo(m_db->filePath()).fileName() : m_settings.git.remotePath;
        break;
    case RemoteSyncSettings::Protocol::WebDAV:
    default:
        remotePath = m_settings.webdav.fullRemoteUrl(QFileInfo(m_db->filePath()).fileName());
        break;
    }

    QString remoteTmpPath = remotePath + QStringLiteral(".tmp.") + QUuid::createUuid().toString(QUuid::WithoutBraces);

    entry.provider->uploadFile(localTempPath, remoteTmpPath, [this, index, localTempPath, remoteTmpPath, remotePath, completion, failedTargets, succeededTargets](const SyncResult& upResult) {
        if (!upResult.isSuccess()) {
            if (failedTargets) {
                failedTargets->append(QStringLiteral("%1 (upload: %2)").arg(m_providers[index].name, upResult.errorMessage));
            }
            emit syncStatusChanged(SyncState::Error, tr("[%1] Upload failed: %2").arg(m_providers[index].name, upResult.errorMessage));
            pushToProviders(index + 1, localTempPath, completion, failedTargets, succeededTargets);
            return;
        }

        m_providers[index].provider->moveFile(remoteTmpPath, remotePath, [this, index, localTempPath, completion, failedTargets, succeededTargets](const SyncResult& mvResult) {
            if (mvResult.isSuccess()) {
                m_providers[index].lastPushedETag.clear();
                if (succeededTargets) {
                    succeededTargets->append(m_providers[index].name);
                }
                int endPct = ((index + 1) * 100) / m_providers.size();
                QString okMsg = tr("[%1] Pushed successfully (%2/%3)").arg(m_providers[index].name).arg(index + 1).arg(m_providers.size());
                emit syncProgress(endPct, okMsg);
            } else {
                if (failedTargets) {
                    failedTargets->append(QStringLiteral("%1 (commit: %2)").arg(m_providers[index].name, mvResult.errorMessage));
                }
                emit syncStatusChanged(SyncState::Error, tr("[%1] Commit failed: %2").arg(m_providers[index].name, mvResult.errorMessage));
            }
            pushToProviders(index + 1, localTempPath, completion, failedTargets, succeededTargets);
        });
    });
}
