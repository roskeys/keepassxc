#include "GoogleDriveSyncProvider.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include "networking/NetworkManager.h"

GoogleDriveSyncProvider::GoogleDriveSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

GoogleDriveSyncProvider::~GoogleDriveSyncProvider()
{
    cancelAll();
}

void GoogleDriveSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void GoogleDriveSyncProvider::cancelAll()
{
    for (auto reply : m_activeReplies) {
        if (reply && reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}

QString GoogleDriveSyncProvider::normalizeFileName(const QString& rawPath) const
{
    QString path = rawPath.trimmed();
    if (path.isEmpty()) {
        path = m_settings.googleDrive.remotePath.trimmed();
    }
    if (path.isEmpty()) {
        path = QStringLiteral("passwords.kdbx");
    }
    // Extract base name if path has slashes
    if (path.contains(QLatin1Char('/'))) {
        path = path.section(QLatin1Char('/'), -1);
    }
    return path;
}

void GoogleDriveSyncProvider::applyAuth(QNetworkRequest& req) const
{
    const auto& g = m_settings.googleDrive;
    if (!g.accessToken.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + g.accessToken.trimmed().toUtf8());
    }
}

void GoogleDriveSyncProvider::refreshTokenIfNeeded(std::function<void(bool success, const QString& errorMsg)> callback)
{
    const auto& g = m_settings.googleDrive;
    // If we don't have a refresh token, we can only rely on the access token
    if (g.refreshToken.trimmed().isEmpty()) {
        if (!g.accessToken.trimmed().isEmpty()) {
            if (callback) callback(true, QString());
        } else {
            if (callback) callback(false, QStringLiteral("Google Drive access token or refresh token is required"));
        }
        return;
    }

    // Call Google OAuth token endpoint to refresh
    QUrl tokenUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    params.addQueryItem(QStringLiteral("refresh_token"), g.refreshToken.trimmed());
    if (!g.clientId.trimmed().isEmpty()) {
        params.addQueryItem(QStringLiteral("client_id"), g.clientId.trimmed());
    }
    if (!g.clientSecret.trimmed().isEmpty()) {
        params.addQueryItem(QStringLiteral("client_secret"), g.clientSecret.trimmed());
    }

    QByteArray body = params.query(QUrl::FullyEncoded).toUtf8();
    QNetworkReply* reply = getNetMgr()->post(req, body);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        m_activeReplies.removeAll(reply);
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respBody = reply->readAll();

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            QJsonDocument doc = QJsonDocument::fromJson(respBody);
            if (doc.isObject()) {
                QString newAccessToken = doc.object().value(QStringLiteral("access_token")).toString();
                if (!newAccessToken.isEmpty()) {
                    m_settings.googleDrive.accessToken = newAccessToken;
                    reply->deleteLater();
                    if (callback) callback(true, QString());
                    return;
                }
            }
        }

        QString errorMsg = reply->errorString();
        QJsonDocument doc = QJsonDocument::fromJson(respBody);
        if (doc.isObject()) {
            if (doc.object().contains(QStringLiteral("error_description"))) {
                errorMsg = doc.object().value(QStringLiteral("error_description")).toString();
            } else if (doc.object().contains(QStringLiteral("error"))) {
                errorMsg = doc.object().value(QStringLiteral("error")).toString();
            }
        }
        reply->deleteLater();
        if (callback) callback(false, QStringLiteral("Failed to refresh Google token: ") + errorMsg);
    });
}

void GoogleDriveSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    configure(settings);
    const auto& g = settings.googleDrive;

    auto doTest = [this, cb]() {
        // Call Google Drive API: get about info to check token validity
        QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/about?fields=user"));
        QNetworkRequest req(url);
        applyAuth(req);

        QNetworkReply* reply = getNetMgr()->get(req);
        m_activeReplies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
            m_activeReplies.removeAll(reply);
            SyncResult res;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            res.httpStatusCode = status;

            QByteArray body = reply->readAll();
            QString detailMsg;
            if (!body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject() && doc.object().contains(QStringLiteral("error"))) {
                    QJsonObject errObj = doc.object().value(QStringLiteral("error")).toObject();
                    detailMsg = errObj.value(QStringLiteral("message")).toString();
                } else {
                    detailMsg = QString::fromUtf8(body).trimmed();
                }
            }

            if (reply->error() == QNetworkReply::NoError && status == 200) {
                res.status = SyncResult::Status::Success;
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = detailMsg.isEmpty() ? QStringLiteral("Google Drive access token invalid or expired") : detailMsg;
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    };

    if (!g.refreshToken.trimmed().isEmpty()) {
        refreshTokenIfNeeded([doTest, cb](bool success, const QString& errorMsg) {
            if (!success) {
                SyncResult res;
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = errorMsg;
                if (cb) cb(res);
                return;
            }
            doTest();
        });
    } else if (!g.accessToken.trimmed().isEmpty()) {
        doTest();
    } else {
        SyncResult res;
        res.status = SyncResult::Status::AuthError;
        res.errorMessage = QStringLiteral("Google Drive credentials (Refresh Token or Access Token) are empty");
        if (cb) cb(res);
    }
}

void GoogleDriveSyncProvider::resolveFileId(const QString& fileName, std::function<void(bool found, const QString& fileId, const QString& errorMsg)> callback)
{
    refreshTokenIfNeeded([this, fileName, callback](bool tokenOk, const QString& tokenErr) {
        if (!tokenOk) {
            if (callback) callback(false, QString(), tokenErr);
            return;
        }

        if (m_fileIdCache.contains(fileName) && !m_fileIdCache.value(fileName).isEmpty()) {
            if (callback) callback(true, m_fileIdCache.value(fileName), QString());
            return;
        }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    // Build search query: name = 'fileName' and trashed = false (and optionally in folder)
    QString q = QStringLiteral("name = '%1' and trashed = false").arg(fileName);
    QString folderId = m_settings.googleDrive.folderId.trimmed();
    if (!folderId.isEmpty()) {
        q += QStringLiteral(" and '%1' in parents").arg(folderId);
    }
    query.addQueryItem(QStringLiteral("q"), q);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("files(id, name, modifiedTime, md5Checksum)"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    applyAuth(req);

    QNetworkReply* reply = getNetMgr()->get(req);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, callback]() {
        m_activeReplies.removeAll(reply);
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) {
                QJsonArray files = doc.object().value(QStringLiteral("files")).toArray();
                if (!files.isEmpty()) {
                    QString fileId = files.first().toObject().value(QStringLiteral("id")).toString();
                    m_fileIdCache.insert(fileName, fileId);
                    reply->deleteLater();
                    if (callback) callback(true, fileId, QString());
                    return;
                }
            }
            reply->deleteLater();
            if (callback) callback(false, QString(), QStringLiteral("File not found on Google Drive"));
            return;
        }

        QString errorMsg = reply->errorString();
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject() && doc.object().contains(QStringLiteral("error"))) {
            errorMsg = doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        }
        reply->deleteLater();
        if (callback) callback(false, QString(), errorMsg);
    });
    });
}

void GoogleDriveSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    QString fileName = normalizeFileName(remotePath);

    resolveFileId(fileName, [this, fileName, cb](bool found, const QString& fileId, const QString& errorMsg) {
        if (!found) {
            SyncResult res;
            res.status = errorMsg.contains(QStringLiteral("not found"), Qt::CaseInsensitive) ? SyncResult::Status::NotFound : SyncResult::Status::NetworkError;
            res.errorMessage = errorMsg;
            if (cb) cb(res);
            return;
        }

        // Fetch file metadata with fileId
        QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(fileId));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id, name, modifiedTime, md5Checksum, size"));
        url.setQuery(query);

        QNetworkRequest req(url);
        applyAuth(req);

        QNetworkReply* reply = getNetMgr()->get(req);
        m_activeReplies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
            m_activeReplies.removeAll(reply);
            SyncResult res;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            res.httpStatusCode = status;

            QByteArray body = reply->readAll();
            if (reply->error() == QNetworkReply::NoError && status == 200) {
                res.status = SyncResult::Status::Success;
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    res.etag = obj.value(QStringLiteral("md5Checksum")).toString();
                    if (res.etag.isEmpty()) {
                        res.etag = obj.value(QStringLiteral("id")).toString();
                    }
                    QString modStr = obj.value(QStringLiteral("modifiedTime")).toString();
                    if (!modStr.isEmpty()) {
                        res.lastModified = QDateTime::fromString(modStr, Qt::ISODate);
                    }
                }
            } else if (status == 404) {
                res.status = SyncResult::Status::NotFound;
                res.errorMessage = QStringLiteral("Remote file not found");
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = QStringLiteral("Google Drive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void GoogleDriveSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    QString fileName = normalizeFileName(remotePath);

    resolveFileId(fileName, [this, fileName, localTempPath, cb](bool found, const QString& fileId, const QString& errorMsg) {
        if (!found) {
            SyncResult res;
            res.status = errorMsg.contains(QStringLiteral("not found"), Qt::CaseInsensitive) ? SyncResult::Status::NotFound : SyncResult::Status::NetworkError;
            res.errorMessage = errorMsg;
            if (cb) cb(res);
            return;
        }

        QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1?alt=media").arg(fileId));
        QNetworkRequest req(url);
        applyAuth(req);

        QNetworkReply* reply = getNetMgr()->get(req);
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
            } else if (status == 404) {
                res.status = SyncResult::Status::NotFound;
                res.errorMessage = QStringLiteral("Remote file not found");
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = QStringLiteral("Google Drive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void GoogleDriveSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    QString fileName = normalizeFileName(remoteTmpPath);

    auto file = std::make_shared<QFile>(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local database file: ") + localPath;
        if (cb) cb(res);
        return;
    }
    QByteArray data = file->readAll();
    file->close();

    // Check if tmp file already exists on Drive to update or create
    resolveFileId(fileName, [this, fileName, data, cb](bool found, const QString& fileId, const QString&) {
        if (found && !fileId.isEmpty()) {
            // Update existing file content: PATCH /upload/drive/v3/files/{fileId}?uploadType=media
            QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files/%1?uploadType=media").arg(fileId));
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
            applyAuth(req);

            QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "PATCH", data);
            m_activeReplies.append(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, fileId, cb]() {
                m_activeReplies.removeAll(reply);
                SyncResult res;
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                res.httpStatusCode = status;

                if (reply->error() == QNetworkReply::NoError && (status == 200 || status == 204)) {
                    res.status = SyncResult::Status::Success;
                    m_fileIdCache.insert(fileName, fileId);
                } else if (status == 401) {
                    res.status = SyncResult::Status::AuthError;
                    res.errorMessage = QStringLiteral("Google Drive access token invalid or expired");
                } else {
                    res.status = SyncResult::Status::NetworkError;
                    res.errorMessage = reply->errorString();
                }

                reply->deleteLater();
                if (cb) cb(res);
            });
        } else {
            // Create new file with multipart/related: metadata + media
            QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart"));
            QNetworkRequest req(url);
            applyAuth(req);

            QString boundary = QStringLiteral("-------KeePassXCGoogleDriveSyncBoundary");
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("multipart/related; boundary=") + boundary);

            QJsonObject metadata;
            metadata[QStringLiteral("name")] = fileName;
            QString folderId = m_settings.googleDrive.folderId.trimmed();
            if (!folderId.isEmpty()) {
                QJsonArray parents;
                parents.append(folderId);
                metadata[QStringLiteral("parents")] = parents;
            }

            QByteArray body;
            body.append(QStringLiteral("--").toUtf8() + boundary.toUtf8() + "\r\n");
            body.append("Content-Type: application/json; charset=UTF-8\r\n\r\n");
            body.append(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
            body.append("\r\n");
            body.append(QStringLiteral("--").toUtf8() + boundary.toUtf8() + "\r\n");
            body.append("Content-Type: application/octet-stream\r\n\r\n");
            body.append(data);
            body.append("\r\n");
            body.append(QStringLiteral("--").toUtf8() + boundary.toUtf8() + "--\r\n");

            QNetworkReply* reply = getNetMgr()->post(req, body);
            m_activeReplies.append(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, cb]() {
                m_activeReplies.removeAll(reply);
                SyncResult res;
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                res.httpStatusCode = status;
                QByteArray resBody = reply->readAll();

                if (reply->error() == QNetworkReply::NoError && (status == 200 || status == 201)) {
                    res.status = SyncResult::Status::Success;
                    QJsonDocument doc = QJsonDocument::fromJson(resBody);
                    if (doc.isObject()) {
                        QString newId = doc.object().value(QStringLiteral("id")).toString();
                        if (!newId.isEmpty()) {
                            m_fileIdCache.insert(fileName, newId);
                        }
                    }
                } else if (status == 401) {
                    res.status = SyncResult::Status::AuthError;
                    res.errorMessage = QStringLiteral("Google Drive access token invalid or expired");
                } else {
                    res.status = SyncResult::Status::NetworkError;
                    res.errorMessage = reply->errorString();
                }

                reply->deleteLater();
                if (cb) cb(res);
            });
        }
    });
}

void GoogleDriveSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    QString srcName = normalizeFileName(srcRemotePath);
    QString destName = normalizeFileName(destRemotePath);

    // If destination file already exists, delete it first or rename source to destination
    resolveFileId(srcName, [this, srcName, destName, cb](bool srcFound, const QString& srcFileId, const QString& srcErr) {
        if (!srcFound || srcFileId.isEmpty()) {
            SyncResult res;
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("Source file not found on Google Drive: ") + srcErr;
            if (cb) cb(res);
            return;
        }

        // Check if destination file already exists
        resolveFileId(destName, [this, srcName, destName, srcFileId, cb](bool destFound, const QString& destFileId, const QString&) {
            auto renameSourceFile = [this, srcName, destName, srcFileId, cb]() {
                // Rename source file to destName: PATCH /drive/v3/files/{fileId} with {"name": destName}
                QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(srcFileId));
                QNetworkRequest req(url);
                req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
                applyAuth(req);

                QJsonObject body;
                body[QStringLiteral("name")] = destName;
                QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

                QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "PATCH", payload);
                m_activeReplies.append(reply);

                connect(reply, &QNetworkReply::finished, this, [this, reply, srcName, destName, srcFileId, cb]() {
                    m_activeReplies.removeAll(reply);
                    SyncResult res;
                    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    res.httpStatusCode = status;

                    if (reply->error() == QNetworkReply::NoError && status == 200) {
                        res.status = SyncResult::Status::Success;
                        m_fileIdCache.remove(srcName);
                        m_fileIdCache.insert(destName, srcFileId);
                    } else if (status == 401) {
                        res.status = SyncResult::Status::AuthError;
                        res.errorMessage = QStringLiteral("Google Drive access token invalid or expired");
                    } else {
                        res.status = SyncResult::Status::NetworkError;
                        res.errorMessage = reply->errorString();
                    }

                    reply->deleteLater();
                    if (cb) cb(res);
                });
            };

            if (destFound && !destFileId.isEmpty() && destFileId != srcFileId) {
                // Delete existing destination file before renaming
                QUrl delUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(destFileId));
                QNetworkRequest delReq(delUrl);
                applyAuth(delReq);

                QNetworkReply* delReply = getNetMgr()->deleteResource(delReq);
                m_activeReplies.append(delReply);

                connect(delReply, &QNetworkReply::finished, this, [this, delReply, destName, renameSourceFile]() {
                    m_activeReplies.removeAll(delReply);
                    m_fileIdCache.remove(destName);
                    delReply->deleteLater();
                    renameSourceFile();
                });
            } else {
                renameSourceFile();
            }
        });
    });
}
