# 출발점 기록 (baseline) — 2026-10-01

## 간단한 사전 설명

`feature/media-modes` 브랜치를 만들고, 커밋하지 않았던 QA 보강·마우스 범위 작업을 첫 커밋 `9d13db9`로 보존했다. 이 상태에서 Release 빌드와 CTest 7개 그룹이 모두 통과했고 컴파일 경고는 0개다. 지시서 1절의 사실은 대부분 맞고, 다른 점 5가지를 아래에 적었다.

## 자세한 설명

### 브랜치

| 항목 | 값 |
|---|---|
| 시작 브랜치 | `ocr-accuracy-foreign-only` (HEAD `dd8a957`) + 커밋 안 된 17개 파일·증거 폴더 |
| 작업 브랜치 | `feature/media-modes` |
| 기준 커밋 | `9d13db9` baseline: QA hardening and custom mouse range work before media modes |
| 원격 푸시 | 하지 않음 |

### 빌드·테스트 (2026-10-01 15:0x KST, 이 PC)

| 항목 | 결과 |
|---|---|
| 명령 | `scripts\build-windows.ps1 -ToolchainRoot D:\SubtitleTranslatorToolchain` |
| 빌드 | 성공, MSVC `warning C` 0건 |
| CTest | 7/7 통과, 47.54초 |
| 그룹별 | translation_protocol 2.18s · local_checks 9.65s · paddle_ocr 7.69s · change_detection 0.13s · game_windows 0.22s · windows_flow 27.65s · shared_core 0.01s |
| 배포 경고 | windeployqt "Translations will not be available" (기존과 같음) |
| 로그 | `build/impl/baseline-build.txt` (저장소에 넣지 않는 build 폴더) |

빌드 전에 사용자에게 설치본(`--auto`로 실행 중)을 종료해 달라고 요청했고, 종료를 확인한 뒤 화면 테스트를 실행했다.

### 지시서 1절 사실 확인

| 항목 | 지시서 | 실제 | 판정 |
|---|---|---|---|
| `src/main_window.cpp` | 약 64KB, `MainWindow` 한 클래스 | 64,419바이트, 1,132줄 (설계 문서는 1,069줄) | 거의 일치 |
| `TranslationService` | 50조각, 전송 계층 주입, 401/403/429/456 | 같음 | 일치 |
| `tests/local_http_fixture.h` | 있음 | 있음 (고정 응답 큐 방식, 요청 내용에 따른 응답 없음) | 일치 |
| `st_session` 원문 32KB | 있음 | 32,768바이트, **마지막 한 줄만** 캐시 | 일치 |
| 바뀜 감지 | 0.4초, 2회 안정, 3초 강제 | 같음 | 일치 |
| CTest 7개 그룹 | 이름 7개 | 같음 | 일치 |
| 설정·자격 증명 위치 | `HKCU\Software\PersonalTools\SubtitleTranslator`, `PersonalTools/SubtitleTranslator/DeepL` | 같음 | 일치 |
| `scripts/prepare-paddle-models.ps1` | 모델 내려받기 방식의 예로 참조 | **저장소에 없음** (README·빌드 스크립트도 참조). 모델은 도구 폴더 `paddle\`에 이미 있음 | **차이 1** |
| R-19 단독 `\|`→`I` | 남은 결함 | **기준 커밋에서 이미 수정됨**: 전역 치환 제거, Tesseract 영어 대명사 문맥만 보정 (`integration_checks::pipeCleanupPreservesUiAndPaddleResults`) | **차이 2** |
| 테스트 설정 격리 | 별도 설정 경로 사용 요구 | `window_checks`는 이미 조직 이름 `SubtitleTranslatorChecks` 사용. 자격 증명은 격리 없음(테스트가 저장 경로를 부르지 않음) | **차이 3** (주의 사항) |
| 단일 인스턴스 | 없음 | 없음. 테스트가 한 프로세스에서 `MainWindow`를 여러 번 만든다 → 단일 인스턴스는 `main()`에 둬야 함 | **차이 4** (설계 제약) |
| `GameWindow` 모듈 | 언급 없음 | 빌드·테스트되지만 앱에서 안 씀(QA I-08). 영상 창 자동 맞춤에 재사용 가능 | **차이 5** (재사용 기회) |

### 핵심 기술 확인 — 캡처 제외

`SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`를 건 창이 지금 캡처 방식(GDI `BitBlt`, Qt `QScreen::grabWindow`가 쓰는 방식)에서 빠지는지 먼저 시험했다.

| 조건 | 같은 점의 픽셀 |
|---|---|
| 제외 전 (빨간 창이 파란 창 위) | 빨강 (`SRCCOPY`, `SRCCOPY\|CAPTUREBLT` 둘 다) |
| 제외 후 | 파랑 (둘 다) — 빨간 창이 캡처에서 빠짐 |

Windows 11 26200, PowerShell P/Invoke 실험(`scratchpad/wda-probe.ps1`). **캡처 방식 교체(WGC·DXGI)는 필요 없다.** 앱 자체 창(Qt 반투명 창)으로 같은 결과가 나오는지는 단계 0에서 Qt 테스트로 다시 확인한다.

### 키 형식 문자열 검색

추적·미추적 파일 전체에서 DeepL 키 형식(UUID, `:fx`) 문자열 **0건**.
