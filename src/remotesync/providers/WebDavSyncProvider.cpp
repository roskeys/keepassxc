#include "WebDavSyncProvider.h"

#include <QByteArray>
#include <QDateTime>
#include <QDomDocument>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>
#include "networking/NetworkManager.h"

WebDavSyncProvider::WebDavSyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

WebDavSyncProvider::~WebDavSyncProvider()
{
    cancelAll();
}

void WebDavSyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void WebDavSyncProvider::applyAuthHeader(QNetworkRequest& req, const RemoteSyncSettings& settings)
{
    if (!settings.username.isEmpty()) {
        QByteArray credentials = QString("%1:%2").arg(settings.username, settings.password).toUtf8();
        req.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    }
}

SyncResult WebDavSyncProvider::parseReply(QNetworkReply* reply)
{
    SyncResult result;
    if (!reply) {
        result.status = SyncResult::Status::NetworkError;
        result.errorMessage = QStringLiteral("Null network reply");
        return result;
    }

    result.httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        switch (reply->error()) {
        case QNetworkReply::AuthenticationRequiredError:
            result.status = SyncResult::Status::AuthError;
            break;
        case QNetworkReply::ContentNotFoundError:
            result.status = SyncResult::Status::NotFound;
            break;
        case QNetworkReply::ContentConflictError:
            result.status = SyncResult::Status::Conflict;
            break;
        case QNetworkReply::SslHandshakeFailedError:
            result.status = SyncResult::Status::SslError;
            break;
        case QNetworkReply::OperationCanceledError:
            result.status = SyncResult::Status::Cancelled;
            break;
        default:
            if (result.httpStatusCode >= 500) {
                result.status = SyncResult::Status::ServerError;
            } else {
                result.status = SyncResult::Status::NetworkError;
            }
            break;
        }
        result.errorMessage = reply->errorString();
        return result;
    }

    result.status = SyncResult::Status::Success;
    if (reply->hasRawHeader("ETag")) {
        result.etag = QString::fromUtf8(reply->rawHeader("ETag")).remove('"');
    }
    if (reply->hasRawHeader("Last-Modified")) {
        result.lastModified = QDateTime::fromString(QString::fromUtf8(reply->rawHeader("Last-Modified")), Qt::RFC2822Date);
    }

    return result;
}

void WebDavSyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    QUrl targetUrl(settings.fullRemoteUrl());
    if (!targetUrl.isValid()) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Invalid URL");
        if (cb) cb(res);
        return;
    }

    QNetworkRequest req(targetUrl);
    applyAuthHeader(req, settings);
    req.setRawHeader("Depth", "0");

    QByteArray propfindBody = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
                             "<D:propfind xmlns:D=\"DAV:\">"
                             "<D:prop><D:getetag/><D:getlastmodified/></D:prop>"
                             "</D:propfind>";

    QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "PROPFIND", propfindBody);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::sslErrors, this, [settings, reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        if (!settings.verifySsl) {
            reply->ignoreSslErrors();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res = parseReply(reply);
        if (reply->error() == QNetworkReply::NoError && (res.httpStatusCode == 200 || res.httpStatusCode == 207)) {
            res.status = SyncResult::Status::Success;
        }
        reply->deleteLater();
        if (cb) cb(res);
    });
}

void WebDavSyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    QUrl targetUrl(remotePath.isEmpty() ? m_settings.url : remotePath);
    QNetworkRequest req(targetUrl);
    applyAuthHeader(req, m_settings);
    req.setRawHeader("Depth", "0");

    QByteArray propfindBody = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
                             "<D:propfind xmlns:D=\"DAV:\">"
                             "<D:prop><D:getetag/><D:getlastmodified/></D:prop>"
                             "</D:propfind>";

    QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "PROPFIND", propfindBody);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        if (!m_settings.verifySsl) {
            reply->ignoreSslErrors();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res = parseReply(reply);

        if (reply->error() == QNetworkReply::NoError && (res.httpStatusCode == 200 || res.httpStatusCode == 207)) {
            res.status = SyncResult::Status::Success;

            // Parse WebDAV XML for ETag / getlastmodified if not present in HTTP headers
            QDomDocument doc;
            if (doc.setContent(reply->readAll())) {
                auto etagNodes = doc.elementsByTagNameNS(QStringLiteral("DAV:"), QStringLiteral("getetag"));
                if (!etagNodes.isEmpty()) {
                    res.etag = etagNodes.at(0).toElement().text().remove('"');
                }
                auto lmNodes = doc.elementsByTagNameNS(QStringLiteral("DAV:"), QStringLiteral("getlastmodified"));
                if (!lmNodes.isEmpty()) {
                    res.lastModified = QDateTime::fromString(lmNodes.at(0).toElement().text(), Qt::RFC2822Date);
                }
            }
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void WebDavSyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    QUrl targetUrl(remotePath.isEmpty() ? m_settings.url : remotePath);
    QNetworkRequest req(targetUrl);
    applyAuthHeader(req, m_settings);

    QNetworkReply* reply = getNetMgr()->get(req);
    m_activeReplies.append(reply);

    auto localFile = std::make_shared<QFile>(localTempPath);
    if (!localFile->open(QIODevice::WriteOnly)) {
        m_activeReplies.removeAll(reply);
        reply->abort();
        reply->deleteLater();
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local temp file for writing: ") + localTempPath;
        if (cb) cb(res);
        return;
    }

    connect(reply, &QNetworkReply::readyRead, this, [reply, localFile]() {
        localFile->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        if (!m_settings.verifySsl) {
            reply->ignoreSslErrors();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, localFile, cb]() {
        m_activeReplies.removeAll(reply);
        localFile->write(reply->readAll());
        localFile->close();

        SyncResult res = parseReply(reply);
        if (res.isSuccess()) {
            if (reply->hasRawHeader("ETag")) {
                res.etag = QString::fromUtf8(reply->rawHeader("ETag")).remove('"');
            }
        } else {
            QFile::remove(localFile->fileName());
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void WebDavSyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    QUrl targetUrl(remoteTmpPath.isEmpty() ? m_settings.url + QStringLiteral(".tmp") : remoteTmpPath);
    QNetworkRequest req(targetUrl);
    applyAuthHeader(req, m_settings);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    auto localFile = std::make_shared<QFile>(localPath);
    if (!localFile->open(QIODevice::ReadOnly)) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local database for reading: ") + localPath;
        if (cb) cb(res);
        return;
    }

    QNetworkReply* reply = getNetMgr()->put(req, localFile.get());
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        if (!m_settings.verifySsl) {
            reply->ignoreSslErrors();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, localFile, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res = parseReply(reply);
        if (reply->error() == QNetworkReply::NoError &&
            (res.httpStatusCode == 200 || res.httpStatusCode == 201 || res.httpStatusCode == 204)) {
            res.status = SyncResult::Status::Success;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void WebDavSyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    QUrl srcUrl(srcRemotePath.isEmpty() ? m_settings.url + QStringLiteral(".tmp") : srcRemotePath);
    QUrl destUrl(destRemotePath.isEmpty() ? m_settings.url : destRemotePath);

    QNetworkRequest req(srcUrl);
    applyAuthHeader(req, m_settings);
    req.setRawHeader("Destination", destUrl.toString().toUtf8());
    req.setRawHeader("Overwrite", "T");

    QNetworkReply* reply = getNetMgr()->sendCustomRequest(req, "MOVE");
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        if (!m_settings.verifySsl) {
            reply->ignoreSslErrors();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res = parseReply(reply);
        if (reply->error() == QNetworkReply::NoError &&
            (res.httpStatusCode == 200 || res.httpStatusCode == 201 || res.httpStatusCode == 204)) {
            res.status = SyncResult::Status::Success;
        }

        reply->deleteLater();
        if (cb) cb(res);
    });
}

void WebDavSyncProvider::cancelAll()
{
    for (auto reply : m_activeReplies) {
        if (reply && reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}
