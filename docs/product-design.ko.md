# 멀티플랫폼 화면 번역 제품 설계

## 간단한 사전 설명

이미지 한 장을 입력받아 로컬 일본어 OCR을 실행하고, 사용자가 허용한 경우 인식 텍스트만 DeepL로 보내 한국어를 표시한다. **C++ 공통 코어 + 플랫폼별 UI·입력·OCR 어댑터**를 선택한다. Windows는 Qt Widgets, Android는 네이티브 UI, iOS는 SwiftUI를 목표로 한다. iOS는 이 작업에서 설계 대상이며 빌드·실행 완료로 표시하지 않는다.

공식 문서 확인일: 2026-09-29. 아래의 “가능”은 플랫폼이 제공하는 기능 범위이며, 이 저장소의 구현·검증 완료 여부와는 별개다. 실제 검증은 validation.md에 기록한다.

## 자세한 설명

### 플랫폼별 기능 지원표

| 기능 | Windows 데스크톱 | Android | iOS |
|---|---|---|---|
| 저장 이미지 가져오기 | 가능. 사용자 파일 선택, 관리자 권한 불필요 | 가능. 시스템 Photo Picker 또는 문서 선택기로 선택한 URI 접근. 전체 사진 권한 요구하지 않음 | 가능. PhotosPicker로 선택한 항목 접근. 전체 사진 보관함 권한을 기본 요구하지 않음 |
| 다른 앱에서 이미지 공유 받기 | 첫 버전은 파일 열기 | 가능. ACTION_SEND + image/*, 제공된 URI 읽기 권한 확인 | 가능. Share Extension + 이미지 유형 등록. 앱 확장의 별도 수명·메모리 고려 |
| 다른 앱 화면 캡처 | 조건부 가능. 사용자 데스크톱의 보이는 픽셀 캡처. 보안 화면·보호 콘텐츠·독점 전체화면 성공 보장 안 함 | 조건부 가능. MediaProjection의 사용자 동의, 세션별 새 토큰, 필요한 foreground service. **이미지 번역 검증 후 별도 기술 검증** | 일반 앱에서 임의로 상시 캡처하는 기능은 지원 대상으로 삼지 않음. ReplayKit 방송 확장은 별도 제품·사용자 시작·제약 검증 필요 |
| 자막 영역 선택 | 마우스 드래그. 한 모니터 내부 사각형, 화면 배율 고려 | 앱 안에서 확대·이동·자르기. 추가 위험 권한 불필요 | 앱 안에서 확대·이동·자르기. 추가 위험 권한 불필요 |
| 번역 표시 | 원문 아래에 배경 없는 글자 자막 또는 이동·크기 조절 가능한 결과 창 | 앱 내 원문·한국어 결과. 다른 앱 위 overlay는 후속 조건부 기능 | 앱 내 원문·한국어 결과. 다른 앱 위 범용 overlay는 지원하지 않음 |
| 단축키 | RegisterHotKey. 충돌 시 버튼 대체. 관리자 권한 불필요 | 터치 버튼·공유 진입. PC식 전역 단축키는 목표 아님 | 터치 버튼·공유 진입 |
| 백그라운드 처리 | 앱이 실행 중이면 비동기 작업 지속 가능. 절전·잠금·종료 이후 성공 보장 안 함 | 첫 버전은 foreground 단발 요청. background 진입 시 취소, 복귀 후 명시적 재시도. 계속 캡처하려면 별도 서비스·정책 검증 | 일반 앱은 background에서 중단될 수 있음. 짧은 유예·시스템 스케줄 작업을 상시 처리로 간주하지 않음 |
| 보호된 화면 | 차단을 우회하지 않음 | FLAG_SECURE 등으로 막힌 화면 우회 금지 | 보호된 콘텐츠·시스템 제한 우회 금지 |

Windows 캡처는 [Qt QScreen](https://doc.qt.io/qt-6/qscreen.html#grabWindow), 단축키는 [Microsoft RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey) 기준이다. 캡처 좌표는 논리 픽셀, OCR 이미지는 물리 픽셀로 구분한다. 다른 창에 가려진 내용이나 보호 콘텐츠를 복원하지 않는다.

Android 입력 근거: [Photo Picker](https://developer.android.com/training/data-storage/shared/photo-picker), [공유 받기](https://developer.android.com/training/sharing/receive). 외부 URI·MIME은 신뢰하지 않고 디코딩 크기와 읽기 오류를 검사한다. [MediaProjection](https://developer.android.com/media/grow/media-projection)은 세션마다 동의가 필요하며 Android 14 이상 대상 앱에는 관련 foreground service 설정이 요구된다. Overlay는 별도 [SYSTEM_ALERT_WINDOW](https://developer.android.com/reference/android/Manifest.permission#SYSTEM_ALERT_WINDOW) 특수 접근 허용이 필요하다. [FLAG_SECURE](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#FLAG_SECURE)를 우회하지 않는다.

iOS 입력 근거: [PhotosPicker](https://developer.apple.com/documentation/photokit/bringing-photos-picker-to-your-swiftui-app), [Share Extension](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/Share.html). [ReplayKit](https://developer.apple.com/documentation/replaykit)은 사용자 주도의 녹화·방송 API이며 범용 overlay 권한을 제공한다는 뜻이 아니다. [Background Tasks](https://developer.apple.com/documentation/backgroundtasks)의 시스템 스케줄링은 실시간 상시 번역 보장이 아니다.

### 공통 사용자 흐름

1. 최초 설정: 로컬 OCR 안내, 개인 API 키(선택), 외부 전송·비용 안내, 동의.
2. 이미지 입력: 파일 / OS 사진 선택 / 공유 / Windows 캡처 중 하나.
3. 입력 검증: 이미지 형식·크기·방향 정규화. 모바일은 확대하고 자막 부분을 자른다.
4. 번역 실행: OCR 중 → 원문 표시 → 빈 결과·오류 처리 → 중복 텍스트 확인 → 필요한 경우 API 요청.
5. 결과: 한국어·일본어 원문 표시. 취소·최근 이미지 재시도·영역 수정 제공.
6. 취소 또는 새로운 요청: 이전 요청 ID를 무효화해 늦게 도착한 결과가 새 화면을 덮지 못하게 한다.

사용자가 명시적으로 실행하기 전 이미지를 업로드하거나 번역을 요청하지 않는다. 같은 세션의 직전 성공 원문과 같으면 메모리의 결과를 재사용한다. 서버 오류의 자동 재시도는 하지 않으며 비용·중복 요청을 피한다.

### 플랫폼별 사용자 흐름과 화면

**Windows**

실행 → 설정 → 모니터 선택 → 원문 글자 영역 드래그 → 위치 미리보기 → 앱 최소화 → 게임에서 Ctrl+Alt+T → 원문 바로 아래에 한국어 글자만 표시. 별도 결과 창은 전체 보기·복사 용도로 남긴다.
파일 선택도 같은 OCR 진입점을 사용한다.

```text
[게임 자막 번역]                    [설정]
[모니터 ▼] [영역 선택] [캡처·번역] [취소]
[이미지 파일 선택] [최근 이미지 재시도]
[현재 상태 / 복구 방법]
[캡처 미리보기]
[일본어 원문]
                     [한국어 결과 창 열기]

별도 창: [번역 상태]
         [큰 글씨 한국어 · 선택/복사]
```

**Android**

앱에서 이미지 선택 또는 갤러리/스크린샷의 공유 → 앱 선택 → 이미지 확대·이동 → 자막 영역 조절 → 번역 → 한국어/원문.
뒤로 가기·선택 취소·읽기 거부는 오류 안내 후 입력 화면을 유지한다. 회전은 비트맵 전체를 Bundle에 넣지 않고 상태 소유자에 유지한다. 프로세스 종료 후 URI 권한을 잃었으면 재선택을 요청한다.

```text
[이미지 번역]                      [설정]
[이미지 선택]
[핀치 확대 / 이동 / 자막 영역]
[영역 초기화]           [선택 영역 번역]
[처리 상태] [취소 / 재시도]
[한국어]
[원문 보기]
```

**iOS — 설계만**

PhotosPicker → 자막 자르기 → 번역 → 결과. 공유 시 Share Extension에서 받은 이미지로 동일한 처리 흐름을 구성하되 확장 내 완료 또는 명시적 앱 복귀를 선택한다. 비공식 방법으로 컨테이너 앱을 강제 실행하지 않는다. 별도 App Group을 쓴다면 필요한 파일만 임시 공유하고 소거·보호 정책을 정한다. 앱 background 진입 시 작업을 취소하고 복귀 후 재시도한다.

### C++ 공통 코어 경계

| 공통 코어 — C++17 표준 라이브러리 | 플랫폼 어댑터 |
|---|---|
| ImageInput: 출처·폭·높이 | 이미지 바이트 소유·디코딩·EXIF 방향·자르기 |
| OcrResult: UTF-8 원문·언어·선택적 정규화 영역 | Tesseract / ML Kit / Apple Vision |
| 텍스트 공백 정리·최대 길이 검사 | 파일 선택·공유 URI·권한 |
| 요청 ID·상태 전이·취소 후 결과 폐기 | 스레드·코루틴·이벤트 루프·앱 수명 주기 |
| 직전 성공 번역의 메모리 캐시 | HTTPS·TLS·시간 제한·네트워크 취소 |
| TranslationResult와 Error 형식 | 키 저장소·UI·클립보드·단축키·Windows 캡처 |

core에는 Qt, Win32, JNI, Android SDK, Apple 프레임워크, Tesseract를 넣지 않는다. 공개 C ABI(core.h)는 Java JNI와 Objective-C++에서 연결하기 쉽도록 제공한다. 코어는 한 소유 스레드에서 호출한다.

공통 오류는 ErrorCode, Stage, retryable, providerStatus, safeMessage로 표현한다. 이미지 오류, 읽기 권한 거부, OCR 없음/실패/빈 원문, 키 없음, 미동의, 인증 실패, 호출 제한, 한도 초과, 네트워크 실패, 시간 초과, 잘못된 응답, 취소를 구분한다. 메시지에 키·원시 HTTP 응답·이미지 바이트를 넣지 않는다.

### UI 기술 비교와 선택

| 기준 | Qt Quick/QML 공통 UI | 플랫폼별 UI |
|---|---|---|
| 터치·레이아웃 | 터치 UI 가능. OS 선택기·공유·권한에는 별도 bridge 필요 | 시스템 선택기·공유·접근성·수명 주기 연동이 자연스러움 |
| C++ 학습 | C++ 비중 높으나 QML·플랫폼 bridge도 학습 필요 | C++ 코어 외 Java/Kotlin·Swift 학습 필요 |
| UI 코드 유지보수 | 공통 코드 많음. 플랫폼별 예외를 공통 UI에 반영 | UI를 각각 관리하지만 각 OS의 동작과 맞추기 쉬움 |
| 배포 | Qt 모바일 toolchain, QML 플러그인·런타임·라이선스 검토 | Android Gradle/SDK, iOS Xcode·서명 등 각 기본 도구 사용 |
| 이 제품과 적합성 | 화면 구성이 매우 같고 QML 경험이 충분할 때 유리 | 입력·권한·공유·창 모델이 서로 다른 현재 제품에 적합 |

**선택: 얇은 C++ 코어 + Windows Qt Widgets + Android 네이티브 UI + iOS SwiftUI.**
모바일에 Widgets를 가져가지 않는다. Windows는 이미 준비한 C++ 도구와 데스크톱 조작에 맞는 Widgets를 이용한다. Android는 작은 프로토타입에 네이티브 View를 사용해 이미지 제스처와 Java/JNI 경계를 드러내고, 향후 Compose로 바꿔도 코어는 유지한다. iOS는 PhotosPicker·Vision·Keychain 연동을 우선한다.

Qt 사용 환경 및 배포는 [Qt Windows 지원](https://doc.qt.io/qt-6/windows.html), [Windows 배포](https://doc.qt.io/qt-6/windows-deployment.html), [LGPL 조건](https://www.qt.io/development/open-source-lgpl-obligations)을 확인한다. DLL을 동적으로 사용하더라도 라이선스 고지·해당 소스 제공 조건·교체 가능성 검토가 생략되는 것은 아니다.

### OCR·번역·키 선택

- Windows: Tesseract CLI + 일본어 모델. 앱과 별도 비동기 프로세스로 실행하며 캡처는 stdin으로 전달한다.
- Android: [ML Kit 일본어 Text Recognition](https://developers.google.com/ml-kit/vision/text-recognition/v2/android)의 모델 포함 패키지를 우선 사용한다. API 23 이상이 필요한 조합인지 선택한 라이브러리 버전 기준으로 고정한다. 포함 모델은 최초 OCR 모델 다운로드 대기를 줄이지만 앱 크기가 커진다.
- iOS: [Vision 텍스트 인식](https://developer.apple.com/documentation/vision/recognizing-text-in-images). 실제 기기에서 지원 언어 목록과 ja-JP를 확인한 뒤 사용한다.
- 번역: [DeepL HTTPS API](https://developers.deepl.com/api-reference/translate/request-translation). 일본어 텍스트만 전송. API Free/Pro endpoint 구분, 인증은 헤더, 오류·시간 제한 처리.
- 개인용: 사용자가 자신의 키를 입력. Windows Credential Manager, Android Keystore로 보호한 암호문, iOS Keychain을 각각 사용. 앱에 개발자 공용 키를 포함하지 않는다.
- 배포용: 공유 비밀키가 필요할 때 서버 중계를 재검토한다. 사용자 인증·사용량 제한·요금 책임·로그 최소화가 필요하므로 이번 단계에서 서버를 구현하지 않는다.
- 로컬 OCR과 번역문도 민감할 수 있어 영구 내역 저장은 기본 제공하지 않는다.

### 필요한 개발·테스트 환경

| 대상 | 필수 환경 | 이 세션에서 확인한 범위 |
|---|---|---|
| Windows | Windows x64, VS Code, MSVC 2022 C++ workload, Windows SDK, CMake, Qt6, Tesseract | MSVC14.44·SDK 22621/26100·VS Code 확인. CMake/Qt/Tesseract는 별도 준비 후 검증 기록 |
| C++ core | C++17 컴파일러, CMake 또는 직접 빌드 | Windows MSVC 및 WSL g++로 독립 검증 |
| Android | Android Studio, 호환 JDK·Gradle·AGP, SDK, NDK·CMake(JNI), 에뮬레이터 또는 USB 디버깅 기기 | SDK·Studio 설치 확인. 최초 점검 시 연결 기기·AVD 없음. 빌드 결과와 기기 결과를 분리 |
| iOS | 지원 macOS·Xcode, iOS Simulator, 실제 iPhone, 개발 서명 환경 | 이 Windows 환경에서 macOS/Xcode·iPhone 실행 환경 확인하지 못함. 빌드·실기기 미검증 |

[Xcode 시스템 요구사항](https://developer.apple.com/xcode/system-requirements)을 따라 macOS/Xcode/iOS 조합을 맞춘다. Windows에서 iOS 앱을 로컬로 빌드·서명·실기기 검증할 수 있다고 가정하지 않는다. Android USB 디버깅 허용 등 기기 사용자의 조작은 별도 필요하다.

### 단계별 완료 기준

1. **설계:** 각 기능의 지원/조건부/미지원, 데이터 경계, 권한, 기술 선택, 테스트 환경이 문서로 확인된다.
2. **저장 이미지 최소 흐름:** 실제 일본어 이미지 → 실제 OCR 결과; 키가 있으면 실제 한국어 응답. 빈 OCR·오류·취소 처리. 모델 크기·시간·메모리·네트워크 여부를 같은 환경에서 기록한다. 키가 없으면 번역 완료 기준은 미충족으로 남긴다.
3. **Windows:** 실제 빌드·실행, 파일/영역 캡처·단축키·창 이동·설정·비동기·오류 복구 검증. 고DPI·복수 모니터·게임·잠금 화면은 별도 수동 확인 항목으로 남길 수 있다.
4. **Android 이미지 번역:** APK 빌드, picker와 ACTION_SEND 입력, 확대·자르기, 원문·번역, 회전·취소·재시도·URI 읽기 실패 처리. 에뮬레이터와 실기기 결과를 별개로 기록한다.
5. **Android 캡처/overlay 기술 검증:** 4의 흐름 검증 이후에만 착수. 시작/중지·세션 동의·특수 권한 거부·foreground notification·화면 잠금·서비스 종료·보호화면 테스트. 상시 캡처 기본값 금지.
6. **iOS:** macOS/Xcode 확보 후 PhotosPicker·Share Extension·crop·Vision·Keychain 검증. 실기기 측정 전 성능 완료를 주장하지 않는다.
7. **배포:** 재현 가능한 빌드·서명, 라이선스 고지·dependency 목록, 스토어 개인정보와 데이터 사용 고지 검토. [Apple App Review](https://developer.apple.com/app-store/review/guidelines/) 및 대상 스토어의 제출 시점 정책을 다시 확인한다.

성능은 실제 측정한 기기·입력·빌드 모드·횟수를 명시한다. 모델 파일 크기와 설치 앱 크기, 프로세스 메모리와 전체 기기 메모리, HTTP 본문 크기와 실제 전송 바이트를 혼동하지 않는다. 측정하지 않은 값은 “미측정”이며 추정을 제시하는 경우 반드시 “추정”으로 표시한다.
