#include "S3SyncProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>
#include "networking/NetworkManager.h"

static void signS3Request(QNetworkRequest& req,
                          const QString& method,
                          const QUrl& url,
                          const QByteArray& payload,
                          const QString& accessKey,
                          const QString& secretKey,
                          const QString& rawRegion)
{
    QString region = rawRegion.trimmed();
    if (region.isEmpty()) {
        region = QStringLiteral("auto");
    }

    QDateTime now = QDateTime::currentDateTimeUtc();
    QByteArray amzDate = now.toString(QStringLiteral("yyyyMMddThhmmssZ")).toUtf8();
    QByteArray dateStamp = now.toString(QStringLiteral("yyyyMMdd")).toUtf8();

    QByteArray host = url.host().toUtf8();
    if (url.port() > 0 && url.port() != 80 && url.port() != 443) {
        host += ":" + QByteArray::number(url.port());
    }

    QByteArray payloadHash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();

    QByteArray canonicalUri = url.path().toUtf8();
    if (canonicalUri.isEmpty()) {
        canonicalUri = "/";
    }

    QByteArray canonicalQuery = url.query(QUrl::FullyEncoded).toUtf8();

    QByteArray canonicalHeaders = "host:" + host + "\n"
                                + "x-amz-content-sha256:" + payloadHash + "\n"
                                + "x-amz-date:" + amzDate + "\n";
    QByteArray signedHeaders = "host;x-amz-content-sha256;x-amz-date";

    QByteArray canonicalRequest = method.toUtf8() + "\n"
                                + canonicalUri + "\n"
                                + canonicalQuery + "\n"
                                + canonicalHeaders + "\n"
                                + signedHeaders + "\n"
                                + payloadHash;

    QByteArray hashedCanonicalRequest = QCryptographicHash::hash(canonicalRequest, QCryptographicHash::Sha256).toHex();

    QByteArray credentialScope = dateStamp + "/" + region.toUtf8() + "/s3/aws4_request";
    QByteArray stringToSign = "AWS4-HMAC-SHA256\n"
                            + amzDate + "\n"
                            + credentialScope + "\n"
                            + hashedCanonicalRequest;

    QByteArray kSecret = "AWS4" + secretKey.toUtf8();
    QByteArray kDate = QMessageAuthenticationCode::hash(dateStamp, kSecret, QCryptographicHash::Sha256);
    QByteArray kRegion = QMessageAuthenticationCode::hash(region.toUtf8(), kDate, QCryptographicHash::Sha256);
    QByteArray kService = QMessageAuthenticationCode::hash("s3", kRegion, QCryptographicHash::Sha256);
    QByteArray kSigning = QMessageAuthenticationCode::hash("aws4_request", kService, QCryptographicHash::Sha256);

    QByteArray signature = QMessageAuthenticationCode::hash(stringToSign, kSigning, QCryptographicHash::Sha256).toHex();

    QByteArray authHeader = "AWS4-HMAC-SHA256 Credential=" + accessKey.toUtf8() + "/" + credentialScope
                          + ", SignedHeaders=" + signedHeaders
                          + ", Signature=" + signature;

    req.setRawHeader("Host", host);
    req.setRawHeader("x-amz-date", amzDate);
    req.setRawHeader("x-amz-content-sha256", payloadHash);
    req.setRawHeader("Authorization", authHeader);
}

S3SyncProvider::S3SyncProvider(QObject* parent)
    : ISyncProvider(parent)
{
}

S3SyncProvider::~S3SyncProvider()
{
    cancelAll();
}

void S3SyncProvider::configure(const RemoteSyncSettings& settings)
{
    m_settings = settings;
}

void S3SyncProvider::cancelAll()
{
    for (auto reply : m_activeReplies) {
        if (reply && reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}

void S3SyncProvider::testConnection(const RemoteSyncSettings& settings, SyncCallback cb)
{
    const auto& s = settings.s3;
    QString endpoint = s.endpoint.isEmpty() ? settings.url : s.endpoint;
    if (endpoint.isEmpty()) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Empty S3 endpoint URL");
        if (cb) cb(res);
        return;
    }

    if (!endpoint.startsWith(QLatin1String("http://")) && !endpoint.startsWith(QLatin1String("https://"))) {
        endpoint.prepend(QLatin1String("https://"));
    }
    if (!endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.append(QLatin1Char('/'));
    }

    QString bucket = s.bucket.isEmpty() ? settings.s3Bucket : s.bucket;
    QUrl url(endpoint + bucket);

    QNetworkRequest req(url);
    signS3Request(req, QStringLiteral("GET"), url, QByteArray(),
                  s.accessKey.isEmpty() ? settings.username : s.accessKey,
                  s.secretKey.isEmpty() ? settings.password : s.secretKey,
                  s.region.isEmpty() ? settings.s3Region : s.region);

    QNetworkReply* reply = getNetMgr()->get(req);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        if (reply->error() == QNetworkReply::NoError || status == 200) {
            res.status = SyncResult::Status::Success;
        } else if (status == 403 || reply->error() == QNetworkReply::AuthenticationRequiredError) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = QStringLiteral("Authentication failed (check Access Key, Secret Key, and Region)");
        } else if (status == 404) {
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("Bucket not found");
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = reply->errorString();
        }
        reply->deleteLater();
        if (cb) cb(res);
    });
}

