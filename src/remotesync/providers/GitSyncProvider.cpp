#include "GitSyncProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>

GitSyncProvider::GitSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

GitSyncProvider::~GitSyncProvider()
{
    cancelAll();
}

void GitSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void GitSyncProvider::cancelAll()
{
    for (auto proc : m_activeProcesses) {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->deleteLater();
        }
    }
    m_activeProcesses.clear();
}

QString GitSyncProvider::getLocalRepoDir() const
{
    const auto& g = m_settings.git;
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/git_sync_repos");
    QByteArray hash = QCryptographicHash::hash(g.repoUrl.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return baseDir + QStringLiteral("/") + QString::fromUtf8(hash);
}

void GitSyncProvider::runGitCommand(const QStringList& args,
                                    const QString& workingDir,
                                    std::function<void(bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr)> callback)
{
    const auto& g = m_settings.git;
    auto* proc = new QProcess(this);
    m_activeProcesses.append(proc);

    if (!workingDir.isEmpty()) {
        proc->setWorkingDirectory(workingDir);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));

    QTemporaryFile* askpassScript = nullptr;
    QTemporaryFile* pwdFile = nullptr;

    if (!g.password.isEmpty()) {
        pwdFile = new QTemporaryFile(this);
        if (pwdFile->open()) {
            pwdFile->write(g.password.toUtf8());
            pwdFile->close();

            askpassScript = new QTemporaryFile(this);
            if (askpassScript->open()) {
                QString scriptContent = QStringLiteral("#!/bin/sh\ncat \"%1\"\n").arg(pwdFile->fileName());
                askpassScript->write(scriptContent.toUtf8());
                askpassScript->close();
                QFile::setPermissions(askpassScript->fileName(),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

                env.insert(QStringLiteral("GIT_ASKPASS"), askpassScript->fileName());
                env.insert(QStringLiteral("SSH_ASKPASS"), askpassScript->fileName());
                env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
                if (!env.contains(QStringLiteral("DISPLAY"))) {
                    env.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
                }
            }
        }
    }

    // Configure SSH command if custom key is specified
    if (!g.keyPath.isEmpty()) {
        QString sshCmd = QStringLiteral("ssh -i \"%1\" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new").arg(g.keyPath);
        env.insert(QStringLiteral("GIT_SSH_COMMAND"), sshCmd);
    } else {
        env.insert(QStringLiteral("GIT_SSH_COMMAND"), QStringLiteral("ssh -o StrictHostKeyChecking=accept-new"));
    }

    // Configure git author
    QString author = g.authorName.isEmpty() ? QStringLiteral("KeePassXC RemoteSync") : g.authorName;
    QString email = g.authorEmail.isEmpty() ? QStringLiteral("keepassxc@localhost") : g.authorEmail;
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), author);
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), email);
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), author);
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), email);

    proc->setProcessEnvironment(env);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, askpassScript, pwdFile, callback](int exitCode, QProcess::ExitStatus exitStatus) {
                m_activeProcesses.removeAll(proc);
                QString stdoutStr = QString::fromUtf8(proc->readAllStandardOutput());
                QString stderrStr = QString::fromUtf8(proc->readAllStandardError());
                bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);

                if (callback) {
                    callback(ok, exitCode, stdoutStr, stderrStr);
                }

                proc->deleteLater();
                if (askpassScript) {
                    delete askpassScript;
                }
                if (pwdFile) {
                    delete pwdFile;
                }
            });

    proc->start(QStringLiteral("git"), args);
}

void GitSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    configure(settings);
    const auto& g = settings.git;
    if (g.repoUrl.trimmed().isEmpty()) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Git repository URL is empty");
        if (cb) cb(res);
        return;
    }

    // Use `git ls-remote` to check connection and credentials without downloading the entire repo
    QString repoUrl = g.repoUrl.trimmed();
    if (!g.username.isEmpty() && repoUrl.startsWith(QLatin1String("https://"))) {
        // Embed username if provided
        repoUrl.insert(8, g.username + QLatin1Char('@'));
    }

    QStringList args = {QStringLiteral("ls-remote"), QStringLiteral("--heads"), repoUrl};
    if (!g.branch.isEmpty()) {
        args << g.branch.trimmed();
    }

    runGitCommand(args, QString(), [cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
        Q_UNUSED(exitCode);
        Q_UNUSED(stdoutStr);
        SyncResult res;
        if (success) {
            res.status = SyncResult::Status::Success;
        } else {
            if (stderrStr.contains(QLatin1String("Permission denied"), Qt::CaseInsensitive)
                || stderrStr.contains(QLatin1String("Authentication failed"), Qt::CaseInsensitive)) {
                res.status = SyncResult::Status::AuthError;
            } else {
                res.status = SyncResult::Status::NetworkError;
            }
            res.errorMessage = stderrStr.trimmed();
        }
        if (cb) cb(res);
    });
}

void GitSyncProvider::ensureCloned(std::function<void(bool success, const QString& errorMsg)> onReady)
{
    QString repoDir = getLocalRepoDir();
    QDir dir(repoDir);

    const auto& g = m_settings.git;
    QString branch = g.branch.trimmed().isEmpty() ? QStringLiteral("main") : g.branch.trimmed();

    if (dir.exists() && QFile::exists(repoDir + QStringLiteral("/.git"))) {
        // Already cloned, perform fetch and pull
        runGitCommand({QStringLiteral("fetch"), QStringLiteral("origin")}, repoDir, [this, repoDir, branch, onReady](bool fetchOk, int, const QString&, const QString& fetchErr) {
            if (!fetchOk) {
                if (onReady) onReady(false, fetchErr);
                return;
            }

            // Checkout branch
            runGitCommand({QStringLiteral("checkout"), branch}, repoDir, [this, repoDir, branch, onReady](bool, int, const QString&, const QString&) {
                runGitCommand({QStringLiteral("pull"), QStringLiteral("origin"), branch}, repoDir, [onReady](bool pullOk, int, const QString&, const QString& pullErr) {
                    if (onReady) onReady(pullOk, pullOk ? QString() : pullErr);
                });
            });
        });
        return;
    }

    // Need to clone
    QDir().mkpath(QFileInfo(repoDir).dir().absolutePath());

    QString repoUrl = g.repoUrl.trimmed();
    if (!g.username.isEmpty() && repoUrl.startsWith(QLatin1String("https://"))) {
        repoUrl.insert(8, g.username + QLatin1Char('@'));
    }

    QStringList cloneArgs = {QStringLiteral("clone"), repoUrl, repoDir};
    runGitCommand(cloneArgs, QString(), [this, repoDir, branch, onReady](bool cloneOk, int, const QString&, const QString& cloneErr) {
        if (!cloneOk) {
            if (onReady) onReady(false, cloneErr);
            return;
        }

        // Switch to branch if specified
        runGitCommand({QStringLiteral("checkout"), branch}, repoDir, [onReady](bool, int, const QString&, const QString&) {
            if (onReady) onReady(true, QString());
        });
    });
}

void GitSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    ensureCloned([this, remotePath, cb](bool ready, const QString& errorMsg) {
        if (!ready) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = errorMsg;
            if (cb) cb(res);
            return;
        }

        QString repoDir = getLocalRepoDir();
        QString path = remotePath.trimmed();
        if (path.isEmpty()) {
            path = m_settings.git.remotePath.trimmed();
        }
        if (path.isEmpty()) {
            path = QStringLiteral("passwords.kdbx");
        }
        if (path.startsWith(QLatin1Char('/'))) {
            path.remove(0, 1);
        }

        QString targetFilePath = repoDir + QStringLiteral("/") + path;
        QFileInfo fi(targetFilePath);

        if (!fi.exists() || !fi.isFile()) {
            SyncResult res;
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("Remote file does not exist in repository: ") + path;
            if (cb) cb(res);
            return;
        }

        // Get latest commit hash for this file
        runGitCommand({QStringLiteral("log"), QStringLiteral("-n"), QStringLiteral("1"), QStringLiteral("--format=%H"), QStringLiteral("--"), path},
                      repoDir,
                      [fi, cb](bool logOk, int, const QString& stdoutStr, const QString&) {
                          SyncResult res;
                          res.status = SyncResult::Status::Success;
                          res.etag = logOk ? stdoutStr.trimmed() : QString::number(fi.lastModified().toMSecsSinceEpoch());
                          res.lastModified = fi.lastModified();
                          if (cb) cb(res);
                      });
    });
}

void GitSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    ensureCloned([this, remotePath, localTempPath, cb](bool ready, const QString& errorMsg) {
        if (!ready) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = errorMsg;
            if (cb) cb(res);
            return;
        }

        QString repoDir = getLocalRepoDir();
        QString path = remotePath.trimmed();
        if (path.isEmpty()) {
            path = m_settings.git.remotePath.trimmed();
        }
        if (path.isEmpty()) {
            path = QStringLiteral("passwords.kdbx");
        }
        if (path.startsWith(QLatin1Char('/'))) {
            path.remove(0, 1);
        }

        QString targetFilePath = repoDir + QStringLiteral("/") + path;
        if (!QFile::exists(targetFilePath)) {
            SyncResult res;
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("File not found in git repository");
            if (cb) cb(res);
            return;
        }

        QFile::remove(localTempPath);
        bool copied = QFile::copy(targetFilePath, localTempPath);

        SyncResult res;
        if (copied) {
            res.status = SyncResult::Status::Success;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = QStringLiteral("Failed to copy downloaded file to temporary path");
        }
        if (cb) cb(res);
    });
}

void GitSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    ensureCloned([this, localPath, remoteTmpPath, cb](bool ready, const QString& errorMsg) {
        if (!ready) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = errorMsg;
            if (cb) cb(res);
            return;
        }

        QString repoDir = getLocalRepoDir();
        QString path = remoteTmpPath.trimmed();
        if (path.startsWith(QLatin1Char('/'))) {
            path.remove(0, 1);
        }

        QString targetFilePath = repoDir + QStringLiteral("/") + path;
        QDir().mkpath(QFileInfo(targetFilePath).dir().absolutePath());

        QFile::remove(targetFilePath);
        bool copied = QFile::copy(localPath, targetFilePath);

        SyncResult res;
        if (copied) {
            res.status = SyncResult::Status::Success;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = QStringLiteral("Failed to stage database in git working directory");
        }
        if (cb) cb(res);
    });
}

void GitSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    QString repoDir = getLocalRepoDir();
    QString src = srcRemotePath.trimmed();
    if (src.startsWith(QLatin1Char('/'))) src.remove(0, 1);
    QString dest = destRemotePath.trimmed();
    if (dest.startsWith(QLatin1Char('/'))) dest.remove(0, 1);

    QString srcFull = repoDir + QStringLiteral("/") + src;
    QString destFull = repoDir + QStringLiteral("/") + dest;

    QFile::remove(destFull);
    if (!QFile::rename(srcFull, destFull)) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Failed to move temporary file into destination in git repo");
        if (cb) cb(res);
        return;
    }

    const auto& g = m_settings.git;
    QString branch = g.branch.trimmed().isEmpty() ? QStringLiteral("main") : g.branch.trimmed();

    // Stage file
    runGitCommand({QStringLiteral("add"), dest}, repoDir, [this, repoDir, branch, cb](bool addOk, int, const QString&, const QString& addErr) {
        if (!addOk) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = QStringLiteral("Git add failed: ") + addErr;
            if (cb) cb(res);
            return;
        }

        // Commit changes
        QString commitMsg = QStringLiteral("KeePassXC auto-sync: ") + QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        runGitCommand({QStringLiteral("commit"), QStringLiteral("-m"), commitMsg}, repoDir, [this, repoDir, branch, cb](bool commitOk, int, const QString&, const QString& commitErr) {
            Q_UNUSED(commitOk);
            Q_UNUSED(commitErr);

            // Push to remote repository
            runGitCommand({QStringLiteral("push"), QStringLiteral("origin"), branch}, repoDir, [cb](bool pushOk, int, const QString&, const QString& pushErr) {
                SyncResult res;
                if (pushOk) {
                    res.status = SyncResult::Status::Success;
                } else {
                    res.status = SyncResult::Status::NetworkError;
                    res.errorMessage = QStringLiteral("Git push failed: ") + pushErr.trimmed();
                }
                if (cb) cb(res);
            });
        });
    });
}
