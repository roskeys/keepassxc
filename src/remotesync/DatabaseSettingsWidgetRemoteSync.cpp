#include "DatabaseSettingsWidgetRemoteSync.h"
#include "ui_DatabaseSettingsWidgetRemoteSync.h"

#include <QDir>
#include <QFileDialog>
#include "RemoteSyncManager.h"
#include "SyncProviderFactory.h"

DatabaseSettingsWidgetRemoteSync::DatabaseSettingsWidgetRemoteSync(QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::DatabaseSettingsWidgetRemoteSync)
{
    m_ui->setupUi(this);

    // Toggle groupboxes based on enable checkboxes
    connect(m_ui->checkWebDavEnabled, &QCheckBox::toggled, m_ui->groupWebDavSettings, &QWidget::setEnabled);
    connect(m_ui->checkDropboxEnabled, &QCheckBox::toggled, m_ui->groupDropboxSettings, &QWidget::setEnabled);
    connect(m_ui->checkSftpEnabled, &QCheckBox::toggled, m_ui->groupSftpSettings, &QWidget::setEnabled);
    connect(m_ui->checkS3Enabled, &QCheckBox::toggled, m_ui->groupS3Settings, &QWidget::setEnabled);
    connect(m_ui->checkGitEnabled, &QCheckBox::toggled, m_ui->groupGitSettings, &QWidget::setEnabled);

    m_ui->groupWebDavSettings->setEnabled(false);
    m_ui->groupDropboxSettings->setEnabled(false);
    m_ui->groupSftpSettings->setEnabled(false);
    m_ui->groupS3Settings->setEnabled(false);
    m_ui->groupGitSettings->setEnabled(false);

    // Browse key file for SFTP & Git
    connect(m_ui->buttonBrowseSftpKey, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onBrowseSftpKey);
    connect(m_ui->buttonBrowseGitKey, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onBrowseGitKey);

    // Test buttons
    connect(m_ui->buttonTestWebDav, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestWebDavConnection);
    connect(m_ui->buttonTestDropbox, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestDropboxConnection);
    connect(m_ui->buttonTestSftp, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestSftpConnection);
    connect(m_ui->buttonTestS3, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestS3Connection);
    connect(m_ui->buttonTestGit, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestGitConnection);
}

DatabaseSettingsWidgetRemoteSync::~DatabaseSettingsWidgetRemoteSync() = default;

void DatabaseSettingsWidgetRemoteSync::loadSettings(QSharedPointer<Database> db)
{
    m_db = db;
    if (!db) {
        return;
    }

    RemoteSyncSettings s = RemoteSyncSettings::fromDatabase(db.data());

    // 1. WebDAV
    m_ui->checkWebDavEnabled->setChecked(s.webdav.enabled);
    m_ui->groupWebDavSettings->setEnabled(s.webdav.enabled);
    m_ui->editWebDavUrl->setText(s.webdav.url);
    m_ui->editWebDavRemotePath->setText(s.webdav.remotePath);
    m_ui->editWebDavUsername->setText(s.webdav.username);
    m_ui->editWebDavPassword->setText(s.webdav.password);
    m_ui->checkWebDavVerifySsl->setChecked(s.webdav.verifySsl);
    m_ui->labelTestWebDavResult->clear();

    // 2. Dropbox
    m_ui->checkDropboxEnabled->setChecked(s.dropbox.enabled);
    m_ui->groupDropboxSettings->setEnabled(s.dropbox.enabled);
    m_ui->editDropboxToken->setText(s.dropbox.accessToken);
    m_ui->editDropboxRemotePath->setText(s.dropbox.remotePath);
    m_ui->editDropboxAppKey->setText(s.dropbox.appKey);
    m_ui->editDropboxAppSecret->setText(s.dropbox.appSecret);
    m_ui->labelTestDropboxResult->clear();

    // 3. SFTP
    m_ui->checkSftpEnabled->setChecked(s.sftp.enabled);
    m_ui->groupSftpSettings->setEnabled(s.sftp.enabled);
    m_ui->editSftpHost->setText(s.sftp.host);
    m_ui->spinSftpPort->setValue(s.sftp.port > 0 ? s.sftp.port : 22);
    m_ui->editSftpRemotePath->setText(s.sftp.remotePath);
    m_ui->editSftpUsername->setText(s.sftp.username);
    m_ui->editSftpKeyPath->setText(s.sftp.keyPath);
    m_ui->editSftpPassword->setText(s.sftp.password);
    m_ui->labelTestSftpResult->clear();

    // 3. S3
    m_ui->checkS3Enabled->setChecked(s.s3.enabled);
    m_ui->groupS3Settings->setEnabled(s.s3.enabled);
    m_ui->editS3Endpoint->setText(s.s3.endpoint);
    m_ui->editS3Bucket->setText(s.s3.bucket);
    m_ui->editS3Region->setText(s.s3.region.isEmpty() ? QStringLiteral("us-east-1") : s.s3.region);
    m_ui->editS3AccessKey->setText(s.s3.accessKey);
    m_ui->editS3SecretKey->setText(s.s3.secretKey);
    m_ui->editS3RemotePath->setText(s.s3.remotePath);
    m_ui->checkS3VerifySsl->setChecked(s.s3.verifySsl);
    // 4. Git
    m_ui->checkGitEnabled->setChecked(s.git.enabled);
    m_ui->groupGitSettings->setEnabled(s.git.enabled);
    m_ui->editGitRepoUrl->setText(s.git.repoUrl);
    m_ui->editGitBranch->setText(s.git.branch.isEmpty() ? QStringLiteral("main") : s.git.branch);
    m_ui->editGitRemotePath->setText(s.git.remotePath);
    m_ui->editGitUsername->setText(s.git.username);
    m_ui->editGitPassword->setText(s.git.password);
    m_ui->editGitKeyPath->setText(s.git.keyPath);
    m_ui->editGitAuthorName->setText(s.git.authorName);
    m_ui->editGitAuthorEmail->setText(s.git.authorEmail);
    m_ui->labelTestGitResult->clear();

    // Global
    m_ui->spinInterval->setValue(s.intervalSeconds / 60 > 0 ? s.intervalSeconds / 60 : 5);
}

