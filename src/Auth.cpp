#include "Auth.h"

#include <QCryptographicHash>
#include <QList>
#include <QPasswordDigestor>
#include <QRandomGenerator>

namespace {

// Cost parameter. 120k iterations of SHA-256 is roughly the OWASP 2024
// recommendation for PBKDF2-HMAC-SHA256 and stays well under 100 ms on
// a Pi 4 — fine for human logins. Iterations are stored in the hash so
// raising this here doesn't invalidate older config files.
constexpr int kIterations = 120000;
constexpr int kSaltLen = 16;
constexpr int kKeyLen = 32;
constexpr auto kAlgoTag = "pbkdf2-sha256";

QByteArray randomSalt() {
    QByteArray salt(kSaltLen, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(salt.data()),
                                          kSaltLen / sizeof(quint32));
    return salt;
}

// Constant-time byte comparison. QByteArray::operator== short-circuits
// on length mismatch and on first differing byte, which leaks timing.
bool constantTimeEqual(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) { return false; }
    quint8 diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff |= static_cast<quint8>(a[i]) ^ static_cast<quint8>(b[i]);
    }
    return diff == 0;
}

QByteArray pbkdf2(const QByteArray& password,
                  const QByteArray& salt,
                  int iterations,
                  int keyLen) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password,
                                              salt,
                                              iterations,
                                              keyLen);
}

}  // namespace

namespace Auth {

QByteArray hashPassword(const QString& password) {
    const QByteArray salt = randomSalt();
    const QByteArray digest = pbkdf2(password.toUtf8(), salt,
                                     kIterations, kKeyLen);
    QByteArray out(kAlgoTag);
    out += '$';
    out += QByteArray::number(kIterations);
    out += '$';
    out += salt.toBase64(QByteArray::Base64Encoding | QByteArray::OmitTrailingEquals);
    out += '$';
    out += digest.toBase64(QByteArray::Base64Encoding | QByteArray::OmitTrailingEquals);
    return out;
}

bool verifyPassword(const QString& password, const QByteArray& storedHash) {
    const QList<QByteArray> parts = storedHash.split('$');
    if (parts.size() != 4) { return false; }
    if (parts[0] != kAlgoTag) { return false; }

    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations <= 0 || iterations > 10'000'000) { return false; }

    const QByteArray salt = QByteArray::fromBase64(parts[2]);
    const QByteArray expected = QByteArray::fromBase64(parts[3]);
    if (salt.isEmpty() || expected.isEmpty()) { return false; }

    const QByteArray actual = pbkdf2(password.toUtf8(), salt,
                                     iterations, expected.size());
    return constantTimeEqual(actual, expected);
}

bool parseBasicHeader(const QByteArray& headerValue,
                      QString& user,
                      QString& password) {
    // Header value is "Basic <base64>" — the prefix is case-insensitive.
    QByteArray trimmed = headerValue.trimmed();
    if (trimmed.size() < 6) { return false; }
    if (qstrnicmp(trimmed.constData(), "Basic ", 6) != 0) { return false; }

    const QByteArray b64 = trimmed.mid(6).trimmed();
    const QByteArray decoded = QByteArray::fromBase64(b64);
    const int colon = decoded.indexOf(':');
    if (colon < 0) { return false; }

    user = QString::fromUtf8(decoded.left(colon));
    password = QString::fromUtf8(decoded.mid(colon + 1));
    return true;
}

bool isLanAddress(const QHostAddress& addr) {
    // Convert IPv4-in-IPv6 (::ffff:192.168.x.y) down to the IPv4 form so
    // the IPv4 subnet checks below work uniformly. Also handles IPv4
    // dual-stack listeners reporting the IPv6 representation.
    bool v4Ok = false;
    const quint32 v4 = addr.toIPv4Address(&v4Ok);

    if (addr.isLoopback()) { return true; }

    // IPv4 link-local 169.254.0.0/16
    if (v4Ok) {
        const QHostAddress a(v4);
        if (a.isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16)) { return true; }
        // RFC1918
        if (a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")),    8))  { return true; }
        if (a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12))  { return true; }
        if (a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")),16))  { return true; }
        return false;
    }

    // IPv6 link-local fe80::/10
    if (addr.isInSubnet(QHostAddress(QStringLiteral("fe80::")), 10)) { return true; }
    // IPv6 ULA fc00::/7
    if (addr.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7))  { return true; }
    return false;
}

bool checkBasic(const QByteArray& headerValue,
                const QString& expectedUser,
                const QByteArray& expectedHash) {
    if (expectedUser.isEmpty() || expectedHash.isEmpty()) { return false; }

    QString user;
    QString password;
    if (!parseBasicHeader(headerValue, user, password)) { return false; }

    // Compare user in constant time too — avoids leaking whether the
    // username is correct via response timing.
    const QByteArray a = user.toUtf8();
    const QByteArray b = expectedUser.toUtf8();
    if (!constantTimeEqual(a, b)) {
        // Still run PBKDF2 so a wrong-username attempt isn't faster than a
        // wrong-password attempt — same timing, same answer (false).
        (void)verifyPassword(password, expectedHash);
        return false;
    }
    return verifyPassword(password, expectedHash);
}

}  // namespace Auth
