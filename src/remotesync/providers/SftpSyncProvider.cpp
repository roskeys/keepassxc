#include "SftpSyncProvider.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryFile>

SftpSyncProvider::SftpSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

SftpSyncProvider::~SftpSyncProvider()
{
    cancelAll();
}

void SftpSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void SftpSyncProvider::cancelAll()
{
    for (auto& proc : m_activeProcesses) {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->kill();
        }
    }
    m_activeProcesses.clear();
}

void SftpSyncProvider::executeSftpCommands(
    const QStringList& commands,
    const QString& expectedHost,
    int port,
    const QString& user,
    const QString& keyPath,
    const QString& password,
    std::function<void(bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr)> onFinished)
{
    QString host = expectedHost.trimmed();
    if (host.startsWith(QLatin1String("sftp://"), Qt::CaseInsensitive)) {
        host = host.mid(7);
    }
    if (host.contains(QLatin1Char('/'))) {
        host = host.section(QLatin1Char('/'), 0, 0);
    }

    if (host.isEmpty()) {
        if (onFinished) {
            onFinished(false, -1, {}, QStringLiteral("Empty host address"));
        }
        return;
    }

    auto* batchFile = new QTemporaryFile(this);
    if (!batchFile->open()) {
        if (onFinished) {
            onFinished(false, -1, {}, QStringLiteral("Failed to create temporary batch file"));
        }
        delete batchFile;
        return;
    }

    for (const QString& cmd : commands) {
        batchFile->write(cmd.toUtf8() + "\n");
    }
    batchFile->close();

    QStringList args;
    args << QStringLiteral("-b") << batchFile->fileName();
    args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new");
    args << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=15");

    if (port > 0) {
        args << QStringLiteral("-P") << QString::number(port);
    }

    QString resolvedKeyPath = keyPath.trimmed();
    if (!resolvedKeyPath.isEmpty()) {
        if (resolvedKeyPath.startsWith(QLatin1String("~/"))) {
            resolvedKeyPath = QDir::homePath() + resolvedKeyPath.mid(1);
        }
        if (QFile::exists(resolvedKeyPath)) {
            args << QStringLiteral("-i") << resolvedKeyPath;
        }
    }

    QString destination;
    if (!user.isEmpty()) {
        destination = user + QLatin1Char('@') + host;
    } else {
        destination = host;
    }
    args << destination;

    auto* proc = new QProcess(this);
    m_activeProcesses.append(proc);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    QTemporaryFile* askpassScript = nullptr;
    QTemporaryFile* pwdFile = nullptr;

    if (!password.isEmpty()) {
        pwdFile = new QTemporaryFile(this);
        if (pwdFile->open()) {
            pwdFile->write(password.toUtf8());
            pwdFile->close();

            askpassScript = new QTemporaryFile(this);
            if (askpassScript->open()) {
                QString scriptContent = QStringLiteral("#!/bin/sh\ncat \"%1\"\n").arg(pwdFile->fileName());
                askpassScript->write(scriptContent.toUtf8());
                askpassScript->close();
                QFile::setPermissions(askpassScript->fileName(),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

                env.insert(QStringLiteral("SSH_ASKPASS"), askpassScript->fileName());
                env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
                if (!env.contains(QStringLiteral("DISPLAY"))) {
                    env.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
                }
            }
        }
    }

    proc->setProcessEnvironment(env);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, batchFile, askpassScript, pwdFile, onFinished](int exitCode, QProcess::ExitStatus exitStatus) {
                m_activeProcesses.removeAll(proc);
                QString out = QString::fromUtf8(proc->readAllStandardOutput());
                QString err = QString::fromUtf8(proc->readAllStandardError());
                bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);

                if (onFinished) {
                    onFinished(ok, exitCode, out, err);
                }

                proc->deleteLater();
                delete batchFile;
                if (askpassScript) {
                    delete askpassScript;
                }
                if (pwdFile) {
                    delete pwdFile;
                }
            });

    proc->start(QStringLiteral("sftp"), args);
}

void SftpSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    const auto& s = settings.sftp;
    QString host = s.host.isEmpty() ? settings.url : s.host;
    int port = s.port > 0 ? s.port : 22;
    QString user = s.username.isEmpty() ? settings.username : s.username;
    QString key = s.keyPath.isEmpty() ? settings.sftpKeyPath : s.keyPath;
    QString pass = s.password.isEmpty() ? settings.password : s.password;

    executeSftpCommands({QStringLiteral("pwd")}, host, port, user, key, pass,
                        [cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
                            Q_UNUSED(exitCode);
                            Q_UNUSED(stdoutStr);
                            SyncResult res;
                            if (success) {
                                res.status = SyncResult::Status::Success;
                            } else {
                                if (stderrStr.contains(QLatin1String("Permission denied"), Qt::CaseInsensitive)) {
                                    res.status = SyncResult::Status::AuthError;
                                    res.errorMessage = QStringLiteral("Authentication failed: invalid key or password");
                                } else {
                                    res.status = SyncResult::Status::NetworkError;
                                    res.errorMessage = stderrStr.trimmed();
                                }
                            }
                            if (cb) {
                                cb(res);
                            }
                        });
}

void SftpSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    const auto& s = m_settings.sftp;
    QString path = remotePath.isEmpty() ? s.remotePath : remotePath;
    if (path.isEmpty()) {
        path = m_settings.remotePath;
    }

    executeSftpCommands({QStringLiteral("ls -l \"%1\"").arg(path)},
                        s.host.isEmpty() ? m_settings.url : s.host,
                        s.port > 0 ? s.port : 22,
                        s.username.isEmpty() ? m_settings.username : s.username,
                        s.keyPath.isEmpty() ? m_settings.sftpKeyPath : s.keyPath,
                        s.password.isEmpty() ? m_settings.password : s.password,
                        [path, cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
                            Q_UNUSED(exitCode);
                            SyncResult res;
                            if (success) {
                                res.status = SyncResult::Status::Success;
                                res.etag = QString::number(stdoutStr.trimmed().length());
                                res.lastModified = QDateTime::currentDateTimeUtc();
                            } else {
                                if (stderrStr.contains(QLatin1String("not found"), Qt::CaseInsensitive)
                                    || stderrStr.contains(QLatin1String("No such file"), Qt::CaseInsensitive)) {
                                    res.status = SyncResult::Status::NotFound;
                                    res.errorMessage = QStringLiteral("Remote file does not exist yet: ") + path;
                                } else if (stderrStr.contains(QLatin1String("Permission denied"), Qt::CaseInsensitive)) {
                                    res.status = SyncResult::Status::AuthError;
                                    res.errorMessage = stderrStr.trimmed();
                                } else {
                                    res.status = SyncResult::Status::NetworkError;
                                    res.errorMessage = stderrStr.trimmed();
                                }
                            }
                            if (cb) {
                                cb(res);
                            }
                        });
}

void SftpSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    const auto& s = m_settings.sftp;
    QString path = remotePath.isEmpty() ? s.remotePath : remotePath;
    if (path.isEmpty()) {
        path = m_settings.remotePath;
    }

    executeSftpCommands({QStringLiteral("get \"%1\" \"%2\"").arg(path, localTempPath)},
                        s.host.isEmpty() ? m_settings.url : s.host,
                        s.port > 0 ? s.port : 22,
                        s.username.isEmpty() ? m_settings.username : s.username,
                        s.keyPath.isEmpty() ? m_settings.sftpKeyPath : s.keyPath,
                        s.password.isEmpty() ? m_settings.password : s.password,
                        [cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
                            Q_UNUSED(exitCode);
                            Q_UNUSED(stdoutStr);
                            SyncResult res;
                            if (success) {
                                res.status = SyncResult::Status::Success;
                            } else {
                                res.status = SyncResult::Status::NetworkError;
                                res.errorMessage = stderrStr.trimmed();
                            }
                            if (cb) {
                                cb(res);
                            }
                        });
}

void SftpSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    const auto& s = m_settings.sftp;
    executeSftpCommands({QStringLiteral("put \"%1\" \"%2\"").arg(localPath, remoteTmpPath)},
                        s.host.isEmpty() ? m_settings.url : s.host,
                        s.port > 0 ? s.port : 22,
                        s.username.isEmpty() ? m_settings.username : s.username,
                        s.keyPath.isEmpty() ? m_settings.sftpKeyPath : s.keyPath,
                        s.password.isEmpty() ? m_settings.password : s.password,
                        [cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
                            Q_UNUSED(exitCode);
                            Q_UNUSED(stdoutStr);
                            SyncResult res;
                            if (success) {
                                res.status = SyncResult::Status::Success;
                            } else {
                                res.status = SyncResult::Status::NetworkError;
                                res.errorMessage = stderrStr.trimmed();
                            }
                            if (cb) {
                                cb(res);
                            }
                        });
}

void SftpSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    const auto& s = m_settings.sftp;
    executeSftpCommands({QStringLiteral("rename \"%1\" \"%2\"").arg(srcRemotePath, destRemotePath)},
                        s.host.isEmpty() ? m_settings.url : s.host,
                        s.port > 0 ? s.port : 22,
                        s.username.isEmpty() ? m_settings.username : s.username,
                        s.keyPath.isEmpty() ? m_settings.sftpKeyPath : s.keyPath,
                        s.password.isEmpty() ? m_settings.password : s.password,
                        [cb](bool success, int exitCode, const QString& stdoutStr, const QString& stderrStr) {
                            Q_UNUSED(exitCode);
                            Q_UNUSED(stdoutStr);
                            SyncResult res;
                            if (success) {
                                res.status = SyncResult::Status::Success;
                            } else {
                                res.status = SyncResult::Status::NetworkError;
                                res.errorMessage = stderrStr.trimmed();
                            }
                            if (cb) {
                                cb(res);
                            }
                        });
}
