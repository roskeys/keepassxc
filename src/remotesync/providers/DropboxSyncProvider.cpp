#include "DropboxSyncProvider.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include "networking/NetworkManager.h"

DropboxSyncProvider::DropboxSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

DropboxSyncProvider::~DropboxSyncProvider()
{
    cancelAll();
}

void DropboxSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void DropboxSyncProvider::cancelAll()
{
    for (auto reply : m_activeReplies) {
        if (reply && reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}

QString DropboxSyncProvider::normalizePath(const QString& rawPath) const
{
    QString path = rawPath.trimmed();
    if (path.isEmpty()) {
        path = m_settings.dropbox.remotePath.trimmed();
    }
    if (path.isEmpty()) {
        path = QStringLiteral("passwords.kdbx");
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return path;
}

void DropboxSyncProvider::applyAuth(QNetworkRequest& req) const
{
    const auto& d = m_settings.dropbox;
    if (!d.accessToken.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + d.accessToken.trimmed().toUtf8());
    }
}

void DropboxSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    configure(settings);
    const auto& d = settings.dropbox;

    if (d.accessToken.trimmed().isEmpty()) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Dropbox Access Token is empty");
        if (cb) cb(res);
        return;
    }

    // Call users/get_current_account
    QUrl url(QStringLiteral("https://api.dropboxapi.com/2/users/get_current_account"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyAuth(req);

    QNetworkReply* reply = getNetMgr()->post(req, QByteArray("null"));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray errBody = reply->readAll();
        QString detailMsg;
        if (!errBody.isEmpty()) {
            detailMsg = QString::fromUtf8(errBody).trimmed();
        }

        if (reply->error() == QNetworkReply::NoError || status == 200) {
            res.status = SyncResult::Status::Success;
        } else if (status == 401 || reply->error() == QNetworkReply::AuthenticationRequiredError) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Dropbox authentication failed (invalid or expired access token)") : detailMsg;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void DropboxSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    QString path = normalizePath(remotePath);

    QUrl url(QStringLiteral("https://api.dropboxapi.com/2/files/get_metadata"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyAuth(req);

    QJsonObject body;
    body[QStringLiteral("path")] = path;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = getNetMgr()->post(req, payload);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, path]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        QByteArray errBody = reply->readAll();
        QString detailMsg;
        if (!errBody.isEmpty()) {
            detailMsg = QString::fromUtf8(errBody).trimmed();
        }

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            res.status = SyncResult::Status::Success;
            QJsonDocument doc = QJsonDocument::fromJson(errBody);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                res.etag = obj.value(QStringLiteral("rev")).toString();
                QString serverModified = obj.value(QStringLiteral("server_modified")).toString();
                if (!serverModified.isEmpty()) {
                    res.lastModified = QDateTime::fromString(serverModified, Qt::ISODate);
                }
            }
        } else if (status == 409) {
            // Dropbox path not found is typically 409 with path/not_found
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("File not found in Dropbox: ") + path;
        } else if (status == 401) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Dropbox access token invalid or expired") : detailMsg;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void DropboxSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    QString path = normalizePath(remotePath);

    QUrl url(QStringLiteral("https://content.dropboxapi.com/2/files/download"));
    QNetworkRequest req(url);
    applyAuth(req);

    QJsonObject arg;
    arg[QStringLiteral("path")] = path;
    req.setRawHeader("Dropbox-API-Arg", QJsonDocument(arg).toJson(QJsonDocument::Compact));

    QNetworkReply* reply = getNetMgr()->post(req, QByteArray());
    m_activeReplies.append(reply);

    auto localFile = std::make_shared<QFile>(localTempPath);
    if (!localFile->open(QIODevice::WriteOnly)) {
        m_activeReplies.removeAll(reply);
        reply->abort();
        reply->deleteLater();
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local temp file for download: ") + localTempPath;
        if (cb) cb(res);
        return;
    }

    connect(reply, &QNetworkReply::readyRead, this, [reply, localFile]() {
        localFile->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, localFile, cb]() {
        m_activeReplies.removeAll(reply);
        localFile->close();
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            res.status = SyncResult::Status::Success;
            if (reply->hasRawHeader("Dropbox-API-Result")) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->rawHeader("Dropbox-API-Result"));
                if (doc.isObject()) {
                    res.etag = doc.object().value(QStringLiteral("rev")).toString();
                }
            }
        } else if (status == 409) {
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("Remote file not found");
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = reply->errorString();
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void DropboxSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    QString path = normalizePath(remoteTmpPath);

    QUrl url(QStringLiteral("https://content.dropboxapi.com/2/files/upload"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    applyAuth(req);

    QJsonObject arg;
    arg[QStringLiteral("path")] = path;
    arg[QStringLiteral("mode")] = QStringLiteral("overwrite");
    arg[QStringLiteral("autorename")] = false;
    arg[QStringLiteral("mute")] = true;
    req.setRawHeader("Dropbox-API-Arg", QJsonDocument(arg).toJson(QJsonDocument::Compact));

    auto file = std::make_shared<QFile>(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local database file: ") + localPath;
        if (cb) cb(res);
        return;
    }

    QByteArray data = file->readAll();
    QNetworkReply* reply = getNetMgr()->post(req, data);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        QByteArray errBody = reply->readAll();
        QString detailMsg;
        if (!errBody.isEmpty()) {
            detailMsg = QString::fromUtf8(errBody).trimmed();
        }

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            res.status = SyncResult::Status::Success;
        } else if (status == 401) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Dropbox access token invalid or expired") : detailMsg;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void DropboxSyncProvider::deleteFile(const QString& remotePath, SyncCallback cb)
{
    QString path = normalizePath(remotePath);

    QUrl url(QStringLiteral("https://api.dropboxapi.com/2/files/delete_v2"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyAuth(req);

    QJsonObject body;
    body[QStringLiteral("path")] = path;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = getNetMgr()->post(req, payload);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        QByteArray errBody = reply->readAll();
        QString detailMsg;
        if (!errBody.isEmpty()) {
            detailMsg = QString::fromUtf8(errBody).trimmed();
        }

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            res.status = SyncResult::Status::Success;
        } else if (status == 409) {
            // Already gone or not found is acceptable when deleting before overwrite
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = detailMsg;
        } else if (status == 401) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Dropbox access token invalid or expired") : detailMsg;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void DropboxSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    QString src = normalizePath(srcRemotePath);
    QString dest = normalizePath(destRemotePath);

    QUrl url(QStringLiteral("https://api.dropboxapi.com/2/files/move_v2"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyAuth(req);

    QJsonObject body;
    body[QStringLiteral("from_path")] = src;
    body[QStringLiteral("to_path")] = dest;
    body[QStringLiteral("autorename")] = false;
    body[QStringLiteral("allow_shared_folder")] = true;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = getNetMgr()->post(req, payload);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, src, dest, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        QByteArray errBody = reply->readAll();
        QString detailMsg;
        if (!errBody.isEmpty()) {
            detailMsg = QString::fromUtf8(errBody).trimmed();
        }

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            res.status = SyncResult::Status::Success;
            reply->deleteLater();
            if (cb) cb(res);
            return;
        }

        // If destination already exists, Dropbox returns 409 conflict.
        // Delete existing destination and retry move once.
        if (status == 409 && (detailMsg.contains(QStringLiteral("conflict")) || detailMsg.contains(QStringLiteral("to")))) {
            reply->deleteLater();
            deleteFile(dest, [this, src, dest, cb](const SyncResult& delRes) {
                Q_UNUSED(delRes);

                // Second move attempt after deleting old dest
                QUrl retryUrl(QStringLiteral("https://api.dropboxapi.com/2/files/move_v2"));
                QNetworkRequest retryReq(retryUrl);
                retryReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                applyAuth(retryReq);

                QJsonObject retryBody;
                retryBody[QStringLiteral("from_path")] = src;
                retryBody[QStringLiteral("to_path")] = dest;
                retryBody[QStringLiteral("autorename")] = false;
                retryBody[QStringLiteral("allow_shared_folder")] = true;
                QByteArray retryPayload = QJsonDocument(retryBody).toJson(QJsonDocument::Compact);

                QNetworkReply* retryReply = getNetMgr()->post(retryReq, retryPayload);
                m_activeReplies.append(retryReply);

                connect(retryReply, &QNetworkReply::finished, this, [this, retryReply, cb]() {
                    m_activeReplies.removeAll(retryReply);
                    SyncResult rRes;
                    int rStatus = retryReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    rRes.httpStatusCode = rStatus;

                    QByteArray rErrBody = retryReply->readAll();
                    QString rDetailMsg;
                    if (!rErrBody.isEmpty()) {
                        rDetailMsg = QString::fromUtf8(rErrBody).trimmed();
                    }

                    if (retryReply->error() == QNetworkReply::NoError && rStatus == 200) {
                        rRes.status = SyncResult::Status::Success;
                    } else if (rStatus == 401) {
                        rRes.status = SyncResult::Status::AuthError;
                        rRes.errorMessage = rDetailMsg.isEmpty() ? QStringLiteral("Dropbox access token invalid or expired") : rDetailMsg;
                    } else {
                        rRes.status = SyncResult::Status::NetworkError;
                        rRes.errorMessage = rDetailMsg.isEmpty() ? retryReply->errorString() : rDetailMsg;
                    }

                    retryReply->deleteLater();
                    if (cb) cb(rRes);
                });
            });
            return;
        }

        if (status == 401) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Dropbox access token invalid or expired") : detailMsg;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}
