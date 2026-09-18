#ifndef KEEPASSXC_REMOTESYNCSETTINGS_H
#define KEEPASSXC_REMOTESYNCSETTINGS_H

#include <QString>

class Database;

struct WebDavSettings
{
    bool enabled = false;
    QString url;
    QString remotePath;
    QString username;
    QString password;
    bool verifySsl = true;

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;
};

struct SftpSettings
{
    bool enabled = false;
    QString host;
    int port = 22;
    QString remotePath;
    QString username;
    QString password;
    QString keyPath;

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;
};

struct S3Settings
{
    bool enabled = false;
    QString endpoint;
    QString bucket;
    QString region;
    QString accessKey;
    QString secretKey;
    QString remotePath;
    bool verifySsl = true;

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;
};

struct GitSettings
{
    bool enabled = false;
    QString repoUrl;
    QString branch;
    QString remotePath; // Path inside repository (defaults to passwords.kdbx)
    QString username;   // For HTTPS auth or author name
    QString password;   // Token / Password / SSH passphrase
    QString keyPath;    // SSH private key path
    QString authorName;
    QString authorEmail;

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;
};

struct RemoteSyncSettings
{
    enum class Protocol
    {
        WebDAV,
        SFTP,
        S3,
        FTPS,
        Git
    };

    WebDavSettings webdav;
    SftpSettings sftp;
    S3Settings s3;
    GitSettings git;

    int intervalSeconds = 300;

    // Legacy fields for backward compatibility
    bool enabled = false;
    Protocol protocol = Protocol::WebDAV;
    QString url;
    QString remotePath;
    QString username;
    QString password;
    bool verifySsl = true;
    QString s3Bucket;
    QString s3Region;
    QString sftpKeyPath;

    bool isAnyEnabled() const
    {
        return webdav.enabled || sftp.enabled || s3.enabled || git.enabled;
    }

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;

    static QString protocolToString(Protocol p);
    static Protocol protocolFromString(const QString& str);

    static RemoteSyncSettings fromDatabase(const Database* db);
    void saveToDatabase(Database* db) const;
};

#endif // KEEPASSXC_REMOTESYNCSETTINGS_H
