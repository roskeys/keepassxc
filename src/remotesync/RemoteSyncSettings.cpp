#include "RemoteSyncSettings.h"

#include "core/CustomData.h"
#include "core/Database.h"
#include "core/Metadata.h"

// Legacy Keys
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

// WebDAV Keys
static const QString KEY_WEBDAV_ENABLED = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_ENABLED");
static const QString KEY_WEBDAV_URL = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_URL");
static const QString KEY_WEBDAV_REMOTEPATH = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_REMOTEPATH");
static const QString KEY_WEBDAV_USERNAME = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_USERNAME");
static const QString KEY_WEBDAV_PASSWORD = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_PASSWORD");
static const QString KEY_WEBDAV_VERIFY_SSL = QStringLiteral("KPXC_REMOTESYNC_WEBDAV_VERIFY_SSL");

// Dropbox Keys
static const QString KEY_DROPBOX_ENABLED = QStringLiteral("KPXC_REMOTESYNC_DROPBOX_ENABLED");
static const QString KEY_DROPBOX_TOKEN = QStringLiteral("KPXC_REMOTESYNC_DROPBOX_TOKEN");
static const QString KEY_DROPBOX_REMOTEPATH = QStringLiteral("KPXC_REMOTESYNC_DROPBOX_REMOTEPATH");
static const QString KEY_DROPBOX_APPKEY = QStringLiteral("KPXC_REMOTESYNC_DROPBOX_APPKEY");
static const QString KEY_DROPBOX_APPSECRET = QStringLiteral("KPXC_REMOTESYNC_DROPBOX_APPSECRET");

// Google Drive Keys
static const QString KEY_GDRIVE_ENABLED = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_ENABLED");
static const QString KEY_GDRIVE_TOKEN = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_TOKEN");
static const QString KEY_GDRIVE_REFRESH_TOKEN = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_REFRESH_TOKEN");
static const QString KEY_GDRIVE_CLIENT_ID = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_CLIENT_ID");
static const QString KEY_GDRIVE_CLIENT_SECRET = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_CLIENT_SECRET");
static const QString KEY_GDRIVE_REMOTEPATH = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_REMOTEPATH");
static const QString KEY_GDRIVE_FOLDERID = QStringLiteral("KPXC_REMOTESYNC_GDRIVE_FOLDERID");

// SFTP Keys
static const QString KEY_SFTP_ENABLED = QStringLiteral("KPXC_REMOTESYNC_SFTP_ENABLED");
static const QString KEY_SFTP_HOST = QStringLiteral("KPXC_REMOTESYNC_SFTP_HOST");
static const QString KEY_SFTP_PORT = QStringLiteral("KPXC_REMOTESYNC_SFTP_PORT");
static const QString KEY_SFTP_REMOTEPATH_NEW = QStringLiteral("KPXC_REMOTESYNC_SFTP_REMOTEPATH");
static const QString KEY_SFTP_USERNAME = QStringLiteral("KPXC_REMOTESYNC_SFTP_USERNAME");
static const QString KEY_SFTP_PASSWORD = QStringLiteral("KPXC_REMOTESYNC_SFTP_PASSWORD");
static const QString KEY_SFTP_KEYPATH_NEW = QStringLiteral("KPXC_REMOTESYNC_SFTP_KEYPATH_NEW");

// S3 Keys
static const QString KEY_S3_ENABLED = QStringLiteral("KPXC_REMOTESYNC_S3_ENABLED");
static const QString KEY_S3_ENDPOINT = QStringLiteral("KPXC_REMOTESYNC_S3_ENDPOINT");
static const QString KEY_S3_BUCKET_NEW = QStringLiteral("KPXC_REMOTESYNC_S3_BUCKET_NEW");
static const QString KEY_S3_REGION_NEW = QStringLiteral("KPXC_REMOTESYNC_S3_REGION_NEW");
static const QString KEY_S3_ACCESSKEY = QStringLiteral("KPXC_REMOTESYNC_S3_ACCESSKEY");
static const QString KEY_S3_SECRETKEY = QStringLiteral("KPXC_REMOTESYNC_S3_SECRETKEY");
static const QString KEY_S3_REMOTEPATH_NEW = QStringLiteral("KPXC_REMOTESYNC_S3_REMOTEPATH");
static const QString KEY_S3_VERIFY_SSL = QStringLiteral("KPXC_REMOTESYNC_S3_VERIFY_SSL");

