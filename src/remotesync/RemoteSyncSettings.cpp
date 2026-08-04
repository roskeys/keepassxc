#include "RemoteSyncSettings.h"

#include "core/CustomData.h"
#include "core/Database.h"
#include "core/Metadata.h"

static const QString KEY_ENABLED = QStringLiteral("KPXC_REMOTESYNC_ENABLED");
static const QString KEY_PROTOCOL = QStringLiteral("KPXC_REMOTESYNC_PROTOCOL");
static const QString KEY_URL = QStringLiteral("KPXC_REMOTESYNC_URL");
static const QString KEY_REMOTEPATH = QStringLiteral("KPXC_REMOTESYNC_REMOTEPATH");
static const QString KEY_USERNAME = QStringLiteral("KPXC_REMOTESYNC_USERNAME");
static const QString KEY_PASSWORD = QStringLiteral("KPXC_REMOTESYNC_PASSWORD");
static const QString KEY_INTERVAL = QStringLiteral("KPXC_REMOTESYNC_INTERVAL");
static const QString KEY_VERIFY_SSL = QStringLiteral("KPXC_REMOTESYNC_VERIFY_SSL");
static const QString KEY_S3_BUCKET = QStringLiteral("KPXC_REMOTESYNC_S3_BUCKET");
static const QString KEY_S3_REGION = QStringLiteral("KPXC_REMOTESYNC_S3_REGION");
static const QString KEY_SFTP_KEYPATH = QStringLiteral("KPXC_REMOTESYNC_SFTP_KEYPATH");

QString RemoteSyncSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        return {};
    }

    QString trimmedPath = remotePath.trimmed();
    if (trimmedPath.startsWith(QLatin1Char('/'))) {
        trimmedPath.remove(0, 1);
    }

    if (trimmedPath.isEmpty()) {
        if (trimmedUrl.endsWith(QLatin1String(".kdbx"), Qt::CaseInsensitive)) {
            return trimmedUrl;
        }
        if (!trimmedUrl.endsWith(QLatin1Char('/'))) {
            trimmedUrl += QLatin1Char('/');
        }
        return trimmedUrl + (defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName);
    }

    if (!trimmedUrl.endsWith(QLatin1Char('/'))) {
        trimmedUrl += QLatin1Char('/');
    }

    return trimmedUrl + trimmedPath;
}

QString RemoteSyncSettings::protocolToString(Protocol p)
{
    switch (p) {
    case Protocol::SFTP:
        return QStringLiteral("sftp");
    case Protocol::S3:
        return QStringLiteral("s3");
    case Protocol::FTPS:
        return QStringLiteral("ftps");
    case Protocol::WebDAV:
    default:
        return QStringLiteral("webdav");
    }
}

RemoteSyncSettings::Protocol RemoteSyncSettings::protocolFromString(const QString& str)
{
    if (str == QLatin1String("sftp")) {
        return Protocol::SFTP;
    } else if (str == QLatin1String("s3")) {
        return Protocol::S3;
    } else if (str == QLatin1String("ftps")) {
        return Protocol::FTPS;
    }
    return Protocol::WebDAV;
}

RemoteSyncSettings RemoteSyncSettings::fromDatabase(const Database* db)
{
    RemoteSyncSettings s;
    if (!db || !db->metadata() || !db->metadata()->customData()) {
        return s;
    }

    const auto* cd = db->metadata()->customData();
    s.enabled = (cd->value(KEY_ENABLED) == QLatin1String("true"));
    s.protocol = protocolFromString(cd->value(KEY_PROTOCOL));
    s.url = cd->value(KEY_URL);
    s.remotePath = cd->value(KEY_REMOTEPATH);
    s.username = cd->value(KEY_USERNAME);
    s.password = cd->value(KEY_PASSWORD);

    bool ok = false;
    int interval = cd->value(KEY_INTERVAL).toInt(&ok);
    if (ok && interval >= 30) {
        s.intervalSeconds = interval;
    } else {
        s.intervalSeconds = 300;
    }

    s.verifySsl = (cd->value(KEY_VERIFY_SSL) != QLatin1String("false"));
    s.s3Bucket = cd->value(KEY_S3_BUCKET);
    s.s3Region = cd->value(KEY_S3_REGION);
    s.sftpKeyPath = cd->value(KEY_SFTP_KEYPATH);

    return s;
}

void RemoteSyncSettings::saveToDatabase(Database* db) const
{
    if (!db || !db->metadata() || !db->metadata()->customData()) {
        return;
    }

    auto* cd = db->metadata()->customData();
    cd->set(KEY_ENABLED, enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_PROTOCOL, protocolToString(protocol));
    cd->set(KEY_URL, url);
    cd->set(KEY_REMOTEPATH, remotePath);
    cd->set(KEY_USERNAME, username);
    cd->set(KEY_PASSWORD, password);
    cd->set(KEY_INTERVAL, QString::number(intervalSeconds));
    cd->set(KEY_VERIFY_SSL, verifySsl ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_S3_BUCKET, s3Bucket);
    cd->set(KEY_S3_REGION, s3Region);
    cd->set(KEY_SFTP_KEYPATH, sftpKeyPath);
}
