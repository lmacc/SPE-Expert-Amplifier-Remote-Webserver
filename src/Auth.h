#pragma once

#include <QByteArray>
#include <QString>

// Password hashing + HTTP Basic-auth helpers for the daemon.
//
// Hash format (printable ASCII, designed to be safe in JSON):
//
//     pbkdf2-sha256$<iterations>$<base64 salt>$<base64 digest>
//
// e.g. pbkdf2-sha256$120000$cmFuZG9tc2FsdA$abc123...
//
// Iterations are stored in the hash so we can raise the cost in future
// builds without invalidating older hashes.
namespace Auth {

// Returns a PBKDF2-SHA256 hash of `password` with a freshly generated
// 16-byte random salt. Format documented above.
QByteArray hashPassword(const QString& password);

// Constant-time verification of `password` against a stored hash.
// Returns false on any malformed input rather than throwing.
bool verifyPassword(const QString& password, const QByteArray& storedHash);

// Parses an HTTP `Authorization: Basic <base64>` header.
// On success writes the decoded user/password and returns true.
// Returns false (and leaves outputs untouched) on any parse error.
bool parseBasicHeader(const QByteArray& headerValue,
                      QString& user,
                      QString& password);

// Convenience: given a request's Authorization header value plus the
// expected username and stored password hash, return true iff the
// credentials match. Empty stored credentials are treated as a config
// error and refused (callers should gate by m_authEnabled first).
bool checkBasic(const QByteArray& headerValue,
                const QString& expectedUser,
                const QByteArray& expectedHash);

}  // namespace Auth
