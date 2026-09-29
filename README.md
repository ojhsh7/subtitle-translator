# 게임 자막 번역 프로토타입 (다국어)

## 간단한 사전 설명

Windows는 파일·자막 영역 캡처와 자동 번역, Android는 이미지 선택·공유와 터치 자르기를 지원하도록 구현했습니다. Windows는 일본어·영어·한국어·중국어를 로컬 OCR로 읽고, 개인 DeepL API 키가 있으면 고른 언어(기본 한국어)로 번역합니다. Android는 아직 일본어 → 한국어만 지원합니다. C++17 코어를 공유합니다.

**Windows에서 사용자 본인의 DeepL 키로 실제 번역 1회를 검증했습니다(샘플 이미지, CLI).** 키가 없으면 실제 OCR 원문까지만 표시합니다. Android의 실제 번역은 미검증입니다. iOS는 설계만 제공하며 구현 완료로 보고하지 않습니다.

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

#### 설치와 바탕화면 바로가기

아래 명령은 실행 폴더를 `%LOCALAPPDATA%\Programs\SubtitleTranslator`에 복사하고, 바탕화면에 **Subtitle Translator** 바로가기를 만듭니다. 관리자 권한은 필요하지 않습니다. `subtitle-translator` 폴더에서 실행하세요. 앱이 실행 중이면 먼저 종료하세요.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
```

바로가기는 `--auto` 인수로 실행합니다. 설정과 자막 영역이 저장돼 있으면 앱 창은 최소화된 채 **바로 자동 번역**을 시작합니다. 처음이면 설정 → 자막 영역 선택을 마치는 즉시 자동 번역이 시작됩니다. 임시 폴더에서 한 번만 실행하려면 `scripts\launch-windows.ps1`을 사용합니다(자동 번역은 직접 켭니다).

#### OCR 엔진: PaddleOCR (기본) + Tesseract (대체)

기본 OCR은 **PaddleOCR PP-OCRv5** 인식 모델(ONNX Runtime으로 실행)입니다. 합성 게임 글씨 192장에서 문자 정확도 **96.7%**(Tesseract 방식 84.4%), 외곽선 글씨 96.0%(66.9%), 그림자 글씨 92.6%(78.3%), 일본어 94.2%(68.4%), 10px 글씨 94.8%(72.9%)였고, 모델을 불러온 뒤 한 줄에 약 16ms(Tesseract 약 0.5초)가 걸렸습니다.

1. 선택한 자막 영역에서 글자 줄을 찾아 줄마다 잘라냅니다(밝기 변화가 많은 가로 띠).
2. 줄마다 한국어 모델과 중국어·일본어·영어 모델이 읽고, 확신도가 높은 쪽을 씁니다(원문 언어를 한 언어로 고르면 해당 모델만 실행).
3. PaddleOCR이 아무것도 읽지 못하거나 모델이 없으면 Tesseract가 대신 읽습니다(아래 전처리 포함).

설정의 **OCR 엔진**에서 Tesseract만 쓰도록 바꿀 수 있습니다. 모델은 `dist/windows/ocr/paddle`에, `onnxruntime.dll`은 실행 파일 옆에 있습니다(Windows System32의 다른 버전 onnxruntime.dll보다 앱 폴더 것이 먼저 쓰입니다).

#### 원문 언어와 번역 언어

| 원문 언어 선택 | Tesseract 모델 | DeepL에 보내는 원문 언어 |
|---|---|---|
| 자동 감지 · 일본어·영어·한국어 (기본) | `jpn+eng+kor` | 보내지 않음 → DeepL이 감지 |
| 자동 감지 · 일본어·영어·한국어·중국어(간체) | `jpn+eng+kor+chi_sim` | 보내지 않음 → DeepL이 감지 |
| 일본어 / 영어 / 한국어 / 중국어(간체·번체) | `jpn` / `eng` / `kor` / `chi_sim`·`chi_tra` | `JA` / `EN` / `KO` / `ZH` |

번역 언어: 한국어 `KO`, 영어(미국) `EN-US`, 영어(영국) `EN-GB`, 일본어 `JA`, 중국어(간체) `ZH-HANS`, 중국어(번체) `ZH-HANT`.

- "자동 감지"는 여러 Tesseract 모델을 함께 불러 단어마다 가장 잘 맞는 모델을 고르는 방식입니다. 한 언어만 고를 때보다 OCR이 느립니다(측정: 한 언어 약 0.08–0.12초, 자동 감지 약 0.2–0.26초). 게임 언어가 정해져 있으면 그 언어를 고르는 편이 빠르고 정확합니다.
- **번역 언어로 이미 쓰인 글자는 번역하지 않습니다.** 번역 언어가 한국어면 한글 부분은 그대로 두고 일본어·영어·중국어 부분만 DeepL로 보냅니다(예: `채주아: Hello!` → `채주아: 안녕하세요!`). 한글만 있는 대사는 DeepL 요청 자체를 보내지 않습니다. 이 판단은 C++ 공통 코어(`core/src/text.cpp`)가 글자 종류(한글·라틴·가나·한자)로 합니다. 약어(HP, NPC)와 OCR이 잘못 읽은 한 글자는 주변 문장을 따릅니다.
- 보내는 조각의 글자 종류로 DeepL 원문 언어를 알려 줍니다(라틴 → 영어, 가나 → 일본어). "you." 같은 짧은 조각을 DeepL이 엉뚱한 언어로 감지하는 것을 막기 위해서입니다. 한자만 있는데 일본어·중국어 모델이 모두 켜져 있으면 DeepL 감지에 맡깁니다.
- **Tesseract(대체 엔진)의 작은 글씨·외곽선 처리:** 원본으로 먼저 읽고, Tesseract 신뢰도가 90 미만이면 흰 글씨만 뽑은 이미지(3배), 1픽셀 두껍게 한 이미지, 밝기만으로 뽑은 이미지(ClearType 색 번짐 대비)를 차례로 읽어 신뢰도가 가장 높은 결과를 씁니다. 추출 이미지는 원본보다 신뢰도가 5 이상 높을 때만 채택합니다. 어려운 글씨는 OCR이 최대 4회 실행되어 한 대사에 약 0.5–1초가 걸립니다.
- 설정의 **자막 배치** 기본값은 "자동"입니다. 선택 영역 높이가 64픽셀 이하이면 한 줄로(PSM 7), 그보다 높으면 여러 줄로(PSM 6) 읽습니다.
- 언어를 바꾸면 최근 번역 기억을 지우고, 자동 번역 중이면 현재 대사를 새 언어로 다시 읽습니다.

#### 사용 순서

1. 실행 → 메인 창에서 **원문 언어**(기본: 자동 감지 · 일본어·영어·한국어)와 **번역 언어**(기본: 한국어)를 고릅니다. 설정의 **OCR 설치·언어 모델 확인**으로 필요한 모델이 있는지 확인합니다.
2. 선택 사항: 본인의 DeepL API 키 입력 → **키 확인**으로 사용량이 표시되는지 확인 → 전송·비용 안내 확인. 키를 채팅이나 소스에 적지 않습니다. 발급 방법은 아래 [DeepL API 키 준비](#deepl-api-키-준비-직접-발급)를 보세요.
3. **자막 영역 선택**에서 대사창 전체가 아닌 **원문 글자 줄만** 드래그합니다. 영역은 저장되어 다음 실행에도 사용합니다.
4. **자동 번역**(`Ctrl+Alt+P`)을 켭니다. 대사가 바뀌고 글자 출력이 멈추면 원문 바로 아래 줄에, 원문 시작 위치에 맞춰 번역이 나타납니다. `Ctrl+Alt+P`를 다시 누르면 멈추고 화면 자막을 지웁니다.
5. 한 번만 번역하려면 `Ctrl+Alt+T`를 누릅니다. 단축키가 다른 앱과 충돌하면 앱의 버튼을 사용합니다.
6. 전체 번역을 복사하거나 이동 가능한 창으로 보려면 **번역 결과 창 열기**를 누릅니다. 파일 선택·취소·최근 이미지 재시도도 가능합니다.

자동 번역은 0.4초마다 선택 영역만 캡처해 작은 흑백 이미지로 비교합니다. 두 번 연속 바뀌지 않으면(약 0.8초) OCR을 한 번 실행하고, 이미 읽은 화면이면 다시 읽지 않습니다. 바로 앞과 같은 원문은 DeepL에 다시 보내지 않습니다. 대사가 바뀔 때마다 새 원문이 전송되므로 사용량이 늘어납니다. 배경이 계속 움직여 화면이 안정되지 않으면 최대 3초마다 한 번 읽습니다. 앱 창이 자막 영역을 가리면 그동안은 건너뜁니다.

화면 자막에는 글자와 얇은 외곽선만 그립니다. 글자 크기는 선택한 원문 줄 높이에 맞춥니다. 클릭은 뒤의 게임으로 통과하도록 설정했습니다. 원문 아래 공간이 부족하면 위쪽에 표시하고, 원문을 가리지 않는 공간 자체가 없으면 별도 결과 창으로 표시합니다. 긴 번역은 화면에서 4줄까지 표시하며 나머지는 결과 창에서 확인합니다. 게임 창을 이동했으면 영역을 다시 선택하세요.

이미지·원문·번역 내역을 기본적으로 파일에 저장하지 않습니다. 선택한 키 저장소는 Windows Credential Manager이고, 일반 설정과 자막 영역 좌표는 현재 사용자 레지스트리의 PersonalTools/SubtitleTranslator입니다. 모니터 해상도나 배율이 바뀌면 저장된 영역을 쓰지 않고 다시 선택하도록 안내합니다.

보이는 픽셀을 캡처하므로 보호 콘텐츠·보안 데스크톱·독점 전체화면·가려진 자막은 성공을 보장하지 않습니다. 창 모드·테두리 없는 창 모드로 확인하세요.

### DeepL API 키 준비 (직접 발급)

이 앱은 사용자 본인의 키만 사용합니다. 개발자 키를 앱에 넣거나 서버로 중계하지 않습니다.

1. [DeepL API 요금제 페이지](https://www.deepl.com/pro-api#api-pricing)에서 요금제를 골라 가입합니다. DeepL 문서에 따르면 기존 DeepL Translator 계정이 있으면 로그아웃한 뒤 새 계정을 만들어야 합니다.
2. 계정의 [API Keys 탭](https://www.deepl.com/your-account/keys)에서 키를 복사합니다.
3. 앱의 설정에 붙여넣습니다. Free 키는 끝이 `:fx`이며, 앱이 키 형식에 맞춰 Free/Pro를 자동으로 선택합니다.
4. **키 확인**을 누릅니다. `GET /v2/usage`로 사용량만 조회하므로 번역 글자는 차감되지 않습니다. 이번 청구 기간 사용량이 표시되면 키와 요금제 설정이 맞는 것입니다.
5. 전송·비용 안내를 읽고 동의에 체크한 뒤 **설정 저장**을 누릅니다.

공식 문서로 확인한 내용: [인증 헤더·`:fx` 접미사·키 위치](https://developers.deepl.com/docs/getting-started/auth), [사용량 조회는 글자를 차감하지 않음](https://developers.deepl.com/api-reference/usage-and-quota), [가입 절차](https://developers.deepl.com/docs/getting-started/quickstart).

확인하지 못한 내용: Free 요금제의 월 무료 글자 수, 가입 시 결제 수단 등록 필요 여부. 공식 요금제 페이지가 자동 조회에 응답하지 않았으므로 가입 화면에서 직접 확인하세요.

### VS Code 빌드

이 폴더를 Windows VS Code에서 엽니다. MSVC2022 C++ workload, Windows SDK, CMake3.21+, Qt6.8+ MSVC2022 x64가 필요합니다. 도구 폴더에 `onnxruntime`(공식 onnxruntime-win-x64 1.30.0 압축 해제), `paddle`(PP-OCRv5 인식 모델 2개), `ocr`(Tesseract5와 traineddata, 대체 엔진)을 둡니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

도구 폴더 기본값은 `%LOCALAPPDATA%\SubtitleTranslatorToolchain`이며 `-ToolchainRoot`로 바꿀 수 있습니다(이 PC는 `D:\SubtitleTranslatorToolchain`). **`%TEMP%`에 두지 마세요.** C: 드라이브 여유 공간이 부족하면 Windows 저장 공간 센스가 임시 폴더를 비우면서 Qt 파일을 지웁니다(2026-09-29 실제 발생). 다른 Qt·CMake 경로는 `-QtRoot`, `-CMakeExe`로 지정합니다. 스크립트는 도구 폴더 안의 소스 사본으로 빌드합니다.

### 저장 이미지 CLI

```powershell
.\dist\windows\subtitle_cli.exe --input .\docs\evidence\japanese-fixture.png --tesseract .\dist\windows\ocr\tesseract.exe --tessdata .\dist\windows\ocr\tessdata
```

`--lang`(기본 `jpn+eng+kor`)과 `--target`(기본 `KO`)으로 원문·번역 언어를 고릅니다. 값은 위 표의 코드만 허용합니다. 결과 JSON은 `sourceText`, `translatedText`, `detectedSourceLanguage`를 담습니다.

`--translate`를 추가하면 자격 증명 저장소 또는 `SUBTITLE_TRANSLATOR_API_KEY` 환경 변수의 사용자 키를 사용합니다. Pro 계정은 `--pro`를 추가합니다. 키를 명령 인수로 받지 않습니다.

종료 코드: 0=요청한 처리 완료(OCR만 요청한 경우 포함), 2=입력 오류, 3=OCR 실패, 4=키 없음, 5=번역 실패, 6=전체 시간 초과. JSON의 `state`로 OCR 완료와 번역 완료를 구분하세요.

### 학습 순서

1. `core/include/subtitle/models.h`: 표준 C++ 모델·오류.
2. `core/src/core.cpp`: 요청 상태·텍스트 정리·중복 처리·취소.
3. `src/ocr_service.cpp`: 비동기 PNG 인코딩과 QProcess.
4. `src/translation_service.cpp`: 비동기 HTTPS와 응답 검증.
5. `src/change_detector.cpp`: 자동 번역에서 대사가 바뀐 뒤 멈췄는지 판단.
6. `src/main_window.cpp`: 플랫폼 입력과 UI 연결, 자동 번역 타이머.
7. `platforms/android`: Java UI·ML Kit·JNI·Keystore.

공통 코어에 Qt·Win32·Tesseract·모바일 SDK를 넣지 않았습니다. CMake 독립 빌드와 Linux g++ 테스트가 가능합니다. 실서비스 번역·실제 게임·고DPI·Android 실기기·iOS·깨끗한 PC 배포는 검증표를 확인하세요. Android 캡처·overlay와 서버 중계는 구현하지 않았습니다.
