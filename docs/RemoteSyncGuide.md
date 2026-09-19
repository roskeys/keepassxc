# KeePassXC Remote Synchronization Guide

KeePassXC provides built-in Remote Database Synchronization, allowing you to automatically synchronize your `.kdbx` password database across devices using various cloud and remote protocols.

---

## Table of Contents
1. [Overview & Sync Behavior](#overview--sync-behavior)
2. [Google Drive Synchronization](#google-drive-synchronization)
3. [Dropbox Synchronization](#dropbox-synchronization)
4. [WebDAV (Nextcloud / ownCloud / Synology)](#webdav-nextcloud--owncloud--synology)
5. [SFTP (SSH File Transfer Protocol)](#sftp-ssh-file-transfer-protocol)
6. [Amazon S3 / S3-Compatible Storage (MinIO, Wasabi, Backblaze B2)](#amazon-s3--s3-compatible-storage)
7. [Git Repository Synchronization (GitHub / GitLab / Self-Hosted)](#git-repository-synchronization)
8. [General Settings & Troubleshooting](#general-settings--troubleshooting)

---

## Overview & Sync Behavior

- **Automatic Two-Way Merge**: Whenever sync runs, KeePassXC downloads the latest remote version (if changed), performs an entry-by-entry timestamp merge with your local database to ensure no passwords or edits are lost, and pushes the merged result back to the remote destination.
- **Trigger Conditions**:
  - Automatically upon opening / unlocking the database.
  - Automatically whenever the database is saved locally.
  - Periodically on a configurable timer (default: every 5 minutes).
  - Manually by clicking the **Synchronize Database** toolbar button or menu option (**Database -> Synchronize Database**).
- **Multi-Cloud Support**: You can configure multiple remote providers simultaneously; KeePassXC will synchronize across all enabled targets.

To configure remote synchronization, open your database and go to:
> **Database** -> **Database Settings** -> **Remote Sync**

---

## Google Drive Synchronization

Google Drive uses OAuth 2.0 with permanent token auto-refresh.

### Step 1: Create OAuth 2.0 Credentials in Google Cloud Console
1. Visit the [Google Cloud Console](https://console.cloud.google.com/).
2. Create a new project (e.g. `KeePassXC-Sync`).
3. Enable the **Google Drive API**:
   - Go to **APIs & Services** -> **Library**, search for **Google Drive API**, and click **Enable**.
4. Configure the **OAuth Consent Screen**:
   - Select **External** (or Internal for Workspace).
   - Enter an app name (e.g., `KeePassXC Sync`) and your email address.
   - Under **Test users**, add your Google email address.
5. Create Credentials:
   - Go to **APIs & Services** -> **Credentials** -> **Create Credentials** -> **OAuth client ID**.
   - Application type: **Web application** (or Desktop app).
   - Under **Authorized redirect URIs**, add:
     ```text
     https://developers.google.com/oauthplayground
     ```
   - Click **Create** and copy your **Client ID** and **Client Secret**.

### Step 2: Generate Permanent Refresh Token
1. Open the [Google OAuth 2.0 Playground](https://developers.google.com/oauthplayground/).
2. Click the **Gear icon (⚙)** in the top right:
   - Check **"Use your own OAuth credentials"**.
   - Paste your **OAuth Client ID** and **OAuth Client Secret**.
3. Under **Step 1: Select & authorize APIs**:
   - Scroll down to **Drive API v3**.
   - Check `https://www.googleapis.com/auth/drive.file` (or `https://www.googleapis.com/auth/drive`).
   - Click **Authorize APIs** and sign in with your Google account.
4. Under **Step 2**:
   - Click **Exchange authorization code for tokens**.
   - Copy the value in the **Refresh token** field (starts with `1//0...`).

### Step 3: Configure KeePassXC
In the **Google Drive** tab:
- **Enable Google Drive Synchronization**: Checked
- **Client ID**: Your Client ID
- **Client Secret**: Your Client Secret
- **Refresh Token (Permanent)**: The `1//0...` token
- **Access Token (optional)**: Leave blank (KeePassXC automatically generates and refreshes it)
- **Remote File Name**: e.g., `passwords.kdbx`
- **Folder ID (optional)**: Leave blank to store in root ("My Drive"), or paste the alphanumeric folder ID from your browser's address bar when browsing that folder on drive.google.com.
- Click **Test Google Drive Connection** to verify.

---

## Dropbox Synchronization

Dropbox supports scoped Personal Access Tokens or OAuth 2.0 Bearer tokens.

### Step 1: Create a Dropbox App
1. Go to the [Dropbox App Console](https://www.dropbox.com/developers/apps).
2. Click **Create app**:
   - Choose **Scoped access**.
   - Choose **App folder** (recommended) or **Full Dropbox**.
   - Name your app (e.g., `KeePassXC-Sync-YourName`).
3. In the app settings under the **Permissions** tab, enable:
   - `files.metadata.read`
   - `files.metadata.write`
   - `files.content.read`
   - `files.content.write`
   - Click **Submit** at the bottom of the page.
4. Under the **Settings** tab:
   - Under **OAuth 2 -> Generated access token**, click **Generate**.
   - Copy the generated access token (starts with `sl.u...` or similar).

### Step 2: Configure KeePassXC
In the **Dropbox** tab:
- **Enable Dropbox Synchronization**: Checked
- **OAuth Access Token**: Paste the generated access token
- **Remote File Path**: e.g., `/passwords.kdbx` or `/Passwords/database.kdbx`
- **App Key / Secret (optional)**: Leave blank unless using custom OAuth flow
- Click **Test Dropbox Connection** to verify.

---

## WebDAV (Nextcloud / ownCloud / Synology)

WebDAV is standard for personal cloud platforms like Nextcloud, ownCloud, mailbox.org, and Synology NAS.

### Step 1: Obtain your WebDAV URL
- **Nextcloud / ownCloud**: In the web interface, click **Files** -> **Files settings** (bottom left corner) to find your WebDAV URL:
  ```text
  https://cloud.example.com/remote.php/dav/files/USERNAME/
  ```
- **Synology DSM (WebDAV Server)**:
  ```text
  https://nas.example.com:5006/home/
  ```

### Step 2: Configure KeePassXC
In the **WebDAV** tab:
- **Enable WebDAV Synchronization**: Checked
- **Server URL**: Your WebDAV root endpoint (e.g., `https://cloud.example.com/remote.php/dav/files/USERNAME/`)
- **Remote File Path**: Path relative to WebDAV root (e.g., `Passwords/database.kdbx`)
- **Username**: Your cloud username
- **Password / Token**: Your password, or preferably an **App Password / App Token** generated from your cloud user security settings.
- **Verify SSL Certificates**: Checked (recommended)
- Click **Test WebDAV Connection** to verify.

---

## SFTP (SSH File Transfer Protocol)

SFTP allows syncing directly to any Linux/Unix server, VPS, or NAS running an SSH daemon.

### Step 1: Requirements
- An SSH account on the target server.
- Either SSH password authentication or an SSH private key (`id_ed25519` / `id_rsa`).

### Step 2: Configure KeePassXC
In the **SFTP (SSH)** tab:
- **Enable SFTP Synchronization**: Checked
- **Host / Server**: Server hostname or IP address (e.g., `vps.example.com`)
- **Port**: Default is `22`
- **Remote File Path**: Absolute or relative remote path (e.g., `/home/username/passwords.kdbx`)
- **Username**: SSH username
- **Authentication Method**:
  - **SSH Key**: Click **Browse...** to select your private key file. If key has a passphrase, enter it in the **Password / Key Passphrase** field.
  - **Password**: Enter your SSH account password.
- Click **Test SFTP Connection** to verify.

---

## Amazon S3 / S3-Compatible Storage

Works with AWS S3, Cloudflare R2, MinIO, Wasabi, Backblaze B2, and DigitalOcean Spaces.

### Step 1: S3 Bucket & Credentials
- Create a bucket dedicated to your passwords or backup.
- Create an IAM User / API Key with `s3:GetObject`, `s3:PutObject`, `s3:DeleteObject`, and `s3:HeadObject` permissions.

### Step 2: Configure KeePassXC
In the **Amazon S3** tab:
- **Enable Amazon S3 Synchronization**: Checked
- **Endpoint URL**:
  - AWS S3: `https://s3.amazonaws.com` (or leave default)
  - Cloudflare R2: `https://<ACCOUNT_ID>.r2.cloudflarestorage.com`
  - MinIO / Local: `https://minio.example.com:9000`
  - Backblaze B2: `https://s3.<region>.backblazeb2.com`
- **Bucket Name**: Name of your bucket (e.g. `my-kpxc-passwords`)
- **Region**: e.g., `us-east-1` (or your provider's region)
- **Access Key ID**: Your access key
- **Secret Access Key**: Your secret key
- **Remote File Path / Object Key**: e.g., `passwords.kdbx`
- **Verify SSL Certificates**: Checked
- Click **Test S3 Connection** to verify.

---

## Git Repository Synchronization

Syncs database revisions using Git version control via HTTPS or SSH (GitHub, GitLab, Gitea, or self-hosted bare repository).

### Step 1: Prepare Repository
- Create a repository (e.g., `passwords`). You can initialize it empty or with existing files.
- For private repositories:
  - **HTTPS**: Use a Personal Access Token with `repo` / read-write permissions as the password.
  - **SSH**: Add your SSH public key to your Git account.

### Step 2: Configure KeePassXC
In the **Git Repository** tab:
- **Enable Git Synchronization**: Checked
- **Repository URL**:
  - SSH: `git@github.com:username/passwords.git`
  - HTTPS: `https://github.com/username/passwords.git`
- **Branch**: Target branch (default: `main`)
- **Remote File Path**: File name / relative path within repo (default: `passwords.kdbx`)
- **Authentication**:
  - **For SSH**: Enter your private key path (e.g. `/home/user/.ssh/id_ed25519`) in **SSH Key Path**.
  - **For HTTPS**: Enter your username in **Username** and personal access token in **Password / Token**.
- **Author Information (optional)**: Customize Git commit author name and email.
- Click **Test Git Connection** to verify.

---

## General Settings & Troubleshooting

### Periodic Sync Interval
Under the **General Sync Settings** group box at the bottom of the dialog:
- Set **Periodic Sync Interval (minutes)** (default: 5 minutes). KeePassXC will check for remote changes and perform two-way synchronization in the background while your database is unlocked.

### Status Indicators
- **Toolbar & Status Bar**: The sync status indicator in the bottom right corner shows:
  - **Idle**: Ready and up to date.
  - **Pulling**: Checking remote status and downloading changes.
  - **Pushing**: Uploading merged database to remote destination.
  - **Error tooltip**: Hover over the sync icon to see error details if a sync operation encounters network, permission, or authentication failures.
