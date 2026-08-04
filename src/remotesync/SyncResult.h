#ifndef KEEPASSXC_SYNCRESULT_H
#define KEEPASSXC_SYNCRESULT_H

#include <QDateTime>
#include <functional>

struct SyncResult
{
    enum class Status
    {
        Success,
        AuthError,
        NetworkError,
        NotFound,
        Conflict,
        ServerError,
        SslError,
        Cancelled
    };

    Status status = Status::Success;
    int httpStatusCode = 0;
    QString errorMessage;
    QString etag;
    QDateTime lastModified;

    [[nodiscard]] bool isSuccess() const
    {
        return status == Status::Success;
    }
};

using SyncCallback = std::function<void(const SyncResult&)>;

#endif // KEEPASSXC_SYNCRESULT_H
