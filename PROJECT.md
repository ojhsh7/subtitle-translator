# subtitle-translator — 프로젝트 현황

갱신 2026-11-01 · 브랜치 `ocr-accuracy-foreign-only` · 상태: **개발 진행 중** · 지금 하는 일: **없음 (다음 후보 P-22, P-12, P-13)** · 기획서: [docs/product-design.ko.md](docs/product-design.ko.md)

게임 화면의 자막 영역을 캡처해 로컬 OCR로 읽고, 사용자 본인의 DeepL 키로 번역해 원문 바로 아래에 띄우는 개인용 도구. C++17 공통 코어를 Windows(Qt)와 Android(Java + JNI)가 함께 쓴다. 사용법은 [README.md](README.md), 문서 위치는 [docs/README.md](docs/README.md).

## 재개 순서

1. 이 파일의 **지금 하는 일**을 읽는다.
2. [백로그](docs/plan/BACKLOG.md)의 `진행 중` 항목과 그 계획서를 연다. 계획서 5절 "일정"에서 끝난 단계 다음부터 이어 간다.
3. 아래 **빌드·테스트**로 지금 코드가 통과하는지 먼저 확인한다(기대: 193 통과, 건너뜀 0).
4. 일을 끝내면 계획서 6절 결과 → 백로그 상태 → 이 파일의 "지금 하는 일"과 "흐름" 순서로 갱신하고 커밋한다.
5. 기존 결정을 바꿀 때는 [결정 기록](docs/plan/DECISIONS.md)에 "변경" 줄을 추가한다.

## 지금 하는 일

진행 중인 항목 없음. 방금 끝난 일: 우선순위 점수 순 개발 ([목록·결과](docs/plan/2026-11-02-priorities.ko.md): 중복 실행 막기, 시작 메뉴·제거, 키 오류 알림, 앞 대사 문맥, 자막 숨기기, 어두운 모드, 문제 보고, P-24·P-32 해결, CTest 193). 그 전: P-33 MVP 사용자 기준 보강 ([결과](docs/plan/2026-11-02-mvp-gaps.ko.md): 대사 기록, 고쳐 쓰기, 게임별 언어, 사용량 표시, 다음 할 일 안내, CTest 187). 그 전: P-31 번역 표시 지연 ([결과](docs/plan/2026-11-02-translation-latency.ko.md): 대사 → 원문 1,118 → 480ms, DeepL 끊긴 연결 1,058 → 252ms, 최근 200줄 기억, CTest 184). 그 전: P-30 디자인 다듬기 ([결과](docs/plan/2026-11-01-design-pass.ko.md): 시스템 색·Fluent 아이콘·스위치, 메인 창 자막 무대, 문구 해요체, CTest 180). 그 전: P-29 앱 화면·설정 개편 ([결과](docs/plan/2026-11-01-app-ui.ko.md): 트레이 상주, 조작판 메인 창, 설정 7쪽, 단축키 변경, 자막 크기·배경, CTest 180). 그 전: P-23 취소 즉시 반영(대기 264ms → 0), P-27 Tesseract 시도 동시 실행(베트남어 524 → 305ms) ([결과](docs/plan/2026-11-01-cancel-and-tesseract.ko.md), CTest 176). 그 전: P-26 OCR 인식 속도 ([결과](docs/plan/2026-11-01-ocr-speed.ko.md): 단축키 → 자막 351 → 83ms, 기본 설정 대사창 OCR 288 → 105ms, 인식 결과 차이 0, CTest 174). 구현 언어는 C++, 엔진은 PaddleOCR 유지로 결정(측정 근거는 같은 계획서 2절).

다음 후보 ([백로그](docs/plan/BACKLOG.md)):
- P-22 배포 실행 파일 코드 서명 (Smart App Control이 켜진 PC에서 실행되도록)
- P-12 QA 남은 케이스 재실행 + 미리 불러오기 이후 시작 메모리 (PC를 2–3시간 쓰지 않는 시간 필요)
- P-13 실제 게임에서 확인 (OCR 스레드·Tesseract 동시 실행이 게임에 주는 영향 포함, 게임 화면 필요)

새 항목을 시작할 때는 [양식](docs/plan/TEMPLATE.md)으로 계획서를 만들고 백로그에서 `진행 중`으로 옮긴다.

