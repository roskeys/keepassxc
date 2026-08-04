#include "SyncProviderFactory.h"
#include "providers/WebDavSyncProvider.h"

ISyncProvider* SyncProviderFactory::create(RemoteSyncSettings::Protocol protocol, QObject* parent)
{
    switch (protocol) {
    case RemoteSyncSettings::Protocol::WebDAV:
        return new WebDavSyncProvider(parent);
    case RemoteSyncSettings::Protocol::SFTP:
    case RemoteSyncSettings::Protocol::S3:
    case RemoteSyncSettings::Protocol::FTPS:
    default:
        // Default to WebDAV for now until plugins are built
        return new WebDavSyncProvider(parent);
    }
}
