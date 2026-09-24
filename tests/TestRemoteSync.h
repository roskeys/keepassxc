#ifndef KEEPASSXC_TESTREMOTESYNC_H
#define KEEPASSXC_TESTREMOTESYNC_H

#include <QObject>

class TestRemoteSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testOneDriveSettingsUrl();
    void testOneDriveSettingsSaveAndLoad();
    void testOneDriveMergePreservation();
    void testOneDriveProviderCreation();
};

#endif // KEEPASSXC_TESTREMOTESYNC_H