void DatabaseSettingsWidgetRemoteSync::saveSettings()
{
    auto db = m_db.toStrongRef();
    if (!db) {
        return;
    }

    RemoteSyncSettings s;

    // 1. WebDAV
    s.webdav.enabled = m_ui->checkWebDavEnabled->isChecked();
    s.webdav.url = m_ui->editWebDavUrl->text().trimmed();
    s.webdav.remotePath = m_ui->editWebDavRemotePath->text().trimmed();
    s.webdav.username = m_ui->editWebDavUsername->text().trimmed();
    s.webdav.password = m_ui->editWebDavPassword->text();
    s.webdav.verifySsl = m_ui->checkWebDavVerifySsl->isChecked();

    // 2. Dropbox
    s.dropbox.enabled = m_ui->checkDropboxEnabled->isChecked();
    s.dropbox.accessToken = m_ui->editDropboxToken->text().trimmed();
    s.dropbox.remotePath = m_ui->editDropboxRemotePath->text().trimmed();
    s.dropbox.appKey = m_ui->editDropboxAppKey->text().trimmed();
    s.dropbox.appSecret = m_ui->editDropboxAppSecret->text().trimmed();

    // 3. SFTP
    s.sftp.enabled = m_ui->checkSftpEnabled->isChecked();
    s.sftp.host = m_ui->editSftpHost->text().trimmed();
    s.sftp.port = m_ui->spinSftpPort->value();
    s.sftp.remotePath = m_ui->editSftpRemotePath->text().trimmed();
    s.sftp.username = m_ui->editSftpUsername->text().trimmed();
    s.sftp.keyPath = m_ui->editSftpKeyPath->text().trimmed();
    s.sftp.password = m_ui->editSftpPassword->text();

    // 3. S3
    s.s3.enabled = m_ui->checkS3Enabled->isChecked();
    s.s3.endpoint = m_ui->editS3Endpoint->text().trimmed();
    s.s3.bucket = m_ui->editS3Bucket->text().trimmed();
    s.s3.region = m_ui->editS3Region->text().trimmed();
    s.s3.accessKey = m_ui->editS3AccessKey->text().trimmed();
    s.s3.secretKey = m_ui->editS3SecretKey->text();
    s.s3.remotePath = m_ui->editS3RemotePath->text().trimmed();
    s.s3.verifySsl = m_ui->checkS3VerifySsl->isChecked();

    // 4. Git
    s.git.enabled = m_ui->checkGitEnabled->isChecked();
    s.git.repoUrl = m_ui->editGitRepoUrl->text().trimmed();
    s.git.branch = m_ui->editGitBranch->text().trimmed();
    s.git.remotePath = m_ui->editGitRemotePath->text().trimmed();
    s.git.username = m_ui->editGitUsername->text().trimmed();
    s.git.password = m_ui->editGitPassword->text();
    s.git.keyPath = m_ui->editGitKeyPath->text().trimmed();
    s.git.authorName = m_ui->editGitAuthorName->text().trimmed();
    s.git.authorEmail = m_ui->editGitAuthorEmail->text().trimmed();

    // Global
    s.intervalSeconds = m_ui->spinInterval->value() * 60;

    s.saveToDatabase(db.data());
}