## 빌드·테스트

이 PC 환경(2026-10-25 구성): VS 2022 Build Tools(MSVC 14.44), CMake 4.4.4, Qt 6.8.3 msvc2022_64, ONNX Runtime 1.30.0, PaddleOCR 모델 8개, Tesseract 5.4 + tessdata_fast. 도구 폴더 `D:\SubtitleTranslatorToolchain`.

저장소 위치: `C:\dev\subtitle-translator` (2026-11-01 OneDrive 문서 폴더에서 이동). **OneDrive 등 동기화 폴더에 두지 않는다**: 빌드할 때마다 배포 폴더(약 230MB)가 업로드되고 `.git` 내부 파일이 동기화 충돌을 일으킬 수 있으며, 경로에 한글이 들어가면 빌드 도구가 예외 설정을 요구한다.

```powershell
# 전체 빌드 + CTest 7그룹 + 배포(dist\windows)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 `
  -ToolchainRoot D:\SubtitleTranslatorToolchain -CMakeExe 'C:\Program Files\CMake\bin\cmake.exe'

# UI 테스트 하나만 다시 돌리기 (소스를 고쳤으면 windows-source에 복사 후 대상만 빌드)
$root='D:\SubtitleTranslatorToolchain'; $qt="$root\Qt\6.8.3\msvc2022_64"
Copy-Item tests\window_checks.cpp "$root\windows-source\tests\" -Force
& 'C:\Program Files\CMake\bin\cmake.exe' --build "$root\windows-build" --config Release --target window_checks
$env:PATH="$qt\bin;$env:PATH"; $env:QT_PLUGIN_PATH="$qt\plugins"; $env:SUBTITLE_TEST_PADDLE="$root\paddle"
$env:SUBTITLE_TEST_TESSERACT="$root\ocr\tesseract.exe"; $env:SUBTITLE_TEST_TESSDATA="$root\ocr\tessdata"
& "$root\windows-build\Release\window_checks.exe" 테스트이름 -o "$root\windows-build\single.txt,txt"
```

마지막 결과(2026-11-02, 번역 지연 개선 후): CTest 7/7 그룹, **193 통과 · 실패 0 · 건너뜀 0**. 비교 빌드를 만들 때는 P-32(복사한 헤더의 수정 시각) 주의. 그룹별 결과 파일은 `D:\SubtitleTranslatorToolchain\windows-build\*.txt`.

**건너뜀이 0이 아니면 통과로 보지 않는다.** Tesseract가 없으면 UI 테스트 9개가 "엔진 없음"으로 조용히 건너뛰어진다.

OCR 정확도 측정 (네트워크 없음):

```powershell
# 기호·정상 짧은 대사·빈 대사창 측정 → summary.json, 행별 CSV
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\probes\false-text-probe.ps1 `
  -Bench D:\SubtitleTranslatorToolchain\windows-build\Release\ocr_benchmark.exe -Out D:\SubtitleTranslatorToolchain\bench\probe-new
# 672장·168장 픽스처: D:\SubtitleTranslatorToolchain\bench\{ocr,scene}-benchmark (생성 방법은 오인식 계획서 6절)
```

**Smart App Control**: 이 PC에서는 2026-10-25에 **껐다**(앱 본체·UI 테스트까지 빌드마다 무작위로 막혀 검증이 불가능했음, 사용자 결정). 다른 PC에서 "An Application Control policy has blocked this file"이 나오면 같은 원인이다. 서명 없는 배포본은 사용자 PC에서도 막힐 수 있다 → 백로그 P-22(코드 서명).

## 안전 규칙

- `subtitle_cli --translate`는 환경 변수 `SUBTITLE_TRANSLATOR_API_KEY`가 없으면 **자격 증명 저장소의 실제 키**로 DeepL을 호출한다. 확인 없이 실행하지 않는다.
- 앱 설정 창에서 "키 저장"을 끈 채 저장하면 저장된 키가 **삭제**된다.
- UI 테스트(`windows_flow`)는 실제 창을 띄우고 전역 단축키를 등록하며 마우스 커서를 움직인다. 실행 중에는 PC를 쓰지 않고, 설치된 SubtitleTranslator는 먼저 종료한다. 창이 없어도 트레이에서 실행 중일 수 있다(바로가기 실행은 트레이에서 시작): `Get-Process SubtitleTranslator | Stop-Process`.
- 사용자 설정은 `HKCU\Software\PersonalTools\SubtitleTranslator`. 앱을 띄우는 QA 전에는 `reg export`로 백업한다(테스트는 별도 조직 이름을 써서 영향 없음).

