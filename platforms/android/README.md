# Android 이미지 번역

## 간단한 사전 설명

Android 9(API28)+에서 이미지 선택·공유 → 확대·자르기 → 로컬 일본어 OCR → 개인 DeepL 키로 한국어 번역을 연결한 네이티브 Java 프로토타입입니다. **APK 빌드와 Android15 에뮬레이터 OCR 흐름은 검증했으며, 실기기와 실제 DeepL 번역은 미검증입니다.**

## 자세한 설명

### 실행·조작

- 결과 APK: ../../dist/android/SubtitleTranslator-debug.apk (프로젝트 루트 기준 dist/android).
- 파일 선택은 시스템 문서 선택기입니다. 전체 사진·저장소 권한을 요청하지 않습니다. 다른 앱의 이미지 공유(ACTION_SEND)도 받습니다.
- 두 손가락으로 확대합니다. “영역 그리기” 모드에서 한 손가락으로 자막을 드래그합니다. 같은 버튼을 누르면 “이미지 이동” 모드로 바뀝니다.
- “선택 영역 번역”은 실제 ML Kit 일본어 OCR을 실행합니다. 키·동의가 없으면 원문까지만 표시합니다.
- 취소·재시도를 지원하며, background 전환 시 진행 중 요청을 취소합니다. 회전 시 이미지를 Bundle에 넣지 않고 상태 소유 객체를 유지합니다. OS가 프로세스를 종료하면 재선택을 안내합니다.
- API 키 저장은 선택 사항입니다. AndroidKeyStore AES-GCM 키로 암호화한 값만 앱 저장소에 보관하며 백업은 비활성화했습니다. 개발자 공용 비밀키·서버 중계는 없습니다.
- URI 읽기 거부·잘못된 이미지·빈 OCR·시간 초과·인증·사용 한도·네트워크 실패를 안내합니다. 원시 서버 응답이나 키를 로그로 남기지 않습니다.

### 빌드

고정 버전: AGP9.3.2, Gradle9.5.0, compile/target SDK37, min SDK28, NDK29.0.14206865, CMake3.22.1, ML Kit Japanese16.0.1. 실제 빌드는 Android Studio JBR25.0.2로 수행했습니다. Java 소스 호환 수준은17입니다.

Windows PowerShell에서 프로젝트 루트 기준:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-android.ps1
```

Android Studio로 이 폴더를 열 수도 있습니다. 공통 코어의 상대 경로가 유지되도록 전체 subtitle-translator 폴더 구조를 함께 보관하세요. Wrapper를 통한 SDK/NDK 등 다운로드에는 네트워크와 디스크 공간이 필요합니다.

### 테스트 다시 실행

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-android.ps1 -BuildTests
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb devices -l
# 아래 serial은 이번 검증용 에뮬레이터입니다. 본인 기기를 사용할 때는 명시적으로 변경하세요.
& $adb -s emulator-5580 install -r .\dist\android\SubtitleTranslator-debug.apk
& $adb -s emulator-5580 install -r "$env:TEMP\SubtitleTranslatorToolchain\android-stage\platforms\android\app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk"
& $adb -s emulator-5580 shell am instrument -w -r dev.personal.subtitle.test/androidx.test.runner.AndroidJUnitRunner
```

테스트는 앱의 키·동의를 비활성화하고 테스트 이미지를 사용합니다. 개인 사용 중인 앱 데이터가 있는 기기보다 별도 에뮬레이터를 사용하세요. APK는 개인 테스트용 debug 서명이며 스토어 배포본이 아닙니다.

이번 검증용 AVD는 C: 공간 부족으로 `D:\SubtitleTranslatorTest-20260929\avd`에 만들었습니다. 재사용하려면 `ANDROID_AVD_HOME`을 이 경로로 설정하고 `SubtitleTranslatorTest` AVD를 실행합니다. 계측 테스트는 Wi-Fi·모바일 데이터를 끈 상태에서 수행했습니다.

### 검증

Android15 API35 x86_64, Pixel5 프로필의 전용 headless 에뮬레이터에서 다음 3개 계측 테스트가 통과했습니다.

- 실제 공유 이미지 → 터치 자르기 → ML Kit OCR → 회전 유지 → 취소·재시도 → URI 권한 거부 → 재입력 → background 취소.
- 실제 시스템 이미지 선택기를 열고 뒤로 가기로 취소.
- 실제 JNI를 통한 UTF-8 일본어·한국어 전달과 취소 후 오래된 결과 폐기.

Wi-Fi·모바일 데이터를 끈 상태에서 포함 모델 OCR이 성공했습니다. ML Kit SDK 자체 진단 데이터 가능성은 별도이며 [Google 데이터 고지](https://developers.google.com/ml-kit/android-data-disclosure)를 확인해야 합니다.

테스트 이미지와 가짜 번역 문자열은 androidTest에만 있습니다. 프로덕션 앱에서 테스트 번역을 성공처럼 보여주는 모드는 없습니다.

**실기기:** 연결된 기기가 없어 미검증. **실제 API:** 키가 없어 미검증. Play 배포·서명·스토어 심사도 수행하지 않았습니다. OS 화면 캡처·overlay는 이 흐름과 실서비스 검증 후 별도 단계이며 구현하지 않았습니다.