// Git Keys
static const QString KEY_GIT_ENABLED = QStringLiteral("KPXC_REMOTESYNC_GIT_ENABLED");
static const QString KEY_GIT_REPOURL = QStringLiteral("KPXC_REMOTESYNC_GIT_REPOURL");
static const QString KEY_GIT_BRANCH = QStringLiteral("KPXC_REMOTESYNC_GIT_BRANCH");
static const QString KEY_GIT_REMOTEPATH = QStringLiteral("KPXC_REMOTESYNC_GIT_REMOTEPATH");
static const QString KEY_GIT_USERNAME = QStringLiteral("KPXC_REMOTESYNC_GIT_USERNAME");
static const QString KEY_GIT_PASSWORD = QStringLiteral("KPXC_REMOTESYNC_GIT_PASSWORD");
static const QString KEY_GIT_KEYPATH = QStringLiteral("KPXC_REMOTESYNC_GIT_KEYPATH");
static const QString KEY_GIT_AUTHORNAME = QStringLiteral("KPXC_REMOTESYNC_GIT_AUTHORNAME");
static const QString KEY_GIT_AUTHOREMAIL = QStringLiteral("KPXC_REMOTESYNC_GIT_AUTHOREMAIL");

QString WebDavSettings::fullRemoteUrl(const QString& defaultFileName) const
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

QString SftpSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString cleanHost = host.trimmed();
    if (cleanHost.startsWith(QLatin1String("sftp://"), Qt::CaseInsensitive)) {
        cleanHost = cleanHost.mid(7);
    }

    QString path = remotePath.trimmed();
    if (path.isEmpty()) {
        path = defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName;
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }

    return QStringLiteral("sftp://%1%2%3:%4%5")
        .arg(username.isEmpty() ? QString() : username + QLatin1Char('@'),
             cleanHost,
             QString(),
             QString::number(port > 0 ? port : 22),
             path);
}

QString S3Settings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString path = remotePath.trimmed();
    if (path.isEmpty()) {
        path = defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName;
    }
    if (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }

    return QStringLiteral("s3://%1/%2").arg(bucket.trimmed(), path);
}

QString GitSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString path = remotePath.trimmed();
    if (path.isEmpty()) {
        path = defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName;
    }
    return QStringLiteral("%1#%2:%3").arg(repoUrl.trimmed(), branch.isEmpty() ? QStringLiteral("main") : branch.trimmed(), path);
}

QString DropboxSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString path = remotePath.trimmed();
    if (path.isEmpty()) {
        path = defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName;
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return QStringLiteral("dropbox:%1").arg(path);
}

QString GoogleDriveSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    QString path = remotePath.trimmed();
    if (path.isEmpty()) {
        path = defaultFileName.isEmpty() ? QStringLiteral("passwords.kdbx") : defaultFileName;
    }
    if (!folderId.trimmed().isEmpty()) {
        return QStringLiteral("googledrive://%1/%2").arg(folderId.trimmed(), path);
    }
    return QStringLiteral("googledrive:/%1").arg(path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path);
}

