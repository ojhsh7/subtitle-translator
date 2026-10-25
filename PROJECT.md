# subtitle-translator — 프로젝트 현황 (진행 중)

최종 갱신: 2026-10-25 · 브랜치 `ocr-accuracy-foreign-only` · 상태: **개발 진행 중 (QA 5단계 완료, 결함 수정 대기)**

## 무엇을 만드는가

게임 화면의 자막 영역을 캡처해 로컬 OCR로 읽고, 사용자 본인의 DeepL 키로 번역해 원문 바로 아래에 띄우는 개인용 도구. C++17 공통 코어를 Windows(Qt)와 Android(Java + JNI)가 함께 쓴다. 사용법은 [README.md](README.md).

## 플랫폼별 상태

| 플랫폼 | 상태 | 검증 수준 |
|---|---|---|
| Windows | 기능 구현 완료, 결함 수정 중 | 빌드·CTest 7/7, 합성 이미지 CER, 실제 DeepL(CLI) 10회 |
| Android | 프로토타입 (일본어 → 한국어, 이미지 선택·자르기) | 에뮬레이터 계측 테스트 3개. 실기기·실번역 미검증 |
| iOS | 설계만 | — |

## 기능 진행표 (Windows)

| 기능 | 상태 | 문서 |
|---|---|---|
| 영역 선택 + 1회 번역 (Ctrl+Alt+T) | 완료 | README |
| 자동 번역 (변화 감지, Ctrl+Alt+P) | 완료 | [auto-translation-recovery](docs/auto-translation-recovery.ko.md) |
| PaddleOCR PP-OCRv5 + Tesseract 대체, 14개 언어 | 완료 | [multilang-recheck](docs/multilang-recheck.ko.md) |
| 외국어 부분만 번역 (공통 코어 `splitForTranslation`) | 완료 | [validation](docs/validation.md) |
| 마우스 주변 번역 / 마우스 올려두기 번역 | 완료 | [mouse-translation](docs/mouse-translation.ko.md), [mouse-hover](docs/mouse-hover.ko.md) |
| 단색 빈 화면 오인식(D-01), 결과 창 가림(R01) 수정 | 완료 | [qa-fixes-2026-10-01](docs/qa-fixes-2026-10-01.ko.md) |
| 특수기호 오인식(B-04), 테두리 있는 빈 대사창 | **미해결** | [QA 보고서](tests/qa-2026-09-30/REPORT.md) |
| DeepL 429/456 대응(R-04), 취소 지연(R-02) | **미해결** | 〃 |
| 게임 창 따라가기·게임별 영역, 영역 단축키 Ctrl+Alt+R, 선택 화면 키보드 조정, 읽은 범위 표시 | 완료 (2026-10-25, CTest 143/143) | [보강 계획·결과](docs/mouse-region-usability-plan.ko.md) |
| 실제 게임 창 추적, 혼합 DPI·다중 모니터 실기 확인 | 미확인 | 〃 |

## 지금까지의 흐름

| 날짜 | 내용 |
|---|---|
| ~2026-09-29 | Windows·Android 프로토타입, 다국어 OCR, 설치 스크립트 |
| 2026-09-30 | 대사창 전체 선택 인식 실패 → 글자 위치 검출 모델 추가 (장면 CER 38.1% → 7.3%) |
| 2026-09-30 | Cowork QA 1–3단계: 문서 불일치 12건, 위험 19건, 테스트 계획 |
| 2026-10-01 | D-01·R01 수정, 특수기호 측정(B-04), 전체 실행(약 14분 후 중단) |
| 2026-10-25 | 새 PC로 clone, QA 5단계 보고서 작성 |

## 측정 수치 (근거가 있는 것만)

| 항목 | 값 | 조건 |
|---|---|---|
| CER, 한 줄 672장 | 2.29% (언어 선택) / 4.46% (다국어 13종) | 합성 이미지, Windows CPU |
| CER, 대사창 168장 | 7.28% | 합성 게임 장면 |
| 한 줄 OCR | 중앙값 45ms, p95 95ms | 웜, 언어 선택 |
| 단축키 → 화면 자막 | 중앙값 367ms | 키 없음(번역 제외) |
| DeepL 번역 구간 | 중앙값 1.20초 | CLI, 영어 26자 |
| 대사 변경 → 자막 갱신 | 중앙값 1.19초, 놓침 0/20 | 가짜 게임 창 |

장시간 CPU·메모리·누수는 아직 측정값이 없다(REPORT.md 3절 C).

## 다음 할 일

1. B-04·빈 대사창 오인식 수정: 줄 신뢰도 하한, `|`→`I` 후처리 범위 축소, 숫자↔한글 혼동 규칙
2. QA 하네스 보완(판정 결과 파일 저장, 단축키 상태 확인) 후 남은 케이스 재실행: C-03·04·07·08, D-01~04, A-04, 1시간 누수, 회귀
3. R-04 429/456 대응, R-17 진단 로그
4. 올려두기 이동 직후 재요청 지연(R-02) 측정, 실제 게임에서 창 추적 확인

이 PC 빌드 환경(2026-10-25 구성): VS 2022 Build Tools, CMake 4.4.4, Qt 6.8.3, ONNX Runtime 1.30.0, Tesseract 5.4, 도구 폴더 `D:\SubtitleTranslatorToolchain`. 빌드: `scripts\build-windows.ps1 -ToolchainRoot D:\SubtitleTranslatorToolchain -CMakeExe 'C:\Program Files\CMake\bin\cmake.exe'`

## 파생 프로젝트

이 저장소의 OCR 벤치마크(합성 픽스처 생성 → 네이티브 실행 → CER 채점)와 Android ML Kit 파이프라인 경험은 모바일 프로그래밍 팀 프로젝트 **종이한장(OnePaper)**의 정확도 측정 도구로 옮겨 쓸 예정이다. 계획은 팀 프로젝트 쪽 개발 계획 제안서에 따로 정리한다.
