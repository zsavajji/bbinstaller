#include "bb.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkCookie>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QTimer>
#include <QtEndian>
#include <QUrlQuery>
#include <memory>

// The system zlib, or the copy bundled in Qt (Windows has no system zlib)
#ifdef BB_QT_ZLIB
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

namespace bb {

static const QByteArray UA = "QNXWebClient/1.0";

static QString tag(const QByteArray &xml, const QString &name)
{
    const QRegularExpression re(QStringLiteral("<%1>([^<]*)</%1>").arg(QRegularExpression::escape(name)));
    const auto m = re.match(QString::fromUtf8(xml));
    return m.hasMatch() ? m.captured(1) : QString();
}

static bool has(const QByteArray &xml, const QString &name)
{
    return xml.contains(QStringLiteral("<%1>").arg(name).toUtf8());
}

// The phone only speaks TLS 1.0 (AES256-SHA) and has a self-signed cert
static QSslConfiguration legacyTls()
{
    QSslConfiguration c = QSslConfiguration::defaultConfiguration();
    c.setPeerVerifyMode(QSslSocket::VerifyNone);
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    c.setProtocol(QSsl::TlsV1_0OrLater);
    QT_WARNING_POP
    // OpenSSL 3 refuses TLS 1.0 and SHA-1 suites at its default security level (ignored by other backends)
    c.setBackendConfigurationOption("MinProtocol", QByteArray("TLSv1"));
    c.setBackendConfigurationOption("CipherString", QByteArray("DEFAULT:@SECLEVEL=0"));
    return c;
}

// InstallNet::HashPass: SHA-512 chain over (count, salt, data), twice; the second pass is seeded with the challenge
QByteArray hashPass(const QString &password, const QByteArray &challenge, const QByteArray &salt, int iterations)
{
    QByteArray data = password.toLatin1();
    for (const QByteArray &prefix : {challenge, QByteArray()}) {
        for (int i = 0; i < iterations; i++) {
            QByteArray count(4, 0);
            qToLittleEndian<qint32>(i, count.data());
            data = QCryptographicHash::hash(count + salt + data, QCryptographicHash::Sha512);
        }
        data = prefix + data;
    }
    return data;
}

static quint16 u16(const QByteArray &b, qint64 pos)
{
    if (pos < 0 || pos + 2 > b.size()) throw Error{QStringLiteral("truncated zip")};
    return qFromLittleEndian<quint16>(b.constData() + pos);
}

static quint32 u32(const QByteArray &b, qint64 pos)
{
    if (pos < 0 || pos + 4 > b.size()) throw Error{QStringLiteral("truncated zip")};
    return qFromLittleEndian<quint32>(b.constData() + pos);
}

static QByteArray inflateRaw(const QByteArray &data, quint32 size)
{
    if (size > 16 * 1024 * 1024) throw Error{QStringLiteral("MANIFEST.MF too large")};
    QByteArray out(size, 0);
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) throw Error{QStringLiteral("zlib init failed")};
    s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    s.avail_in = uInt(data.size());
    s.next_out = reinterpret_cast<Bytef *>(out.data());
    s.avail_out = uInt(out.size());
    const int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END) throw Error{QStringLiteral("corrupt MANIFEST.MF")};
    return out;
}

// Just enough zip to pull META-INF/MANIFEST.MF out of a .bar without reading the whole file
static QString readManifest(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw Error{f.errorString()};
    const auto read = [&](qint64 pos, qint64 len) {
        if (!f.seek(pos)) throw Error{QStringLiteral("truncated zip")};
        QByteArray b = f.read(len);
        if (b.size() != len) throw Error{QStringLiteral("truncated zip")};
        return b;
    };
    const qint64 size = f.size();
    const QByteArray tail = read(qMax<qint64>(0, size - 65557), qMin<qint64>(size, 65557));
    const qint64 eocd = tail.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (eocd < 0) throw Error{QStringLiteral("not a zip file")};
    const QByteArray cd = read(u32(tail, eocd + 16), u32(tail, eocd + 12));
    for (qint64 p = 0; p + 46 <= cd.size(); p += 46 + u16(cd, p + 28) + u16(cd, p + 30) + u16(cd, p + 32)) {
        if (cd.mid(p + 46, u16(cd, p + 28)) != "META-INF/MANIFEST.MF") continue;
        const quint32 local = u32(cd, p + 42);
        const QByteArray lh = read(local, 30);
        const QByteArray data = read(local + 30 + u16(lh, 26) + u16(lh, 28), u32(cd, p + 20));
        return QString::fromUtf8(u16(cd, p + 10) == 8 ? inflateRaw(data, u32(cd, p + 24)) : data);
    }
    throw Error{QStringLiteral("no META-INF/MANIFEST.MF")};
}

