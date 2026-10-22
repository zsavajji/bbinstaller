// BB10 desktop protocol, ported from Sachesi's InstallNet (src/installer.cpp).
// The OS brings up the phone's USB network link (macOS: CDC-NCM, Linux: rndis_host/cdc_ncm);
// this only speaks the phone's cgi-bin protocol: discovery -> login -> update.cgi.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace bb {

struct Error {
    QString message;
    bool wrongPassword = false;
};

struct BarInfo {
    QString path, name, version, error;
    qint64 size = 0;
};

struct DeviceInfo {
    QString ip, model, os;
    int protocol = 1;
    bool setupDone = true;
};

struct Progress {
    enum Phase { Upload, Install, Done };
    int index;
    Phase phase;
    int percent;
};

QByteArray hashPass(const QString &password, const QByteArray &challenge, const QByteArray &salt, int iterations);
BarInfo barInfo(const QString &file);
QStringList candidateIps();
std::optional<DeviceInfo> identify(QNetworkAccessManager &nam, const QString &ip);

class Device {
public:
    explicit Device(DeviceInfo info) : m_info(std::move(info)) {}
    const DeviceInfo &info() const { return m_info; }

    void login(const QString &password);
    void install(const QStringList &files, const std::function<void(const Progress &)> &progress);

private:
    QByteArray call(const QString &page, const QByteArray *body = nullptr, const QString &file = {},
                    const std::function<void(qint64)> &onSent = {});
    QByteArray post(const QString &page, const QList<QPair<QString, QString>> &form = {});

    DeviceInfo m_info;
    QMap<QByteArray, QByteArray> m_cookies;
    QNetworkAccessManager m_nam;
};

} // namespace bb