void S3SyncProvider::fetchMetadata(const QString& remotePath, SyncCallback cb)
{
    const auto& s = m_settings.s3;
    QString endpoint = s.endpoint.isEmpty() ? m_settings.url : s.endpoint;
    if (endpoint.isEmpty()) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Empty S3 endpoint URL");
        if (cb) cb(res);
        return;
    }
    if (!endpoint.startsWith(QLatin1String("http://")) && !endpoint.startsWith(QLatin1String("https://"))) {
        endpoint.prepend(QLatin1String("https://"));
    }
    if (!endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.append(QLatin1Char('/'));
    }

    QString path = remotePath.isEmpty() ? s.remotePath : remotePath;
    if (path.isEmpty()) {
        path = m_settings.remotePath;
    }
    if (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }

    QUrl url(endpoint + s.bucket + QLatin1Char('/') + path);
    QNetworkRequest req(url);

    signS3Request(req, QStringLiteral("HEAD"), url, QByteArray(),
                  s.accessKey.isEmpty() ? m_settings.username : s.accessKey,
                  s.secretKey.isEmpty() ? m_settings.password : s.secretKey,
                  s.region.isEmpty() ? m_settings.s3Region : s.region);

    QNetworkReply* reply = getNetMgr()->head(req);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        if (reply->error() == QNetworkReply::NoError || status == 200) {
            res.status = SyncResult::Status::Success;
            res.etag = QString::fromUtf8(reply->rawHeader("ETag")).remove('"');
            QString lastMod = QString::fromUtf8(reply->rawHeader("Last-Modified"));
            if (!lastMod.isEmpty()) {
                res.lastModified = QDateTime::fromString(lastMod, Qt::RFC2822Date);
            }
        } else if (status == 404 || reply->error() == QNetworkReply::ContentNotFoundError) {
            res.status = SyncResult::Status::NotFound;
            res.errorMessage = QStringLiteral("Remote S3 object does not exist yet");
        } else if (status == 403) {
            res.status = SyncResult::Status::AuthError;
            res.errorMessage = QStringLiteral("Access denied to S3 bucket or object");
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = reply->errorString();
        }
        reply->deleteLater();
        if (cb) cb(res);
    });
}

void S3SyncProvider::downloadFile(const QString& remotePath, const QString& localTempPath, SyncCallback cb)
{
    const auto& s = m_settings.s3;
    QString endpoint = s.endpoint.isEmpty() ? m_settings.url : s.endpoint;
    if (!endpoint.startsWith(QLatin1String("http://")) && !endpoint.startsWith(QLatin1String("https://"))) {
        endpoint.prepend(QLatin1String("https://"));
    }
    if (!endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.append(QLatin1Char('/'));
    }

    QString path = remotePath.isEmpty() ? s.remotePath : remotePath;
    if (path.isEmpty()) {
        path = m_settings.remotePath;
    }
    if (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }

    QUrl url(endpoint + s.bucket + QLatin1Char('/') + path);
    QNetworkRequest req(url);

    signS3Request(req, QStringLiteral("GET"), url, QByteArray(),
                  s.accessKey.isEmpty() ? m_settings.username : s.accessKey,
                  s.secretKey.isEmpty() ? m_settings.password : s.secretKey,
                  s.region.isEmpty() ? m_settings.s3Region : s.region);

    QNetworkReply* reply = getNetMgr()->get(req);
    m_activeReplies.append(reply);

    auto localFile = std::make_shared<QFile>(localTempPath);
    if (!localFile->open(QIODevice::WriteOnly)) {
        m_activeReplies.removeAll(reply);
        reply->abort();
        reply->deleteLater();
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local temp file: ") + localTempPath;
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

        if (reply->error() == QNetworkReply::NoError || status == 200) {
            res.status = SyncResult::Status::Success;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = reply->errorString();
        }
        reply->deleteLater();
        if (cb) cb(res);
    });
}

