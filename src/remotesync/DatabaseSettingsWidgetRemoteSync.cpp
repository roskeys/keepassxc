#include "DatabaseSettingsWidgetRemoteSync.h"
#include "ui_DatabaseSettingsWidgetRemoteSync.h"

#include "RemoteSyncManager.h"
#include "SyncProviderFactory.h"

DatabaseSettingsWidgetRemoteSync::DatabaseSettingsWidgetRemoteSync(QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::DatabaseSettingsWidgetRemoteSync)
{
    m_ui->setupUi(this);

    connect(m_ui->comboProtocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DatabaseSettingsWidgetRemoteSync::onProtocolChanged);
    connect(m_ui->buttonTest, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemoteSync::onTestConnection);
    connect(m_ui->checkEnableSync, &QCheckBox::toggled, m_ui->groupSettings, &QWidget::setEnabled);

    m_ui->groupSettings->setEnabled(false);
}

DatabaseSettingsWidgetRemoteSync::~DatabaseSettingsWidgetRemoteSync() = default;

void DatabaseSettingsWidgetRemoteSync::loadSettings(QSharedPointer<Database> db)
{
    m_db = db;
    if (!db) {
        return;
    }

    RemoteSyncSettings s = RemoteSyncSettings::fromDatabase(db.data());
    m_ui->checkEnableSync->setChecked(s.enabled);
    m_ui->groupSettings->setEnabled(s.enabled);

    switch (s.protocol) {
    case RemoteSyncSettings::Protocol::SFTP:
        m_ui->comboProtocol->setCurrentIndex(1);
        break;
    case RemoteSyncSettings::Protocol::S3:
        m_ui->comboProtocol->setCurrentIndex(2);
        break;
    case RemoteSyncSettings::Protocol::FTPS:
        m_ui->comboProtocol->setCurrentIndex(3);
        break;
    case RemoteSyncSettings::Protocol::WebDAV:
    default:
        m_ui->comboProtocol->setCurrentIndex(0);
        break;
    }

    m_ui->editUrl->setText(s.url);
    m_ui->editRemotePath->setText(s.remotePath);
    m_ui->editUsername->setText(s.username);
    m_ui->editPassword->setText(s.password);
    m_ui->spinInterval->setValue(s.intervalSeconds / 60);
    m_ui->checkVerifySsl->setChecked(s.verifySsl);
    m_ui->labelTestResult->clear();
}

void DatabaseSettingsWidgetRemoteSync::saveSettings()
{
    auto db = m_db.toStrongRef();
    if (!db) {
        return;
    }

    RemoteSyncSettings s;
    s.enabled = m_ui->checkEnableSync->isChecked();

    switch (m_ui->comboProtocol->currentIndex()) {
    case 1:
        s.protocol = RemoteSyncSettings::Protocol::SFTP;
        break;
    case 2:
        s.protocol = RemoteSyncSettings::Protocol::S3;
        break;
    case 3:
        s.protocol = RemoteSyncSettings::Protocol::FTPS;
        break;
    case 0:
    default:
        s.protocol = RemoteSyncSettings::Protocol::WebDAV;
        break;
    }

    s.url = m_ui->editUrl->text().trimmed();
    s.remotePath = m_ui->editRemotePath->text().trimmed();
    s.username = m_ui->editUsername->text().trimmed();
    s.password = m_ui->editPassword->text();
    s.intervalSeconds = m_ui->spinInterval->value() * 60;
    s.verifySsl = m_ui->checkVerifySsl->isChecked();

    s.saveToDatabase(db.data());
}

void DatabaseSettingsWidgetRemoteSync::onProtocolChanged(int index)
{
    if (index == 0) {
        m_ui->editUrl->setPlaceholderText(QStringLiteral("https://storage.roskey.net/dav"));
        m_ui->editRemotePath->setPlaceholderText(QStringLiteral("Credentials/passwords.kdbx"));
    } else if (index == 1) {
        m_ui->editUrl->setPlaceholderText(QStringLiteral("sftp://example.com"));
        m_ui->editRemotePath->setPlaceholderText(QStringLiteral("/home/user/Credentials/passwords.kdbx"));
    } else if (index == 2) {
        m_ui->editUrl->setPlaceholderText(QStringLiteral("https://s3.us-east-1.amazonaws.com"));
        m_ui->editRemotePath->setPlaceholderText(QStringLiteral("mybucket/Credentials/passwords.kdbx"));
    } else if (index == 3) {
        m_ui->editUrl->setPlaceholderText(QStringLiteral("ftps://example.com"));
        m_ui->editRemotePath->setPlaceholderText(QStringLiteral("Credentials/passwords.kdbx"));
    }
}

void DatabaseSettingsWidgetRemoteSync::onTestConnection()
{
    RemoteSyncSettings s;
    s.enabled = true;
    switch (m_ui->comboProtocol->currentIndex()) {
    case 1: s.protocol = RemoteSyncSettings::Protocol::SFTP; break;
    case 2: s.protocol = RemoteSyncSettings::Protocol::S3; break;
    case 3: s.protocol = RemoteSyncSettings::Protocol::FTPS; break;
    case 0: default: s.protocol = RemoteSyncSettings::Protocol::WebDAV; break;
    }

    s.url = m_ui->editUrl->text().trimmed();
    s.remotePath = m_ui->editRemotePath->text().trimmed();
    s.username = m_ui->editUsername->text().trimmed();
    s.password = m_ui->editPassword->text();
    s.verifySsl = m_ui->checkVerifySsl->isChecked();

    m_ui->labelTestResult->setText(tr("Testing connection…"));
    m_ui->buttonTest->setEnabled(false);

    auto* provider = SyncProviderFactory::create(s.protocol, this);
    provider->configure(s);
    provider->testConnection(s, [this, provider](const SyncResult& res) {
        m_ui->buttonTest->setEnabled(true);
        if (res.isSuccess()) {
            m_ui->labelTestResult->setText(tr("Connection successful!"));
        } else {
            m_ui->labelTestResult->setText(tr("Connection failed: %1").arg(res.errorMessage));
        }
        provider->deleteLater();
    });
}