void DatabaseSettingsWidgetRemoteSync::onBrowseSftpKey()
{
    QString initialDir = QDir::homePath() + QStringLiteral("/.ssh");
    QString selected = QFileDialog::getOpenFileName(this, tr("Select SSH Private Key"), initialDir, tr("All Files (*);;SSH Keys (*.id_* id_* *.pem)"));
    if (!selected.isEmpty()) {
        m_ui->editSftpKeyPath->setText(selected);
    }
}

void DatabaseSettingsWidgetRemoteSync::onBrowseGitKey()
{
    QString initialDir = QDir::homePath() + QStringLiteral("/.ssh");
    QString selected = QFileDialog::getOpenFileName(this, tr("Select SSH Private Key for Git"), initialDir, tr("All Files (*);;SSH Keys (*.id_* id_* *.pem)"));
    if (!selected.isEmpty()) {
        m_ui->editGitKeyPath->setText(selected);
    }
}

void DatabaseSettingsWidgetRemoteSync::onTestWebDavConnection()
{
    RemoteSyncSettings s;
    s.protocol = RemoteSyncSettings::Protocol::WebDAV;
    s.webdav.enabled = true;
    s.webdav.url = m_ui->editWebDavUrl->text().trimmed();
    s.webdav.remotePath = m_ui->editWebDavRemotePath->text().trimmed();
    s.webdav.username = m_ui->editWebDavUsername->text().trimmed();
    s.webdav.password = m_ui->editWebDavPassword->text();
    s.webdav.verifySsl = m_ui->checkWebDavVerifySsl->isChecked();

    // Compatibility fields
    s.url = s.webdav.url;
    s.remotePath = s.webdav.remotePath;
    s.username = s.webdav.username;
    s.password = s.webdav.password;
    s.verifySsl = s.webdav.verifySsl;

    m_ui->labelTestWebDavResult->setText(tr("Testing WebDAV connection..."));
    m_ui->buttonTestWebDav->setEnabled(false);

    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::WebDAV, this);
    provider->testConnection(s, [this, provider](const SyncResult& result) {
        m_ui->buttonTestWebDav->setEnabled(true);
        if (result.isSuccess()) {
            m_ui->labelTestWebDavResult->setText(tr("<font color='green'>Connection successful!</font>"));
        } else {
            m_ui->labelTestWebDavResult->setText(tr("<font color='red'>Failed: %1</font>").arg(result.errorMessage));
        }
        provider->deleteLater();
    });
}

void DatabaseSettingsWidgetRemoteSync::onTestDropboxConnection()
{
    RemoteSyncSettings s;
    s.protocol = RemoteSyncSettings::Protocol::Dropbox;
    s.dropbox.enabled = true;
    s.dropbox.accessToken = m_ui->editDropboxToken->text().trimmed();
    s.dropbox.remotePath = m_ui->editDropboxRemotePath->text().trimmed();
    s.dropbox.appKey = m_ui->editDropboxAppKey->text().trimmed();
    s.dropbox.appSecret = m_ui->editDropboxAppSecret->text().trimmed();

    m_ui->labelTestDropboxResult->setText(tr("Testing Dropbox connection..."));
    m_ui->buttonTestDropbox->setEnabled(false);

    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::Dropbox, this);
    provider->testConnection(s, [this, provider](const SyncResult& result) {
        m_ui->buttonTestDropbox->setEnabled(true);
        if (result.isSuccess()) {
            m_ui->labelTestDropboxResult->setText(tr("<font color='green'>Connection successful!</font>"));
        } else {
            m_ui->labelTestDropboxResult->setText(tr("<font color='red'>Failed: %1</font>").arg(result.errorMessage));
        }
        provider->deleteLater();
    });
}

