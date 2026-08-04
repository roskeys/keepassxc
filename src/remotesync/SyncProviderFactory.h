#ifndef KEEPASSXC_SYNCPROVIDERFACTORY_H
#define KEEPASSXC_SYNCPROVIDERFACTORY_H

#include <QObject>
#include "ISyncProvider.h"
#include "RemoteSyncSettings.h"

class SyncProviderFactory
{
public:
    static ISyncProvider* create(RemoteSyncSettings::Protocol protocol, QObject* parent = nullptr);
};

#endif // KEEPASSXC_SYNCPROVIDERFACTORY_H