## 플랫폼별 상태

| 플랫폼 | 상태 | 검증 수준 |
|---|---|---|
| Windows | 기능 구현 완료, 오인식·속도·화면 개선 | CTest 193/193, 합성 이미지 CER, 실제 DeepL(CLI) 10회 |
| Android | 프로토타입 (일본어 → 한국어, 이미지 선택·자르기) | 에뮬레이터 계측 테스트 3개. 실기기·실번역 미검증 (P-15) |
| iOS | 설계만 | — |

## 기능 진행표 (Windows)

| 기능 | 상태 | 문서 |
|---|---|---|
| 영역 선택 + 1회 번역 (Ctrl+Alt+T) | 완료 | README |
| 자동 번역 (변화 감지, Ctrl+Alt+P) | 완료 | [auto-translation-recovery](docs/auto-translation-recovery.ko.md) |
| PaddleOCR PP-OCRv5 + Tesseract 대체, 14개 언어 | 완료 | [multilang-recheck](docs/multilang-recheck.ko.md) |
| 외국어 부분만 번역 (공통 코어 `splitForTranslation`) | 완료 | [validation](docs/validation.md) |
| 마우스 주변 번역 / 마우스 올려두기 번역 | 완료 | [mouse-translation](docs/mouse-translation.ko.md), [mouse-hover](docs/mouse-hover.ko.md) |
| 단색 빈 화면 오인식, 결과 창 가림 | 완료 (2026-10-01) | [qa-fixes-2026-10-01](docs/qa-fixes-2026-10-01.ko.md) |
| 게임 창 따라가기·게임별 영역, 영역 단축키 Ctrl+Alt+R, 선택 화면 키보드 조정, 읽은 범위 표시 | 완료 (2026-10-25) | [계획·결과](docs/plan/2026-10-25-mouse-region-usability.ko.md) |
| 특수기호·빈 대사창 오인식 줄이기 | 완료 (2026-10-25, CTest 154/154) | [계획·결과](docs/plan/2026-10-25-ocr-false-text.ko.md) |
| 429/456 대응, 취소 지연, 로컬 HTTP 테스트, 모델 간 보정, Tesseract 전 글자 줄 확인 | 완료 (2026-10-25, CTest 171/171) | [계획·결과](docs/plan/2026-10-25-cleanup-and-reliability.ko.md) |
| 인식 속도: 공용 스레드 풀, 모델 미리 불러오기, 캡처 대기 생략 | 완료 (2026-11-01, CTest 174/174) | [계획·결과](docs/plan/2026-11-01-ocr-speed.ko.md) |
| 취소 즉시 반영, Tesseract 시도 동시 실행 | 완료 (2026-11-01, CTest 176/176) | [계획·결과](docs/plan/2026-11-01-cancel-and-tesseract.ko.md) |
| 트레이 상주, 조작판 메인 창, 설정 7쪽, 단축키 변경, 자막 크기·배경 | 완료 (2026-11-01, CTest 180/180) | [계획·결과](docs/plan/2026-11-01-app-ui.ko.md) |
| 디자인 다듬기: 시스템 색·Fluent 아이콘·스위치, 자막 무대, 문구 정리 | 완료 (2026-11-01, CTest 180/180) | [계획·결과](docs/plan/2026-11-01-design-pass.ko.md) |
| 번역 표시 지연: 0.15초 감지·0.3초 대기, DeepL 미리 연결, 최근 200줄 기억 | 완료 (2026-11-02, CTest 184/184) | [계획·결과](docs/plan/2026-11-02-translation-latency.ko.md) |
| 대사 기록, 고쳐 쓰기, 게임별 언어, 사용량 표시, 다음 할 일 안내 | 완료 (2026-11-02, CTest 187/187) | [계획·결과](docs/plan/2026-11-02-mvp-gaps.ko.md) |
| 우선순위 개발: 중복 실행 막기, 시작 메뉴·제거, 키 오류 알림, 앞 대사 문맥, 자막 숨기기, 어두운 모드, 문제 보고 | 완료 (2026-11-02, CTest 193/193) | [목록·결과](docs/plan/2026-11-02-priorities.ko.md) |
| 코드 서명, 실제 게임·다중 모니터·장시간 확인 | 남음 (조건: [우선순위 4절](docs/plan/2026-11-02-priorities.ko.md)) | [백로그](docs/plan/BACKLOG.md) |

