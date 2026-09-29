#include "credential_store.h"
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
namespace {
constexpr wchar_t Target[] = L"PersonalTools/SubtitleTranslator/DeepL";
QString problem(DWORD code) { return QStringLiteral("Windows 자격 증명 저장소 작업 실패 (%1).").arg(code); }
}
namespace Credentials {
bool save(const QString& key, QString& error) {
    error.clear();
    QByteArray bytes = key.toUtf8();
    if (bytes.isEmpty() || bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        error = QStringLiteral("저장할 API 키의 길이가 올바르지 않습니다."); return false;
    }
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC; c.TargetName = const_cast<LPWSTR>(Target);
    c.CredentialBlobSize = DWORD(bytes.size()); c.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool ok = CredWriteW(&c, 0) != 0;
    if (!ok) error = problem(GetLastError());
    SecureZeroMemory(bytes.data(), SIZE_T(bytes.size()));
    return ok;
}
QString load(QString& error) {
    error.clear(); PCREDENTIALW c = nullptr;
    if (!CredReadW(Target, CRED_TYPE_GENERIC, 0, &c)) {
        const auto code = GetLastError(); if (code != ERROR_NOT_FOUND) error = problem(code);
        return {};
    }
    QString key = QString::fromUtf8(reinterpret_cast<const char*>(c->CredentialBlob), c->CredentialBlobSize);
    if (c->CredentialBlob) SecureZeroMemory(c->CredentialBlob, c->CredentialBlobSize);
    CredFree(c); return key;
}
bool remove(QString& error) {
    error.clear();
    if (CredDeleteW(Target, CRED_TYPE_GENERIC, 0)) return true;
    const auto code = GetLastError(); if (code == ERROR_NOT_FOUND) return true;
    error = problem(code); return false;
}
}