// Refuses OS/radio bars: Sachesi sends those with mode=os, this app only does apps
BarInfo barInfo(const QString &file)
{
    BarInfo info;
    info.path = file;
    info.name = QFileInfo(file).fileName();
    try {
        info.size = QFileInfo(file).size();
        QMap<QString, QString> fields;
        for (const QString &l : readManifest(file).split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
            const qsizetype colon = l.indexOf(QLatin1Char(':'));
            if (colon >= 0) fields.insert(l.left(colon).trimmed(), l.mid(colon + 1).trimmed());
        }
        if (!fields.value(QStringLiteral("Package-Name")).isEmpty()) info.name = fields.value(QStringLiteral("Package-Name"));
        info.version = fields.value(QStringLiteral("Package-Version"));
        if (fields.value(QStringLiteral("Package-Type")) == QLatin1String("system")
            || fields.value(QStringLiteral("Patch-Package-Type")) == QLatin1String("system"))
            info.error = QStringLiteral("OS/radio bar: use Sachesi");
    } catch (const Error &e) {
        info.error = QStringLiteral("not an installable .bar (%1)").arg(e.message);
    }
    return info;
}

struct Request {
    const QByteArray *body = nullptr;
    QString file;
    QByteArray type = "x-www-form-urlencoded";
    QMap<QByteArray, QByteArray> *cookies = nullptr;
    int timeout = 600000;
    std::function<void(qint64)> onSent;
};

// Blocks in a local event loop so the protocol reads top to bottom; the UI keeps running meanwhile
static QByteArray request(QNetworkAccessManager &nam, const QString &url, const Request &opts = {})
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, UA);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/" + opts.type));
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(opts.timeout);
    if (opts.cookies && !opts.cookies->isEmpty()) {
        QList<QByteArray> pairs;
        for (auto it = opts.cookies->cbegin(); it != opts.cookies->cend(); ++it) pairs << it.key() + '=' + it.value();
        req.setRawHeader("Cookie", pairs.join("; "));
    }
    if (url.startsWith(QLatin1String("https:"))) req.setSslConfiguration(legacyTls());

    QFile file(opts.file);
    QNetworkReply *raw;
    if (!opts.file.isEmpty()) {
        if (!file.open(QIODevice::ReadOnly)) throw Error{file.errorString()};
        raw = nam.post(req, &file);
    } else if (opts.body) {
        raw = nam.post(req, *opts.body);
    } else {
        raw = nam.get(req);
    }
    std::unique_ptr<QNetworkReply, void (*)(QNetworkReply *)> reply(raw, [](QNetworkReply *r) { r->deleteLater(); });
    if (opts.onSent) QObject::connect(raw, &QNetworkReply::uploadProgress, raw, [&](qint64 sent, qint64) { opts.onSent(sent); });

    QEventLoop loop;
    QObject::connect(raw, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!raw->isFinished()) loop.exec();
    if (!raw->isFinished()) {
        raw->abort();
        throw Error{QStringLiteral("cancelled")};
    }

    if (opts.cookies) {
        for (const QNetworkCookie &c : raw->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>())
            opts.cookies->insert(c.name(), c.value());
    }
    // Like Node, any HTTP answer counts as an answer; only transport failures throw
    if (raw->error() != QNetworkReply::NoError && !raw->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
        if (raw->error() == QNetworkReply::OperationCanceledError || raw->error() == QNetworkReply::TimeoutError)
            throw Error{QStringLiteral("phone did not answer")};
        throw Error{raw->errorString()};
    }
    return raw->readAll();
}

// Sachesi's guess: the phone sits one address below ours on the USB link
QStringList candidateIps()
{
    QStringList ips;
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || !a.toString().startsWith(QLatin1String("169.254."))) continue;
        QStringList p = a.toString().split(QLatin1Char('.'));
        const int last = p[3].toInt() - 1;
        p[3] = QString::number(last ? last : 128);
        ips << p.join(QLatin1Char('.'));
    }
    return ips;
}