QString RemoteSyncSettings::fullRemoteUrl(const QString& defaultFileName) const
{
    if (webdav.enabled) {
        return webdav.fullRemoteUrl(defaultFileName);
    }
    if (dropbox.enabled) {
        return dropbox.fullRemoteUrl(defaultFileName);
    }
    if (googleDrive.enabled) {
        return googleDrive.fullRemoteUrl(defaultFileName);
    }
    if (sftp.enabled) {
        return sftp.fullRemoteUrl(defaultFileName);
    }
    if (s3.enabled) {
        return s3.fullRemoteUrl(defaultFileName);
    }
    if (git.enabled) {
        return git.fullRemoteUrl(defaultFileName);
    }
    return {};
}

QString RemoteSyncSettings::protocolToString(Protocol p)
{
    switch (p) {
    case Protocol::Dropbox:
        return QStringLiteral("dropbox");
    case Protocol::GoogleDrive:
        return QStringLiteral("googledrive");
    case Protocol::SFTP:
        return QStringLiteral("sftp");
    case Protocol::S3:
        return QStringLiteral("s3");
    case Protocol::FTPS:
        return QStringLiteral("ftps");
    case Protocol::Git:
        return QStringLiteral("git");
    case Protocol::WebDAV:
    default:
        return QStringLiteral("webdav");
    }
}

