#include "TestRemoteSync.h"

#include <QTest>
#include "core/CustomData.h"
#include "core/Database.h"
#include "core/Metadata.h"
#include "remotesync/RemoteSyncSettings.h"
#include "remotesync/SyncProviderFactory.h"

void TestRemoteSync::initTestCase()
{
}

void TestRemoteSync::testOneDriveSettingsUrl()
{
    OneDriveSettings s;
    s.remotePath = QStringLiteral("passwords.kdbx");
    QCOMPARE(s.fullRemoteUrl(), QStringLiteral("onedrive:/passwords.kdbx"));

    s.remotePath = QStringLiteral("/Secrets/vault.kdbx");
    QCOMPARE(s.fullRemoteUrl(), QStringLiteral("onedrive:/Secrets/vault.kdbx"));

    s.driveId = QStringLiteral("b!abcd1234");
    QCOMPARE(s.fullRemoteUrl(), QStringLiteral("onedrive://b!abcd1234/Secrets/vault.kdbx"));

    // Empty remotePath fallback
    s.remotePath = QString();
    s.driveId = QString();
    QCOMPARE(s.fullRemoteUrl(QStringLiteral("my_default.kdbx")), QStringLiteral("onedrive:/my_default.kdbx"));
}

void TestRemoteSync::testOneDriveSettingsSaveAndLoad()
{
    Database db;
    RemoteSyncSettings s;
    s.oneDrive.enabled = true;
    s.oneDrive.clientId = QStringLiteral("client-id-12345");
    s.oneDrive.clientSecret = QStringLiteral("client-secret-abcde");
    s.oneDrive.refreshToken = QStringLiteral("refresh-token-xyz");
    s.oneDrive.accessToken = QStringLiteral("access-token-999");
    s.oneDrive.remotePath = QStringLiteral("KeePass/passwords.kdbx");
    s.oneDrive.driveId = QStringLiteral("drive-id-777");
    s.intervalSeconds = 600;

    QVERIFY(s.isAnyEnabled());
    QCOMPARE(RemoteSyncSettings::protocolToString(RemoteSyncSettings::Protocol::OneDrive), QStringLiteral("onedrive"));
    QCOMPARE(RemoteSyncSettings::protocolFromString(QStringLiteral("onedrive")), RemoteSyncSettings::Protocol::OneDrive);

    s.saveToDatabase(&db);

    RemoteSyncSettings loaded = RemoteSyncSettings::fromDatabase(&db);
    QVERIFY(loaded.oneDrive.enabled);
    QCOMPARE(loaded.oneDrive.clientId, QStringLiteral("client-id-12345"));
    QCOMPARE(loaded.oneDrive.clientSecret, QStringLiteral("client-secret-abcde"));
    QCOMPARE(loaded.oneDrive.refreshToken, QStringLiteral("refresh-token-xyz"));
    QCOMPARE(loaded.oneDrive.accessToken, QStringLiteral("access-token-999"));
    QCOMPARE(loaded.oneDrive.remotePath, QStringLiteral("KeePass/passwords.kdbx"));
    QCOMPARE(loaded.oneDrive.driveId, QStringLiteral("drive-id-777"));
    QCOMPARE(loaded.intervalSeconds, 600);
}

void TestRemoteSync::testOneDriveMergePreservation()
{
    CustomData cd;
    QVERIFY(cd.isProtected(QStringLiteral("KPXC_REMOTESYNC_ONEDRIVE_ENABLED")));
    QVERIFY(cd.isProtected(QStringLiteral("KPXC_REMOTESYNC_ONEDRIVE_TOKEN")));
    QVERIFY(cd.isProtected(QStringLiteral("KPXC_REMOTESYNC_ONEDRIVE_REFRESH_TOKEN")));
    QVERIFY(cd.isProtected(QStringLiteral("KPXC_REMOTESYNC_ONEDRIVE_CLIENT_ID")));
}

void TestRemoteSync::testOneDriveProviderCreation()
{
    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::OneDrive);
    QVERIFY(provider != nullptr);
    delete provider;
}

QTEST_GUILESS_MAIN(TestRemoteSync)
