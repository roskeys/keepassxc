#ifndef KEEPASSXC_DATABASESETTINGSWIDGETREMOTESYNC_H
#define KEEPASSXC_DATABASESETTINGSWIDGETREMOTESYNC_H

#include <QPointer>
#include <QWidget>

#include "RemoteSyncSettings.h"
#include "core/Database.h"

namespace Ui
{
    class DatabaseSettingsWidgetRemoteSync;
}

class DatabaseSettingsWidgetRemoteSync : public QWidget
{
    Q_OBJECT

public:
    explicit DatabaseSettingsWidgetRemoteSync(QWidget* parent = nullptr);
    ~DatabaseSettingsWidgetRemoteSync() override;

    void loadSettings(QSharedPointer<Database> db);
    void saveSettings();

private slots:
    void onBrowseSftpKey();
    void onBrowseGitKey();
    void onTestWebDavConnection();
    void onTestDropboxConnection();
    void onTestSftpConnection();
    void onTestS3Connection();
    void onTestGitConnection();

private:
    QScopedPointer<Ui::DatabaseSettingsWidgetRemoteSync> m_ui;
    QWeakPointer<Database> m_db;
};

#endif // KEEPASSXC_DATABASESETTINGSWIDGETREMOTESYNC_H
