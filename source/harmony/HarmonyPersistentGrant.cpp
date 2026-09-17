#include "HarmonyPersistentGrant.h"

#include "HarmonyEnvironment.h"

#include <QDebug>

#include <cstdlib>
#include <cstring>

#include <filemanagement/fileshare/error_code.h>
#include <filemanagement/fileshare/oh_file_share.h>
#include <filemanagement/file_uri/oh_file_uri.h>

namespace HarmonyPersistentGrant {

namespace {

bool s_probed = false;
bool s_supported = false;
QString s_detail;

const char *errorName(FileManagement_ErrCode code)
{
    switch (code) {
    case ERR_OK:                   return "ERR_OK";
    case ERR_PERMISSION_ERROR:     return "ERR_PERMISSION_ERROR (FILE_ACCESS_PERSIST not granted)";
    case ERR_INVALID_PARAMETER:    return "ERR_INVALID_PARAMETER";
    case ERR_DEVICE_NOT_SUPPORTED: return "ERR_DEVICE_NOT_SUPPORTED (no FolderAuthorization syscap)";
    case ERR_EPERM:                return "ERR_EPERM";
    case ERR_ENOENT:               return "ERR_ENOENT";
    case ERR_ENOMEM:               return "ERR_ENOMEM";
    case ERR_UNKNOWN:              return "ERR_UNKNOWN";
    }
    return "unrecognised";
}

void probe()
{
    s_probed = true;

    // Any real path will do. The question this asks is whether the capability and
    // the permission exist at all, and both are checked before the URI is looked at:
    // a device without FolderAuthorization answers 801 whatever it is handed. So we
    // use our own sandbox root, which always exists, rather than a user folder we
    // may have no way to name.
    const QByteArray path = HarmonyEnvironment::writableDocumentsRoot().toUtf8();
    char *uri = nullptr;
    const FileManagement_ErrCode uriRc =
        OH_FileUri_GetUriFromPath(path.constData(), path.size(), &uri);
    if (uriRc != ERR_OK || !uri) {
        s_detail = QStringLiteral("OH_FileUri_GetUriFromPath: %1, rc=%2")
                       .arg(QString::fromLatin1(errorName(uriRc)))
                       .arg(uriRc);
        free(uri);
        return;
    }

    FileShare_PolicyInfo policy {};
    policy.uri = uri;
    policy.length = static_cast<unsigned int>(std::strlen(uri));
    policy.operationMode = READ_MODE;

    // Check rather than persist: this is a query with no side effects, so a probe on
    // every launch cannot accumulate grants or trip the "only already-granted
    // temporary permissions may be persisted" rule.
    bool *results = nullptr;
    unsigned int resultNum = 0;
    const FileManagement_ErrCode rc =
        OH_FileShare_CheckPersistentPermission(&policy, 1, &results, &resultNum);
    free(results);
    free(uri);

    // 0 means the query ran, not that the path is persisted -- an unpersisted path
    // answers false, which is the expected result here and still proves the
    // capability is live. 801 and 201 are the two answers that rule it out.
    s_supported = (rc != ERR_DEVICE_NOT_SUPPORTED && rc != ERR_PERMISSION_ERROR);
    s_detail = QStringLiteral("OH_FileShare_CheckPersistentPermission: %1, rc=%2")
                   .arg(QString::fromLatin1(errorName(rc)))
                   .arg(rc);
    qInfo() << "HarmonyPersistentGrant:" << s_detail
            << "- persistent PDF references are"
            << (s_supported ? "available" : "unavailable");
}

} // namespace

bool isSupported()
{
    if (!s_probed) {
        probe();
    }
    return s_supported;
}

QString capabilityDetail()
{
    if (!s_probed) {
        probe();
    }
    return s_detail;
}

} // namespace HarmonyPersistentGrant