// Call once per connection. HTTP discovery has the details, but the phone stops serving
// port 80 after a while (seen after ~60 polls), so fall back to "login.cgi answers over HTTPS".
std::optional<DeviceInfo> identify(QNetworkAccessManager &nam, const QString &ip)
{
    try {
        Request r;
        r.timeout = 2000;
        const QByteArray xml = request(nam, QStringLiteral("http://%1/cgi-bin/discovery.cgi").arg(ip), r);
        if (has(xml, QStringLiteral("DeviceCharacteristics")))
            return DeviceInfo{
                ip,
                tag(xml, QStringLiteral("ModelName")),
                tag(xml, QStringLiteral("PlatformVersion")),
                tag(xml, QStringLiteral("MaxSupportedProtocolVersion")).toDouble() >= 3 ? 3 : 1,
                tag(xml, QStringLiteral("OobeCompleted")) != QLatin1String("0"),
            };
    } catch (const Error &) {
    }
    try {
        Request r;
        r.timeout = 3000;
        const QByteArray xml = request(nam, QStringLiteral("https://%1/cgi-bin/login.cgi?request_version=3").arg(ip), r);
        if (has(xml, QStringLiteral("RimTabletResponse"))) return DeviceInfo{ip, QStringLiteral("BlackBerry 10"), QString(), 3, true};
    } catch (const Error &) {
    }
    return std::nullopt;
}

QByteArray Device::call(const QString &page, const QByteArray *body, const QString &file, const std::function<void(qint64)> &onSent)
{
    Request r;
    r.body = body;
    r.file = file;
    if (!file.isEmpty()) r.type = "octet-stream";
    r.cookies = &m_cookies;
    r.onSent = onSent;
    const QByteArray xml = request(m_nam, QStringLiteral("https://%1/cgi-bin/%2").arg(m_info.ip, page), r);
    if (tag(xml, QStringLiteral("Status")) == QLatin1String("Error") || has(xml, QStringLiteral("ErrorDescription"))) {
        const QString desc = tag(xml, QStringLiteral("ErrorDescription"));
        throw Error{desc.isEmpty() ? QString::fromUtf8(xml) : desc};
    }
    return xml;
}

QByteArray Device::post(const QString &page, const QList<QPair<QString, QString>> &form)
{
    QUrlQuery q;
    q.setQueryItems(form);
    const QByteArray body = q.toString(QUrl::FullyEncoded).toUtf8();
    return call(page, &body);
}

void Device::login(const QString &password)
{
    const QString v = QString::number(m_info.protocol);
    QByteArray xml = call(QStringLiteral("login.cgi?request_version=") + v);
    if (has(xml, QStringLiteral("AuthChallenge"))) {
        const QByteArray digest = hashPass(password, tag(xml, QStringLiteral("Challenge")).toLatin1(),
                                           QByteArray::fromHex(tag(xml, QStringLiteral("Salt")).toLatin1()),
                                           tag(xml, QStringLiteral("ICount")).toInt());
        xml = call(QStringLiteral("login.cgi?challenge_data=%1&request_version=%2").arg(QString::fromLatin1(digest.toHex().toUpper()), v));
    }
    if (tag(xml, QStringLiteral("Status")) != QLatin1String("Success"))
        throw Error{QStringLiteral("Wrong password. Not retrying: too many wrong attempts wipe the phone. (%1)")
                        .arg(QString::fromUtf8(xml).replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "))),
                    true};
}

void Device::install(const QStringList &files, const std::function<void(const Progress &)> &progress)
{
    qint64 total = 0;
    for (const QString &f : files) total += QFileInfo(f).size();
    QByteArray xml = post(QStringLiteral("update.cgi"), {{QStringLiteral("mode"), QStringLiteral("bar")}, {QStringLiteral("size"), QString::number(total)}});
    if (!has(xml, QStringLiteral("UpdateStart"))) throw Error{QStringLiteral("Phone refused the update: ") + QString::fromUtf8(xml)};
    for (int index = 0; index < files.size(); index++) {
        const qint64 size = QFileInfo(files[index]).size();
        xml = call(QStringLiteral("update.cgi?type=bar"), nullptr, files[index], [&](qint64 sent) {
            progress({index, Progress::Upload, size ? qRound(100.0 * sent / size) : 100});
        });
        // Phone answers UpdateSend, then UpdateProgress (InProgress...) until Success
        while (!(has(xml, QStringLiteral("UpdateProgress")) && tag(xml, QStringLiteral("Status")) == QLatin1String("Success"))) {
            progress({index, Progress::Install, tag(xml, QStringLiteral("Progress")).toInt()});
            QEventLoop wait;
            QTimer::singleShot(500, &wait, &QEventLoop::quit);
            wait.exec();
            xml = post(QStringLiteral("update.cgi"));
        }
        progress({index, Progress::Done, 100});
    }
    post(QStringLiteral("update.cgi"), {{QStringLiteral("status"), QStringLiteral("success")}});
}

} // namespace bb
