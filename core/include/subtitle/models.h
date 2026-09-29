#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace subtitle {
enum class InputSource { ImageFile, SharedImage, ScreenCapture };
// Pixels stay owned by the platform adapter. No file path or OS object crosses this boundary.
struct ImageInput { InputSource source; int width; int height; };
struct TextBlock {
    std::string utf8Text;
    float left = 0, top = 0, width = 0, height = 0; // Normalized image coordinates.
};
struct OcrResult {
    std::string utf8Text;
    std::string language; // Engine language set, e.g. "jpn+eng+kor"; empty when unknown.
    std::vector<TextBlock> blocks;
};
enum class ErrorCode {
    None, InvalidImage, PermissionDenied, OcrUnavailable, OcrFailed, EmptyText,
    MissingCredential, ConsentRequired, AuthenticationFailed, RateLimited,
    QuotaExceeded, NetworkUnavailable, Timeout, InvalidResponse, Cancelled, Internal
};
enum class Stage { Import, Ocr, Translation };
struct Error {
    ErrorCode code = ErrorCode::None;
    Stage stage = Stage::Import;
    bool retryable = false;
    int providerStatus = 0;
    std::string safeMessage; // No API keys, raw server responses, or image bytes.
};
struct TranslationResult {
    std::uint64_t requestId = 0;
    OcrResult original;
    std::string sourceLanguage; // Detected by the provider, e.g. "EN"; empty when unknown.
    std::string targetLanguage; // Provider code, e.g. "KO" or "EN-US".
    std::string translatedText;
    bool reusedFromMemory = false;
};
std::string normalizeText(const std::string& utf8);
}