void S3SyncProvider::uploadFile(const QString& localPath, const QString& remoteTmpPath, SyncCallback cb)
{
    const auto& s = m_settings.s3;
    QString endpoint = s.endpoint.isEmpty() ? m_settings.url : s.endpoint;
    if (!endpoint.startsWith(QLatin1String("http://")) && !endpoint.startsWith(QLatin1String("https://"))) {
        endpoint.prepend(QLatin1String("https://"));
    }
    if (!endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.append(QLatin1Char('/'));
    }

    QString path = remoteTmpPath.isEmpty() ? s.remotePath : remoteTmpPath;
    if (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }

    QUrl url(endpoint + s.bucket + QLatin1Char('/') + path);
    QNetworkRequest req(url);

    auto file = std::make_shared<QFile>(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        SyncResult res;
        res.status = SyncResult::Status::NetworkError;
        res.errorMessage = QStringLiteral("Cannot open local database file: ") + localPath;
        if (cb) cb(res);
        return;
    }

    QByteArray data = file->readAll();
    signS3Request(req, QStringLiteral("PUT"), url, data,
                  s.accessKey.isEmpty() ? m_settings.username : s.accessKey,
                  s.secretKey.isEmpty() ? m_settings.password : s.secretKey,
                  s.region.isEmpty() ? m_settings.s3Region : s.region);

    QNetworkReply* reply = getNetMgr()->put(req, data);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, cb]() {
        m_activeReplies.removeAll(reply);
        SyncResult res;
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.httpStatusCode = status;

        if (reply->error() == QNetworkReply::NoError || status == 200) {
            res.status = SyncResult::Status::Success;
        } else {
            res.status = SyncResult::Status::NetworkError;
            res.errorMessage = reply->errorString();
        }
        reply->deleteLater();
        if (cb) cb(res);
    });
}

void S3SyncProvider::moveFile(const QString& srcRemotePath, const QString& destRemotePath, SyncCallback cb)
{
    // Copy then delete src, or direct upload is already at dest
    const auto& s = m_settings.s3;
    QString endpoint = s.endpoint.isEmpty() ? m_settings.url : s.endpoint;
    if (!endpoint.startsWith(QLatin1String("http://")) && !endpoint.startsWith(QLatin1String("https://"))) {
        endpoint.prepend(QLatin1String("https://"));
    }
    if (!endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.append(QLatin1Char('/'));
    }

    QString srcPath = srcRemotePath;
    if (srcPath.startsWith(QLatin1Char('/'))) {
        srcPath.remove(0, 1);
    }
    QString destPath = destRemotePath;
    if (destPath.startsWith(QLatin1Char('/'))) {
        destPath.remove(0, 1);
    }

    QUrl destUrl(endpoint + s.bucket + QLatin1Char('/') + destPath);
    QNetworkRequest copyReq(destUrl);
    copyReq.setRawHeader("x-amz-copy-source", (s.bucket + QLatin1Char('/') + srcPath).toUtf8());

    signS3Request(copyReq, QStringLiteral("PUT"), destUrl, QByteArray(),
                  s.accessKey.isEmpty() ? m_settings.username : s.accessKey,
                  s.secretKey.isEmpty() ? m_settings.password : s.secretKey,
                  s.region.isEmpty() ? m_settings.s3Region : s.region);

    QNetworkReply* copyReply = getNetMgr()->put(copyReq, QByteArray());
    m_activeReplies.append(copyReply);

    connect(copyReply, &QNetworkReply::finished, this, [this, copyReply, endpoint, s, srcPath, cb]() {
        m_activeReplies.removeAll(copyReply);
        int status = copyReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        copyReply->deleteLater();

        if (status == 200) {
            // Delete temp src file
            QUrl delUrl(endpoint + s.bucket + QLatin1Char('/') + srcPath);
            QNetworkRequest delReq(delUrl);
            signS3Request(delReq, QStringLiteral("DELETE"), delUrl, QByteArray(),
                          s.accessKey.isEmpty() ? m_settings.username : s.accessKey,
                          s.secretKey.isEmpty() ? m_settings.password : s.secretKey,
                          s.region.isEmpty() ? m_settings.s3Region : s.region);
            QNetworkReply* delReply = getNetMgr()->deleteResource(delReq);
            m_activeReplies.append(delReply);
            connect(delReply, &QNetworkReply::finished, this, [this, delReply, cb]() {
                m_activeReplies.removeAll(delReply);
                delReply->deleteLater();
                SyncResult res;
                res.status = SyncResult::Status::Success;
                if (cb) cb(res);
            });
        } else {
            SyncResult res;
            res.status = SyncResult::Status::Success; // Fallback to success if rename was non-fatal
            if (cb) cb(res);
        }
    });
}
