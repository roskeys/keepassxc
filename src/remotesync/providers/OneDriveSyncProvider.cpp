#include "OneDriveSyncProvider.h"

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

OneDriveSyncProvider::OneDriveSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

OneDriveSyncProvider::~OneDriveSyncProvider()
{
    cancelAll();
}

void OneDriveSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
    m_tokenExpiresAt = QDateTime();
}

void OneDriveSyncProvider::cancelAll()
{
    for (auto reply : m_activeReplies) {
        if (reply && reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}

QString OneDriveSyncProvider::normalizePath(const QString& rawPath) const
{
    QString path = rawPath.trimmed();
    if (path.isEmpty()) {
        path = m_settings.oneDrive.remotePath.trimmed();
    }
    if (path.isEmpty()) {
        path = QStringLiteral("passwords.kdbx");
    }
    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    return path;
}

QUrl OneDriveSyncProvider::buildItemUrl(const QString& normalizedPath) const
{
    QString driveBase = m_settings.oneDrive.driveId.trimmed().isEmpty()
        ? QStringLiteral("https://graph.microsoft.com/v1.0/me/drive")
        : QStringLiteral("https://graph.microsoft.com/v1.0/drives/%1").arg(m_settings.oneDrive.driveId.trimmed());
    return QUrl(QStringLiteral("%1/root:/%2").arg(driveBase, normalizedPath));
}

QUrl OneDriveSyncProvider::buildContentUrl(const QString& normalizedPath) const
{
    QString driveBase = m_settings.oneDrive.driveId.trimmed().isEmpty()
        ? QStringLiteral("https://graph.microsoft.com/v1.0/me/drive")
        : QStringLiteral("https://graph.microsoft.com/v1.0/drives/%1").arg(m_settings.oneDrive.driveId.trimmed());
    return QUrl(QStringLiteral("%1/root:/%2:/content").arg(driveBase, normalizedPath));
}

void OneDriveSyncProvider::applyAuth(QNetworkRequest& req) const
{
    const auto& o = m_settings.oneDrive;
    if (!o.accessToken.trimmed().isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + o.accessToken.trimmed().toUtf8());
    }
}

void OneDriveSyncProvider::refreshToken(std::function<void(bool success, const QString& errorMsg)> callback)
{
    const auto& o = m_settings.oneDrive;
    if (o.refreshToken.trimmed().isEmpty()) {
        if (!o.accessToken.trimmed().isEmpty()) {
            if (callback) callback(true, QString());
        } else {
            if (callback) callback(false, tr("OneDrive access token or refresh token is required"));
        }
        return;
    }

    if (o.clientId.trimmed().isEmpty()) {
        if (callback) callback(false, tr("OneDrive Client ID (Application ID) is required to refresh the token"));
        return;
    }

    QUrl tokenUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    params.addQueryItem(QStringLiteral("refresh_token"), o.refreshToken.trimmed());
    params.addQueryItem(QStringLiteral("client_id"), o.clientId.trimmed());
    params.addQueryItem(QStringLiteral("scope"), QStringLiteral("https://graph.microsoft.com/Files.ReadWrite offline_access"));
    if (!o.clientSecret.trimmed().isEmpty()) {
        params.addQueryItem(QStringLiteral("client_secret"), o.clientSecret.trimmed());
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
                QJsonObject obj = doc.object();
                QString newAccessToken = obj.value(QStringLiteral("access_token")).toString();
                if (!newAccessToken.isEmpty()) {
                    m_settings.oneDrive.accessToken = newAccessToken;
                    if (obj.contains(QStringLiteral("refresh_token"))) {
                        QString newRefreshToken = obj.value(QStringLiteral("refresh_token")).toString();
                        if (!newRefreshToken.isEmpty()) {
                            m_settings.oneDrive.refreshToken = newRefreshToken;
                        }
                    }
                    int expiresIn = obj.value(QStringLiteral("expires_in")).toInt(3600);
                    m_tokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn > 60 ? expiresIn - 60 : expiresIn);
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
        if (callback) callback(false, tr("Failed to refresh OneDrive token: %1").arg(errorMsg));
    });
}

void OneDriveSyncProvider::ensureAuthenticated(std::function<void(bool success, const QString& errorMsg)> callback)
{
    const auto& o = m_settings.oneDrive;
    if (o.refreshToken.trimmed().isEmpty()) {
        if (!o.accessToken.trimmed().isEmpty()) {
            if (callback) callback(true, QString());
        } else {
            if (callback) callback(false, tr("OneDrive credentials (Access Token or Refresh Token) are empty"));
        }
        return;
    }

    if (m_tokenExpiresAt.isValid() && QDateTime::currentDateTimeUtc() < m_tokenExpiresAt && !o.accessToken.trimmed().isEmpty()) {
        if (callback) callback(true, QString());
        return;
    }

    refreshToken(callback);
}

void OneDriveSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    configure(settings);

    ensureAuthenticated([this, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString driveBase = m_settings.oneDrive.driveId.trimmed().isEmpty()
            ? QStringLiteral("https://graph.microsoft.com/v1.0/me/drive")
            : QStringLiteral("https://graph.microsoft.com/v1.0/drives/%1").arg(m_settings.oneDrive.driveId.trimmed());

        QUrl url(driveBase);
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
                res.errorMessage = detailMsg.isEmpty() ? tr("OneDrive access token invalid or expired") : detailMsg;
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = detailMsg.isEmpty() ? reply->errorString() : detailMsg;
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void OneDriveSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    ensureAuthenticated([this, remotePath, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString path = normalizePath(remotePath);
        QUrl url = buildItemUrl(path);
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
                    res.etag = obj.value(QStringLiteral("eTag")).toString();
                    QString dtStr = obj.value(QStringLiteral("lastModifiedDateTime")).toString();
                    if (!dtStr.isEmpty()) {
                        res.lastModified = QDateTime::fromString(dtStr, Qt::ISODate);
                    }
                }
                if (res.etag.isEmpty() && reply->hasRawHeader("ETag")) {
                    res.etag = QString::fromUtf8(reply->rawHeader("ETag"));
                }
            } else if (status == 404) {
                res.status = SyncResult::Status::NotFound;
                res.errorMessage = tr("Remote file not found");
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = tr("OneDrive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void OneDriveSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    ensureAuthenticated([this, remotePath, localTempPath, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString path = normalizePath(remotePath);
        QUrl url = buildContentUrl(path);
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        applyAuth(req);

        auto localFile = std::make_shared<QFile>(localTempPath);
        if (!localFile->open(QIODevice::WriteOnly)) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = tr("Cannot open local temp file for download: %1").arg(localTempPath);
            if (cb) cb(res);
            return;
        }

        QNetworkReply* reply = getNetMgr()->get(req);
        m_activeReplies.append(reply);

        connect(reply, &QNetworkReply::readyRead, this, [reply, localFile]() {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200 || status == 206) {
                localFile->write(reply->readAll());
            }
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply, localFile, cb]() {
            m_activeReplies.removeAll(reply);
            localFile->close();
            SyncResult res;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            res.httpStatusCode = status;

            // Handle manual redirect if Qt did not follow automatically
            if ((status == 301 || status == 302 || status == 307 || status == 308) && reply->hasRawHeader("Location")) {
                QUrl redirectUrl = reply->header(QNetworkRequest::LocationHeader).toUrl();
                reply->deleteLater();
                if (redirectUrl.isValid()) {
                    QNetworkRequest redirReq(redirectUrl);
                    redirReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                    if (localFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        QNetworkReply* redirReply = getNetMgr()->get(redirReq);
                        m_activeReplies.append(redirReply);
                        connect(redirReply, &QNetworkReply::readyRead, this, [redirReply, localFile]() {
                            localFile->write(redirReply->readAll());
                        });
                        connect(redirReply, &QNetworkReply::finished, this, [this, redirReply, localFile, cb]() {
                            m_activeReplies.removeAll(redirReply);
                            localFile->close();
                            SyncResult redirRes;
                            int redirStatus = redirReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            redirRes.httpStatusCode = redirStatus;
                            if (redirReply->error() == QNetworkReply::NoError && (redirStatus == 200 || redirStatus == 206)) {
                                redirRes.status = SyncResult::Status::Success;
                                if (redirReply->hasRawHeader("ETag")) {
                                    redirRes.etag = QString::fromUtf8(redirReply->rawHeader("ETag"));
                                }
                            } else {
                                redirRes.status = SyncResult::Status::NetworkError;
                                redirRes.errorMessage = redirReply->errorString();
                            }
                            redirReply->deleteLater();
                            if (cb) cb(redirRes);
                        });
                        return;
                    }
                }
            }

            if (reply->error() == QNetworkReply::NoError && (status == 200 || status == 206)) {
                res.status = SyncResult::Status::Success;
                if (reply->hasRawHeader("ETag")) {
                    res.etag = QString::fromUtf8(reply->rawHeader("ETag"));
                }
            } else if (status == 404) {
                res.status = SyncResult::Status::NotFound;
                res.errorMessage = tr("Remote file not found");
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = tr("OneDrive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void OneDriveSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    ensureAuthenticated([this, localPath, remoteTmpPath, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString path = normalizePath(remoteTmpPath);
        QUrl url = buildContentUrl(path);
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        applyAuth(req);

        auto file = std::make_shared<QFile>(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            SyncResult res;
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = tr("Cannot open local database file: %1").arg(localPath);
            if (cb) cb(res);
            return;
        }

        QByteArray data = file->readAll();
        file->close();

        QNetworkReply* reply = getNetMgr()->put(req, data);
        m_activeReplies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
            m_activeReplies.removeAll(reply);
            SyncResult res;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            res.httpStatusCode = status;
            QByteArray body = reply->readAll();

            if (reply->error() == QNetworkReply::NoError && (status == 200 || status == 201)) {
                res.status = SyncResult::Status::Success;
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    res.etag = doc.object().value(QStringLiteral("eTag")).toString();
                    QString dtStr = doc.object().value(QStringLiteral("lastModifiedDateTime")).toString();
                    if (!dtStr.isEmpty()) {
                        res.lastModified = QDateTime::fromString(dtStr, Qt::ISODate);
                    }
                }
                if (res.etag.isEmpty() && reply->hasRawHeader("ETag")) {
                    res.etag = QString::fromUtf8(reply->rawHeader("ETag"));
                }
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = tr("OneDrive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void OneDriveSyncProvider::deleteFile(const QString& remotePath, SyncCallback cb)
{
    ensureAuthenticated([this, remotePath, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString path = normalizePath(remotePath);
        QUrl url = buildItemUrl(path);
        QNetworkRequest req(url);
        applyAuth(req);

        QNetworkReply* reply = getNetMgr()->deleteResource(req);
        m_activeReplies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
            m_activeReplies.removeAll(reply);
            SyncResult res;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            res.httpStatusCode = status;

            if (reply->error() == QNetworkReply::NoError && (status == 200 || status == 204 || status == 404)) {
                res.status = SyncResult::Status::Success;
            } else if (status == 401) {
                res.status = SyncResult::Status::AuthError;
                res.errorMessage = tr("OneDrive access token invalid or expired");
            } else {
                res.status = SyncResult::Status::NetworkError;
                res.errorMessage = reply->errorString();
            }

            reply->deleteLater();
            if (cb) cb(res);
        });
    });
}

void OneDriveSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    ensureAuthenticated([this, srcRemotePath, destRemotePath, cb](bool authOk, const QString& authErr) {
        if (!authOk) {
            SyncResult res;
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = authErr;
            if (cb) cb(res);
            return;
        }

        QString srcPath = normalizePath(srcRemotePath);
        QString destPath = normalizePath(destRemotePath);
        QString destFileName = destPath.section(QLatin1Char('/'), -1);

        auto doRename = [this, srcPath, destPath, destFileName, cb](bool isRetry) {
            QUrl url = buildItemUrl(srcPath);
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            applyAuth(req);

            QJsonObject body;
            body[QStringLiteral("name")] = destFileName;
            body[QStringLiteral("@microsoft.graph.conflictBehavior")] = QStringLiteral("replace");
            QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

            QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "PATCH", payload);
            m_activeReplies.append(reply);

            connect(reply, &QNetworkReply::finished, this, [this, reply, srcPath, destPath, destFileName, isRetry, cb]() {
                m_activeReplies.removeAll(reply);
                SyncResult res;
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                res.httpStatusCode = status;
                QByteArray respBody = reply->readAll();

                if (reply->error() == QNetworkReply::NoError && status == 200) {
                    res.status = SyncResult::Status::Success;
                    QJsonDocument doc = QJsonDocument::fromJson(respBody);
                    if (doc.isObject()) {
                        res.etag = doc.object().value(QStringLiteral("eTag")).toString();
                    }
                    reply->deleteLater();
                    if (cb) cb(res);
                } else if (status == 409 && !isRetry) {
                    // Conflict: destination already exists and could not be replaced via PATCH.
                    // Delete destination first, then retry rename once.
                    reply->deleteLater();
                    deleteFile(destPath, [this, srcPath, destFileName, cb](const SyncResult& delRes) {
                        Q_UNUSED(delRes);
                        QUrl retryUrl = buildItemUrl(srcPath);
                        QNetworkRequest retryReq(retryUrl);
                        retryReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
                        applyAuth(retryReq);

                        QJsonObject retryBody;
                        retryBody[QStringLiteral("name")] = destFileName;
                        QByteArray retryPayload = QJsonDocument(retryBody).toJson(QJsonDocument::Compact);

                        QNetworkReply* retryReply = getNetMgr()->sendCustomRequest(retryReq, "PATCH", retryPayload);
                        m_activeReplies.append(retryReply);

                        connect(retryReply, &QNetworkReply::finished, this, [this, retryReply, cb]() {
                            m_activeReplies.removeAll(retryReply);
                            SyncResult finalRes;
                            int finalStatus = retryReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            finalRes.httpStatusCode = finalStatus;
                            if (retryReply->error() == QNetworkReply::NoError && finalStatus == 200) {
                                finalRes.status = SyncResult::Status::Success;
                                QJsonDocument doc = QJsonDocument::fromJson(retryReply->readAll());
                                if (doc.isObject()) {
                                    finalRes.etag = doc.object().value(QStringLiteral("eTag")).toString();
                                }
                            } else {
                                finalRes.status = SyncResult::Status::NetworkError;
                                finalRes.errorMessage = retryReply->errorString();
                            }
                            retryReply->deleteLater();
                            if (cb) cb(finalRes);
                        });
                    });
                } else if (status == 401) {
                    res.status = SyncResult::Status::AuthError;
                    res.errorMessage = tr("OneDrive access token invalid or expired");
                    reply->deleteLater();
                    if (cb) cb(res);
                } else {
                    res.status = SyncResult::Status::NetworkError;
                    res.errorMessage = reply->errorString();
                    reply->deleteLater();
                    if (cb) cb(res);
                }
            });
        };

        doRename(false);
    });
}