## 지금까지의 흐름

| 날짜 | 내용 |
|---|---|
| ~2026-09-29 | Windows·Android 프로토타입, 다국어 OCR, 설치 스크립트 |
| 2026-09-30 | 대사창 전체 선택 인식 실패 → 글자 위치 검출 모델 추가 (장면 CER 38.1% → 7.3%) |
| 2026-09-30 | Cowork QA 1–3단계: 문서 불일치 12건, 위험 19건, 테스트 계획 |
| 2026-10-01 | 단색 빈 화면·결과 창 가림 수정, 특수기호 측정(B-04), QA 전체 실행(약 14분 후 중단) |
| 2026-10-25 | 새 PC 빌드 환경 구성, QA 보고서, 게임 창 따라가기 등 편의 기능(CTest 137 → 143), 계획 문서 구조 정리(`docs/plan/`), 특수기호·빈 대사창 오인식 수정(CTest 154), 백로그 12개 일괄 처리(CTest 171), 기획서 개정 |
| 2026-11-01 | 저장소를 `C:\dev`로 이동, OCR 인식 속도 개선(단축키 → 자막 351 → 83ms), 구현 언어·엔진 탐색(C++·PaddleOCR 유지), 취소 즉시 반영·Tesseract 동시 시도, 앱 화면·설정 개편(트레이·단축키 변경), CTest 180 |
| 2026-11-02 | 번역 표시 지연 개선(대사 → 원문 1.12 → 0.48초, DeepL 미리 연결, 최근 200줄 기억), CTest 184 |
| 2026-11-02 | MVP 보강: 대사 기록·고쳐 쓰기·게임별 언어·사용량·다음 할 일 안내, CTest 187 |
| 2026-11-02 | 우선순위 점수 목록 22개 작성 후 점수 순 개발 12개, 막힌 5개는 조건 기록, CTest 193 |

## 측정 수치 (근거가 있는 것만)

| 항목 | 값 | 조건 |
|---|---|---|
| CER, 한 줄 672장 | 2.29% (언어 선택) / 4.46% (다국어 13종) | 합성 이미지, Windows CPU |
| CER, 대사창 168장 | 7.28% | 합성 게임 장면 |
| 한 줄 OCR | 중앙값 17.5ms (기본 설정 28.9ms), p95 87.3ms | 웜, 언어 선택, 스레드 4 |
| 대사창 OCR | 중앙값 73.6ms (기본 설정 104.9ms), p95 292.6ms | 웜, 합성 장면 168장 |
| 베트남어 대사창 (Tesseract) | 중앙값 305ms, 최대 563ms | 웜, 12장 |
| 취소 후 다음 읽기 대기 | 0ms (수정 전 264ms) | 1400×720 8줄, 다국어 |
| 단축키 → 화면 자막 | 중앙값 83ms, 최대 95ms (수정 전 351ms) | 키 없음(번역 제외), 2026-11-01 |
| DeepL 번역 구간 | 중앙값 1.20초 | CLI, 영어 26자 |
| 대사 변경 → 원문 | 중앙값 0.48초 (수정 전 1.12초), 놓침 0/20 | 가짜 게임 창, 2026-11-02 |
| DeepL 요청 (끊긴 연결 / 미리 연 연결) | 1.06초 / 0.25초 | 앱 코드, 가짜 키(사용량 없음) |

장시간 CPU·메모리·누수는 아직 측정값이 없다(P-12).

## 파생 프로젝트

이 저장소의 OCR 벤치마크(합성 픽스처 생성 → 네이티브 실행 → CER 채점) 경험은 모바일 프로그래밍 팀 프로젝트 **종이한장(OnePaper)**의 번역 기능 측정에 옮겨 썼다(저장소 `C:\dev\onepaper`, 원격 `github.com/ojhsh7/onepaper` 비공개).
