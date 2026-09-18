#include "SyncProviderFactory.h"
#include "providers/GitSyncProvider.h"
#include "providers/S3SyncProvider.h"
#include "providers/SftpSyncProvider.h"
#include "providers/WebDavSyncProvider.h"

ISyncProvider* SyncProviderFactory::create(RemoteSyncSettings::Protocol protocol, QObject* parent)
{
    switch (protocol) {
    case RemoteSyncSettings::Protocol::SFTP:
        return new SftpSyncProvider(parent);
    case RemoteSyncSettings::Protocol::S3:
        return new S3SyncProvider(parent);
    case RemoteSyncSettings::Protocol::Git:
        return new GitSyncProvider(parent);
    case RemoteSyncSettings::Protocol::WebDAV:
    case RemoteSyncSettings::Protocol::FTPS:
    default:
        return new WebDavSyncProvider(parent);
    }
}
