# 일본어 → 한국어 자막 번역 프로토타입

## 간단한 사전 설명

Windows는 파일·자막 영역 캡처, Android는 이미지 선택·공유와 터치 자르기를 지원하도록 구현했습니다. 로컬 일본어 OCR 뒤 개인 DeepL API 키가 있으면 한국어를 요청합니다. C++17 코어를 공유합니다.

**사용자 API 키가 없어 실서비스 번역은 미검증입니다.** 키가 없으면 실제 OCR 원문까지만 표시합니다. iOS는 설계만 제공하며 구현 완료로 보고하지 않습니다.

## 자세한 설명

### 문서

- [멀티플랫폼 설계·지원표·흐름](docs/product-design.ko.md)
- [검증 결과와 제한](docs/validation.md)
- [공통 데이터·오류](core/include/subtitle/models.h)
- [공통 코어 C ABI](core/include/subtitle/core.h)
- [Android 안내](platforms/android/README.md)
- [서드파티·배포 조건](docs/THIRD_PARTY_NOTICES.md)

### Windows 실행

생성한 실행 파일: `dist/windows/SubtitleTranslator.exe`. DLL·플러그인·ocr 하위 폴더를 함께 유지하세요.

아래 명령으로 실행 폴더를 Windows 로컬 임시 폴더에 복사해 실행합니다. `subtitle-translator` 폴더에서 실행하세요.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch-windows.ps1
```

1. 실행 → 설정에서 Tesseract·일본어 모델 확인.
2. 선택 사항: 본인의 DeepL API 키와 Free/Pro 요금제 입력, 전송·비용 안내 확인. 키를 채팅이나 소스에 적지 않습니다.
3. 자막 영역 선택 → `Ctrl+Alt+T`. 단축키 충돌 시 캡처 버튼을 사용합니다.
4. 기본값인 **원문 아래에 번역 글자만 표시**를 켜면 게임 대사 바로 아래에 배경·테두리 없는 한국어 자막이 나타납니다. 자막 글자 부분만 영역으로 선택하세요. 키가 없으면 번역이 실행되지 않았다는 안내가 표시됩니다.
5. **위치 미리보기**로 배치를 확인하고 앱을 최소화하세요. `Ctrl+Alt+T`로 갱신하며, 표시 체크를 해제하면 화면 자막이 숨겨집니다.
6. 전체 번역을 복사하거나 이동 가능한 창으로 보려면 **한국어 결과 창 열기**를 누릅니다. 파일 선택·취소·최근 이미지 재시도도 가능합니다.

화면 자막에는 글자와 얇은 외곽선만 그립니다. 클릭은 뒤의 게임으로 통과하도록 설정했습니다. 원문 아래 공간이 부족하면 위쪽에 표시하고, 원문을 가리지 않는 공간 자체가 없으면 별도 결과 창으로 표시합니다. 긴 번역은 화면에서 4줄까지 표시하며 나머지는 결과 창에서 확인합니다. 게임 창을 이동했으면 영역을 다시 선택하세요.

이미지·원문·번역 내역을 기본적으로 파일에 저장하지 않습니다. 선택한 키 저장소는 Windows Credential Manager이고, 일반 설정은 현재 사용자 레지스트리의 PersonalTools/SubtitleTranslator입니다. 캡처 영역은 재실행 시 다시 선택합니다.

보이는 픽셀을 캡처하므로 보호 콘텐츠·보안 데스크톱·독점 전체화면·가려진 자막은 성공을 보장하지 않습니다. 창 모드·테두리 없는 창 모드로 확인하세요.

### VS Code 빌드

이 폴더를 Windows VS Code에서 엽니다. MSVC2022 C++ workload, Windows SDK, CMake3.21+, Qt6.8+ MSVC2022 x64, Tesseract5와 jpn 모델이 필요합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

현재 도구는 `%TEMP%\SubtitleTranslatorToolchain`에 있습니다. 다른 설치 경로는 `-QtRoot`, `-CMakeExe` 인수로 지정합니다. 스크립트는 WSL UNC의 MSBuild 문제를 피하려고 Windows 임시 폴더의 소스 사본으로 빌드합니다. 평소 개발에는 짧은 Windows 로컬 경로를 권장합니다.

### 저장 이미지 CLI

```powershell
.\dist\windows\subtitle_cli.exe --input .\docs\evidence\japanese-fixture.png --tesseract .\dist\windows\ocr\tesseract.exe --tessdata .\dist\windows\ocr\tessdata
```

`--translate`를 추가하면 자격 증명 저장소 또는 `SUBTITLE_TRANSLATOR_API_KEY` 환경 변수의 사용자 키를 사용합니다. Pro 계정은 `--pro`를 추가합니다. 키를 명령 인수로 받지 않습니다.

종료 코드: 0=요청한 처리 완료(OCR만 요청한 경우 포함), 2=입력 오류, 3=OCR 실패, 4=키 없음, 5=번역 실패, 6=전체 시간 초과. JSON의 `state`로 OCR 완료와 번역 완료를 구분하세요.

### 학습 순서

1. `core/include/subtitle/models.h`: 표준 C++ 모델·오류.
2. `core/src/core.cpp`: 요청 상태·텍스트 정리·중복 처리·취소.
3. `src/ocr_service.cpp`: 비동기 PNG 인코딩과 QProcess.
4. `src/translation_service.cpp`: 비동기 HTTPS와 응답 검증.
5. `src/main_window.cpp`: 플랫폼 입력과 UI 연결.
6. `platforms/android`: Java UI·ML Kit·JNI·Keystore.

공통 코어에 Qt·Win32·Tesseract·모바일 SDK를 넣지 않았습니다. CMake 독립 빌드와 Linux g++ 테스트가 가능합니다. 실서비스 번역·실제 게임·고DPI·Android 실기기·iOS·깨끗한 PC 배포는 검증표를 확인하세요. Android 캡처·overlay와 서버 중계는 구현하지 않았습니다.
