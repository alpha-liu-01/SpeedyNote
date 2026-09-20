#include "HarmonyEnvironment.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <filemanagement/environment/error_code.h>
#include <filemanagement/environment/oh_environment.h>

namespace HarmonyEnvironment {

namespace {

// The NDK call is cheap but not free, and its answer cannot change within a
// process: the sandbox mount is established at startup, so a permission granted
// after launch only takes effect on the next run.
QString s_documentsDir;
bool s_probed = false;

// Why s_documentsDir is empty, kept so the About tab can show it. Every branch
// below that returns without setting s_documentsDir sets this instead.
QString s_failureDetail;

const char *errorName(FileManagement_ErrCode code)
{
    switch (code) {
    case ERR_OK:                  return "ERR_OK";
    case ERR_PERMISSION_ERROR:    return "ERR_PERMISSION_ERROR (permission not granted)";
    case ERR_INVALID_PARAMETER:   return "ERR_INVALID_PARAMETER";
    case ERR_DEVICE_NOT_SUPPORTED:return "ERR_DEVICE_NOT_SUPPORTED (no FolderObtain syscap)";
    case ERR_EPERM:               return "ERR_EPERM";
    case ERR_ENOENT:              return "ERR_ENOENT";
    case ERR_ENOMEM:              return "ERR_ENOMEM";
    case ERR_UNKNOWN:             return "ERR_UNKNOWN";
    }
    return "unrecognised";
}

void probe()
{
    s_probed = true;

    char *raw = nullptr;
    const FileManagement_ErrCode rc = OH_Environment_GetUserDocumentDir(&raw);
    if (rc != ERR_OK || !raw) {
        // Expected on phones (no syscap) and before the permission is granted.
        // Logged rather than silent because the two causes need different fixes
        // and are otherwise indistinguishable from "saving is broken".
        s_failureDetail = QStringLiteral("OH_Environment_GetUserDocumentDir: %1, rc=%2")
                              .arg(QString::fromLatin1(errorName(rc)))
                              .arg(rc);
        qWarning() << "HarmonyEnvironment: no user Documents dir, rc =" << rc
                   << errorName(rc);
        free(raw);
        return;
    }

    const QString path = QString::fromUtf8(raw);
    free(raw);

    // The API can hand back a path that is mapped but not yet materialised.
    if (!QDir(path).exists() && !QDir().mkpath(path)) {
        s_failureDetail = QStringLiteral("cannot create %1").arg(path);
        qWarning() << "HarmonyEnvironment: user Documents dir is not usable:" << path;
        return;
    }

    // Verify we can create both a directory *and* a file inside it, rather than
    // trusting access(W_OK). None of these implies the others here: the sharefs
    // layer behind the user folders applies its own rules on top of the ordinary
    // permission bits, and we have seen mkdir succeed in a folder where creating
    // a file then fails. A .snb bundle needs both, so the probe checks both and
    // reports errno, which is the only thing that distinguishes a missing
    // permission from an unsupported operation.
    const QString canaryDir = QDir(path).filePath(QStringLiteral(".speedynote-probe"));
    const QString canaryFile = canaryDir + QStringLiteral("/canary");
    QFile::remove(canaryFile);            // in case a previous run was killed
    QDir().rmdir(canaryDir);              // mid-probe, leaving these behind
    if (!QDir().mkdir(canaryDir)) {
        s_failureDetail = QStringLiteral("mkdir in %1: %2")
                              .arg(path, QString::fromLocal8Bit(strerror(errno)));
        qWarning() << "HarmonyEnvironment: cannot create directories in" << path
                   << "-" << strerror(errno) << "- bundles cannot be stored there";
        return;
    }
    const int fd = ::open(canaryFile.toUtf8().constData(),
                          O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        s_failureDetail = QStringLiteral("open() in %1: %2")
                              .arg(path, QString::fromLocal8Bit(strerror(errno)));
        qWarning() << "HarmonyEnvironment: can create directories but not files in"
                   << path << "-" << strerror(errno)
                   << "- bundles cannot be stored there";
        QDir().rmdir(canaryDir);
        return;
    }
    ::close(fd);
    QFile::remove(canaryFile);
    QDir().rmdir(canaryDir);

    s_documentsDir = path;
    qInfo() << "HarmonyEnvironment: user Documents dir =" << path;
}

} // namespace

QString userDocumentsDir()
{
    if (!s_probed) {
        probe();
    }
    return s_documentsDir;
}

QString accessFailureDetail()
{
    if (!s_probed) {
        probe();
    }
    return s_failureDetail;
}

bool hasUserDocumentsAccess()
{
    // A non-empty result already means the probe created a directory and a file
    // there successfully. Re-checking with QFileInfo::isWritable() would only
    // add back the weaker access(W_OK) test that the probe exists to replace.
    return !userDocumentsDir().isEmpty();
}

QString writableDocumentsRoot()
{
    if (hasUserDocumentsAccess()) {
        return userDocumentsDir();
    }

    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/notebooks");
    QDir().mkpath(fallback);
    return fallback;
}

} // namespace HarmonyEnvironment
