#ifndef KEEPASSXC_REMOTESYNCSETTINGS_H
#define KEEPASSXC_REMOTESYNCSETTINGS_H

#include <QString>

class Database;

struct RemoteSyncSettings
{
    enum class Protocol
    {
        WebDAV,
        SFTP,
        S3,
        FTPS
    };

    bool enabled = false;
    Protocol protocol = Protocol::WebDAV;
    QString url;
    QString remotePath;
    QString username;
    QString password;
    int intervalSeconds = 300;
    bool verifySsl = true;

    // Protocol-specific options
    QString s3Bucket;
    QString s3Region;
    QString sftpKeyPath;

    QString fullRemoteUrl(const QString& defaultFileName = {}) const;

    static QString protocolToString(Protocol p);
    static Protocol protocolFromString(const QString& str);

    static RemoteSyncSettings fromDatabase(const Database* db);
    void saveToDatabase(Database* db) const;
};

#endif // KEEPASSXC_REMOTESYNCSETTINGS_H