void DatabaseSettingsWidgetRemoteSync::onTestSftpConnection()
{
    RemoteSyncSettings s;
    s.protocol = RemoteSyncSettings::Protocol::SFTP;
    s.sftp.enabled = true;
    s.sftp.host = m_ui->editSftpHost->text().trimmed();
    s.sftp.port = m_ui->spinSftpPort->value();
    s.sftp.remotePath = m_ui->editSftpRemotePath->text().trimmed();
    s.sftp.username = m_ui->editSftpUsername->text().trimmed();
    s.sftp.keyPath = m_ui->editSftpKeyPath->text().trimmed();
    s.sftp.password = m_ui->editSftpPassword->text();

    // Compatibility fields
    s.url = s.sftp.host;
    s.remotePath = s.sftp.remotePath;
    s.username = s.sftp.username;
    s.password = s.sftp.password;
    s.sftpKeyPath = s.sftp.keyPath;

    m_ui->labelTestSftpResult->setText(tr("Testing SFTP connection..."));
    m_ui->buttonTestSftp->setEnabled(false);

    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::SFTP, this);
    provider->testConnection(s, [this, provider](const SyncResult& result) {
        m_ui->buttonTestSftp->setEnabled(true);
        if (result.isSuccess()) {
            m_ui->labelTestSftpResult->setText(tr("<font color='green'>Connection successful!</font>"));
        } else {
            m_ui->labelTestSftpResult->setText(tr("<font color='red'>Failed: %1</font>").arg(result.errorMessage));
        }
        provider->deleteLater();
    });
}

void DatabaseSettingsWidgetRemoteSync::onTestS3Connection()
{
    RemoteSyncSettings s;
    s.protocol = RemoteSyncSettings::Protocol::S3;
    s.s3.enabled = true;
    s.s3.endpoint = m_ui->editS3Endpoint->text().trimmed();
    s.s3.bucket = m_ui->editS3Bucket->text().trimmed();
    s.s3.region = m_ui->editS3Region->text().trimmed();
    s.s3.accessKey = m_ui->editS3AccessKey->text().trimmed();
    s.s3.secretKey = m_ui->editS3SecretKey->text();
    s.s3.remotePath = m_ui->editS3RemotePath->text().trimmed();
    s.s3.verifySsl = m_ui->checkS3VerifySsl->isChecked();

    // Compatibility fields
    s.url = s.s3.endpoint;
    s.s3Bucket = s.s3.bucket;
    s.s3Region = s.s3.region;
    s.username = s.s3.accessKey;
    s.password = s.s3.secretKey;
    s.remotePath = s.s3.remotePath;
    s.verifySsl = s.s3.verifySsl;

    m_ui->labelTestS3Result->setText(tr("Testing S3 connection..."));
    m_ui->buttonTestS3->setEnabled(false);

    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::S3, this);
    provider->testConnection(s, [this, provider](const SyncResult& result) {
        m_ui->buttonTestS3->setEnabled(true);
        if (result.isSuccess()) {
            m_ui->labelTestS3Result->setText(tr("<font color='green'>Connection successful!</font>"));
        } else {
            m_ui->labelTestS3Result->setText(tr("<font color='red'>Failed: %1</font>").arg(result.errorMessage));
        }
        provider->deleteLater();
    });
}

void DatabaseSettingsWidgetRemoteSync::onTestGitConnection()
{
    RemoteSyncSettings s;
    s.protocol = RemoteSyncSettings::Protocol::Git;
    s.git.enabled = true;
    s.git.repoUrl = m_ui->editGitRepoUrl->text().trimmed();
    s.git.branch = m_ui->editGitBranch->text().trimmed();
    s.git.remotePath = m_ui->editGitRemotePath->text().trimmed();
    s.git.username = m_ui->editGitUsername->text().trimmed();
    s.git.password = m_ui->editGitPassword->text();
    s.git.keyPath = m_ui->editGitKeyPath->text().trimmed();
    s.git.authorName = m_ui->editGitAuthorName->text().trimmed();
    s.git.authorEmail = m_ui->editGitAuthorEmail->text().trimmed();

    m_ui->labelTestGitResult->setText(tr("Testing Git connection..."));
    m_ui->buttonTestGit->setEnabled(false);

    auto* provider = SyncProviderFactory::create(RemoteSyncSettings::Protocol::Git, this);
    provider->testConnection(s, [this, provider](const SyncResult& result) {
        m_ui->buttonTestGit->setEnabled(true);
        if (result.isSuccess()) {
            m_ui->labelTestGitResult->setText(tr("<font color='green'>Connection successful!</font>"));
        } else {
            m_ui->labelTestGitResult->setText(tr("<font color='red'>Failed: %1</font>").arg(result.errorMessage));
        }
        provider->deleteLater();
    });
}
