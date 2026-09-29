# Third-party components

This is a personal development prototype. No public release has been performed.

- Qt 6.8.3 Core/Gui/Widgets/Network/Concurrent/Test: dynamically linked; LGPLv3 intended for eligible modules. Preserve license texts, corresponding source access, notices, and users' replacement rights before sharing binaries. Module SBOMs are in the Qt SDK sbom directory.
  - Exact source: https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz
  - Conditions: https://www.qt.io/development/open-source-lgpl-obligations
- Tesseract 5.5.3 Windows build 20260724, linked from UB Mannheim documentation: engine Apache-2.0. Bundled Leptonica and other DLLs have their own terms. This extracted runtime is for local testing; full transitive notices/source obligations must be handled before public redistribution.
  - https://github.com/tesseract-ocr/tesseract/releases/tag/5.5.3
  - https://github.com/UB-Mannheim/tesseract/wiki
- tessdata_fast Japanese model: Apache-2.0. Exact SHA-256 and bytes are recorded in evidence.
  - https://github.com/tesseract-ocr/tessdata_fast
- Android ML Kit text-recognition-japanese 16.0.1: Google SDK terms and data disclosure apply. A bundled model enables local inference but does not rule out SDK diagnostic traffic.
  - https://developers.google.com/ml-kit/terms
  - https://developers.google.com/ml-kit/android-data-disclosure
- DeepL: external service with the user's own API account. No key or account is supplied.
- Android/Gradle/Microsoft build tools retain their own licenses. Android debug signing is for development, not a production release identity.