RemoteSyncSettings::Protocol RemoteSyncSettings::protocolFromString(const QString& str)
{
    if (str == QLatin1String("dropbox")) {
        return Protocol::Dropbox;
    }
    if (str == QLatin1String("googledrive")) {
        return Protocol::GoogleDrive;
    }
    if (str == QLatin1String("sftp")) {
        return Protocol::SFTP;
    }
    if (str == QLatin1String("s3")) {
        return Protocol::S3;
    }
    if (str == QLatin1String("ftps")) {
        return Protocol::FTPS;
    }
    if (str == QLatin1String("git")) {
        return Protocol::Git;
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

    auto getVal = [cd](const QString& key, const QString& def = QString()) -> QString {
        return cd->contains(key) ? cd->value(key) : def;
    };

    // Global interval
    if (cd->contains(KEY_INTERVAL)) {
        s.intervalSeconds = cd->value(KEY_INTERVAL).toInt();
        if (s.intervalSeconds <= 0) {
            s.intervalSeconds = 300;
        }
    }

    // 1. WebDAV Settings
    if (cd->contains(KEY_WEBDAV_ENABLED)) {
        s.webdav.enabled = (cd->value(KEY_WEBDAV_ENABLED) == QLatin1String("true"));
        s.webdav.url = cd->value(KEY_WEBDAV_URL);
        s.webdav.remotePath = cd->value(KEY_WEBDAV_REMOTEPATH);
        s.webdav.username = cd->value(KEY_WEBDAV_USERNAME);
        s.webdav.password = cd->value(KEY_WEBDAV_PASSWORD);
        s.webdav.verifySsl = (getVal(KEY_WEBDAV_VERIFY_SSL, QStringLiteral("true")) == QLatin1String("true"));
    } else if (cd->contains(KEY_ENABLED) && cd->value(KEY_ENABLED) == QLatin1String("true")) {
        // Legacy migration: if legacy sync was enabled with WebDAV
        QString proto = cd->value(KEY_PROTOCOL);
        if (proto.isEmpty() || proto == QLatin1String("webdav")) {
            s.webdav.enabled = true;
            s.webdav.url = cd->value(KEY_URL);
            s.webdav.remotePath = cd->value(KEY_REMOTEPATH);
            s.webdav.username = cd->value(KEY_USERNAME);
            s.webdav.password = cd->value(KEY_PASSWORD);
            s.webdav.verifySsl = (getVal(KEY_VERIFY_SSL, QStringLiteral("true")) == QLatin1String("true"));
        }
    }

    // 2. Dropbox Settings
    if (cd->contains(KEY_DROPBOX_ENABLED)) {
        s.dropbox.enabled = (cd->value(KEY_DROPBOX_ENABLED) == QLatin1String("true"));
        s.dropbox.accessToken = cd->value(KEY_DROPBOX_TOKEN);
        s.dropbox.remotePath = cd->value(KEY_DROPBOX_REMOTEPATH);
        s.dropbox.appKey = cd->value(KEY_DROPBOX_APPKEY);
        s.dropbox.appSecret = cd->value(KEY_DROPBOX_APPSECRET);
    }

    // 3. Google Drive Settings
    if (cd->contains(KEY_GDRIVE_ENABLED)) {
        s.googleDrive.enabled = (cd->value(KEY_GDRIVE_ENABLED) == QLatin1String("true"));
        s.googleDrive.accessToken = cd->value(KEY_GDRIVE_TOKEN);
        s.googleDrive.refreshToken = cd->value(KEY_GDRIVE_REFRESH_TOKEN);
        s.googleDrive.clientId = cd->value(KEY_GDRIVE_CLIENT_ID);
        s.googleDrive.clientSecret = cd->value(KEY_GDRIVE_CLIENT_SECRET);
        s.googleDrive.remotePath = cd->value(KEY_GDRIVE_REMOTEPATH);
        s.googleDrive.folderId = cd->value(KEY_GDRIVE_FOLDERID);
    }

    // 3. SFTP Settings
    if (cd->contains(KEY_SFTP_ENABLED)) {
        s.sftp.enabled = (cd->value(KEY_SFTP_ENABLED) == QLatin1String("true"));
        s.sftp.host = cd->value(KEY_SFTP_HOST);
        s.sftp.port = getVal(KEY_SFTP_PORT, QStringLiteral("22")).toInt();
        s.sftp.remotePath = cd->value(KEY_SFTP_REMOTEPATH_NEW);
        s.sftp.username = cd->value(KEY_SFTP_USERNAME);
        s.sftp.password = cd->value(KEY_SFTP_PASSWORD);
        s.sftp.keyPath = cd->value(KEY_SFTP_KEYPATH_NEW);
    } else if (cd->contains(KEY_ENABLED) && cd->value(KEY_PROTOCOL) == QLatin1String("sftp")) {
        // Legacy migration for SFTP
        s.sftp.enabled = (cd->value(KEY_ENABLED) == QLatin1String("true"));
        s.sftp.host = cd->value(KEY_URL);
        s.sftp.port = 22;
        s.sftp.remotePath = cd->value(KEY_REMOTEPATH);
        s.sftp.username = cd->value(KEY_USERNAME);
        s.sftp.password = cd->value(KEY_PASSWORD);
        s.sftp.keyPath = cd->value(KEY_SFTP_KEYPATH);
    }

    // 3. S3 Settings
    if (cd->contains(KEY_S3_ENABLED)) {
        s.s3.enabled = (cd->value(KEY_S3_ENABLED) == QLatin1String("true"));
        s.s3.endpoint = cd->value(KEY_S3_ENDPOINT);
        s.s3.bucket = cd->value(KEY_S3_BUCKET_NEW);
        s.s3.region = getVal(KEY_S3_REGION_NEW, QStringLiteral("us-east-1"));
        s.s3.accessKey = cd->value(KEY_S3_ACCESSKEY);
        s.s3.secretKey = cd->value(KEY_S3_SECRETKEY);
        s.s3.remotePath = cd->value(KEY_S3_REMOTEPATH_NEW);
        s.s3.verifySsl = (getVal(KEY_S3_VERIFY_SSL, QStringLiteral("true")) == QLatin1String("true"));
    } else if (cd->contains(KEY_ENABLED) && cd->value(KEY_PROTOCOL) == QLatin1String("s3")) {
        // Legacy migration for S3
        s.s3.enabled = (cd->value(KEY_ENABLED) == QLatin1String("true"));
        s.s3.endpoint = cd->value(KEY_URL);
        s.s3.bucket = cd->value(KEY_S3_BUCKET);
        s.s3.region = getVal(KEY_S3_REGION, QStringLiteral("us-east-1"));
        s.s3.accessKey = cd->value(KEY_USERNAME);
        s.s3.secretKey = cd->value(KEY_PASSWORD);
        s.s3.remotePath = cd->value(KEY_REMOTEPATH);
        s.s3.verifySsl = (getVal(KEY_VERIFY_SSL, QStringLiteral("true")) == QLatin1String("true"));
    }

    // 4. Git Settings
    if (cd->contains(KEY_GIT_ENABLED)) {
        s.git.enabled = (cd->value(KEY_GIT_ENABLED) == QLatin1String("true"));
        s.git.repoUrl = cd->value(KEY_GIT_REPOURL);
        s.git.branch = getVal(KEY_GIT_BRANCH, QStringLiteral("main"));
        s.git.remotePath = cd->value(KEY_GIT_REMOTEPATH);
        s.git.username = cd->value(KEY_GIT_USERNAME);
        s.git.password = cd->value(KEY_GIT_PASSWORD);
        s.git.keyPath = cd->value(KEY_GIT_KEYPATH);
        s.git.authorName = cd->value(KEY_GIT_AUTHORNAME);
        s.git.authorEmail = cd->value(KEY_GIT_AUTHOREMAIL);
    }

    // Populate legacy fields for compatibility
    s.enabled = s.isAnyEnabled();
    if (s.webdav.enabled) {
        s.protocol = Protocol::WebDAV;
        s.url = s.webdav.url;
        s.remotePath = s.webdav.remotePath;
        s.username = s.webdav.username;
        s.password = s.webdav.password;
        s.verifySsl = s.webdav.verifySsl;
    } else if (s.sftp.enabled) {
        s.protocol = Protocol::SFTP;
        s.url = s.sftp.host;
        s.remotePath = s.sftp.remotePath;
        s.username = s.sftp.username;
        s.password = s.sftp.password;
        s.sftpKeyPath = s.sftp.keyPath;
    } else if (s.s3.enabled) {
        s.protocol = Protocol::S3;
        s.url = s.s3.endpoint;
        s.s3Bucket = s.s3.bucket;
        s.s3Region = s.s3.region;
        s.username = s.s3.accessKey;
        s.password = s.s3.secretKey;
        s.remotePath = s.s3.remotePath;
        s.verifySsl = s.s3.verifySsl;
    }

    return s;
}

void RemoteSyncSettings::saveToDatabase(Database* db) const
{
    if (!db || !db->metadata() || !db->metadata()->customData()) {
        return;
    }

    auto* cd = db->metadata()->customData();

    // Global
    cd->set(KEY_ENABLED, isAnyEnabled() ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_INTERVAL, QString::number(intervalSeconds));

    // WebDAV
    cd->set(KEY_WEBDAV_ENABLED, webdav.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_WEBDAV_URL, webdav.url);
    cd->set(KEY_WEBDAV_REMOTEPATH, webdav.remotePath);
    cd->set(KEY_WEBDAV_USERNAME, webdav.username);
    cd->set(KEY_WEBDAV_PASSWORD, webdav.password);
    cd->set(KEY_WEBDAV_VERIFY_SSL, webdav.verifySsl ? QStringLiteral("true") : QStringLiteral("false"));

    // Dropbox
    cd->set(KEY_DROPBOX_ENABLED, dropbox.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_DROPBOX_TOKEN, dropbox.accessToken);
    cd->set(KEY_DROPBOX_REMOTEPATH, dropbox.remotePath);
    cd->set(KEY_DROPBOX_APPKEY, dropbox.appKey);
    cd->set(KEY_DROPBOX_APPSECRET, dropbox.appSecret);

    // Google Drive
    cd->set(KEY_GDRIVE_ENABLED, googleDrive.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_GDRIVE_TOKEN, googleDrive.accessToken);
    cd->set(KEY_GDRIVE_REFRESH_TOKEN, googleDrive.refreshToken);
    cd->set(KEY_GDRIVE_CLIENT_ID, googleDrive.clientId);
    cd->set(KEY_GDRIVE_CLIENT_SECRET, googleDrive.clientSecret);
    cd->set(KEY_GDRIVE_REMOTEPATH, googleDrive.remotePath);
    cd->set(KEY_GDRIVE_FOLDERID, googleDrive.folderId);

    // SFTP
    cd->set(KEY_SFTP_ENABLED, sftp.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_SFTP_HOST, sftp.host);
    cd->set(KEY_SFTP_PORT, QString::number(sftp.port > 0 ? sftp.port : 22));
    cd->set(KEY_SFTP_REMOTEPATH_NEW, sftp.remotePath);
    cd->set(KEY_SFTP_USERNAME, sftp.username);
    cd->set(KEY_SFTP_PASSWORD, sftp.password);
    cd->set(KEY_SFTP_KEYPATH_NEW, sftp.keyPath);

    // S3
    cd->set(KEY_S3_ENABLED, s3.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_S3_ENDPOINT, s3.endpoint);
    cd->set(KEY_S3_BUCKET_NEW, s3.bucket);
    cd->set(KEY_S3_REGION_NEW, s3.region);
    cd->set(KEY_S3_ACCESSKEY, s3.accessKey);
    cd->set(KEY_S3_SECRETKEY, s3.secretKey);
    cd->set(KEY_S3_REMOTEPATH_NEW, s3.remotePath);
    cd->set(KEY_S3_VERIFY_SSL, s3.verifySsl ? QStringLiteral("true") : QStringLiteral("false"));

    // Git
    cd->set(KEY_GIT_ENABLED, git.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    cd->set(KEY_GIT_REPOURL, git.repoUrl);
    cd->set(KEY_GIT_BRANCH, git.branch.isEmpty() ? QStringLiteral("main") : git.branch);
    cd->set(KEY_GIT_REMOTEPATH, git.remotePath);
    cd->set(KEY_GIT_USERNAME, git.username);
    cd->set(KEY_GIT_PASSWORD, git.password);
    cd->set(KEY_GIT_KEYPATH, git.keyPath);
    cd->set(KEY_GIT_AUTHORNAME, git.authorName);
    cd->set(KEY_GIT_AUTHOREMAIL, git.authorEmail);

    // Keep legacy keys updated for compatibility
    if (webdav.enabled) {
        cd->set(KEY_PROTOCOL, QStringLiteral("webdav"));
        cd->set(KEY_URL, webdav.url);
        cd->set(KEY_REMOTEPATH, webdav.remotePath);
        cd->set(KEY_USERNAME, webdav.username);
        cd->set(KEY_PASSWORD, webdav.password);
        cd->set(KEY_VERIFY_SSL, webdav.verifySsl ? QStringLiteral("true") : QStringLiteral("false"));
    } else if (sftp.enabled) {
        cd->set(KEY_PROTOCOL, QStringLiteral("sftp"));
        cd->set(KEY_URL, sftp.host);
        cd->set(KEY_REMOTEPATH, sftp.remotePath);
        cd->set(KEY_USERNAME, sftp.username);
        cd->set(KEY_PASSWORD, sftp.password);
        cd->set(KEY_SFTP_KEYPATH, sftp.keyPath);
    } else if (s3.enabled) {
        cd->set(KEY_PROTOCOL, QStringLiteral("s3"));
        cd->set(KEY_URL, s3.endpoint);
        cd->set(KEY_S3_BUCKET, s3.bucket);
        cd->set(KEY_S3_REGION, s3.region);
        cd->set(KEY_USERNAME, s3.accessKey);
        cd->set(KEY_PASSWORD, s3.secretKey);
        cd->set(KEY_REMOTEPATH, s3.remotePath);
        cd->set(KEY_VERIFY_SSL, s3.verifySsl ? QStringLiteral("true") : QStringLiteral("false"));
    }
}
