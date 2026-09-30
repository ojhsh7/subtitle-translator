# 1단계: 문서·가정 ↔ 실제 코드 불일치

기준: HEAD `0b8ed75` (브랜치 `ocr-accuracy-foreign-only`), merge-base `bfbcfaf` (main). 커밋되지 않은 코드 변경 없음(텍스트 파일 기준, 바이너리·build·dist 제외).

## 커밋 요약

| 커밋 | 메시지 | 실제 변경 |
|---|---|---|
| 83be211 | Save … prototypes | 최초 커밋 (main) |
| 1d437b4 | 업데이트 | DeepL 키 확인(`/v2/usage`), `TranslationService::translate` 재작성(요청 본문·오류 메시지), 설정 창에 키 확인·Free/Pro 선택 추가, 실키 1회·잘못된 키 증거 파일 (main) |
| bfbcfaf | 인식 기능 개선 다른언어 감지기능 추가 | (main 끝, merge-base) |
| 335dee3 | Improve small/outlined text OCR… | `core/src/text.cpp` 외국어 부분만 번역(+141), Tesseract 마스크 재시도(`ocr_service.cpp` +164), CLI 경로 |
| **e87e9c4** | **11** | PaddleOCR 도입(`paddle_ocr.cpp/.h` +332), OCR 언어 7→16 옵션·번역 언어 6종(`languages.h`), `ocr_benchmark`, `prepare-paddle-models.ps1`, `summarize-ocr-benchmark.py`, 빌드 스크립트 모델 복사 |
| d5f87e1 | 마우스 범위 지정 기능 업데이트 | 마우스 주변 번역(Ctrl+Alt+M/F8), `game_window.cpp` 신규(+166), 글자 위치 검출 모델(`paddle_ocr.cpp` +277), 장면 픽스처 생성기 |
| **1b69b36** | **11** | 마우스 올려두기(Ctrl+Alt+H/F9, `main_window.cpp` +128), 변화 감지 재작성(비교 이미지 960×320, 연속 2회 안정, OCR 실패 재시도 2회), `make-enja-fixtures.py`(+1194, 미완료 실험) |
| **0b8ed75** | **11** | 코드 변경 없음. hover 증거 txt 6개와 docs 2개만 |

## 불일치 목록

| # | 문서·가정의 주장 | 실제 | 근거 파일:줄 | 태그 |
|---|---|---|---|---|
| I-01 | QA 프롬프트: 메시지 "11" 커밋 2개, 브랜치 커밋 8개 | "11" 커밋 **3개**(e87e9c4, 1b69b36, 0b8ed75). 전체 이력 8개 중 브랜치 고유 커밋 5개 | `.git/logs/HEAD` | [측정함] |
| I-02 | `docs/mouse-translation.ko.md:40`, `docs/auto-translation-recovery.ko.md:39`: "번역 프로토콜 검증은 로컬 (테스트) 서버" | `tests/translation_tests.cpp`에 네트워크 코드 0건. 정적 응답 해석(`decodeResponses`) 픽스처만 검사. 실제 HTTP 경로(타임아웃·중단·429 수신)는 테스트되지 않음 | `tests/translation_tests.cpp:11-114` | [측정함] |
| I-03 | QA 프롬프트: 키는 환경변수 `DEEPL_API_KEY` | 앱은 환경변수를 읽지 않고 Windows 자격 증명(`PersonalTools/SubtitleTranslator/DeepL`)만 읽음. CLI는 `SUBTITLE_TRANSLATOR_API_KEY` → 없으면 **자격 증명 저장소로 대체** | `src/credential_store.cpp:6`, `src/pipeline_cli.cpp:112-113` | [코드 분석 추정] |
| I-04 | QA 프롬프트 가정: 엔드포인트를 모의 서버로 바꿀 수 있음 | 설정·환경변수·인자 없음. `api-free.deepl.com`/`api.deepl.com` 하드코딩 | `src/translation_service.cpp:17` | [코드 분석 추정] |
| I-05 | README:118 "앱이 키 형식에 맞춰 Free/Pro를 자동으로 선택" | 설정 창에서 키를 **입력(textEdited)할 때만** 자동 전환. 저장된 값은 `translation/freeApi` 그대로. CLI는 접미사를 보지 않고 `--pro` 인자만 따름 | `src/main_window.cpp:516-523`, `src/pipeline_cli.cpp:126` | [코드 분석 추정] |
| I-06 | README:53 "메모리 최대 약 380MB" (앱 사용 조건처럼 서술) | 벤치마크 호스트(`ocr_benchmark`)의 최대 작업 집합. 앱 프로세스 실측값 아님 | `docs/multilang-recheck.ko.md:33` | [코드 분석 추정] |
| I-07 | README:60 CER 672장, README:44 장면 168장 | 저장소 `tests/fixtures`에는 PNG 25장만 있음. 672/168장은 스크립트로 생성해 `build/`(gitignore)에 있음. 재측정하려면 생성 스크립트·폰트 의존 | `scripts/make-multilang-fixtures.py:58`, `.gitignore:1` | [측정함] |
| I-08 | `GameWindow` 모듈(d5f87e1)은 게임 창 추적용 | 빌드·테스트는 되지만 `MainWindow`에서 한 번도 호출하지 않음(문서 auto-translation-recovery:15도 인정). 호출되지 않는 코드 | `CMakeLists.txt:26`, `src/main_window.*` 참조 0건 | [측정함] |
| I-09 | `make-enja-fixtures.py` (+1194줄) | 사용량 제한으로 중단된 영어/일본어 실험 준비 파일. 어떤 스크립트·문서도 실행하지 않음 | `docs/auto-translation-recovery.ko.md:12,17` | [측정함] |
| I-10 | README 다국어 앱 | 설치 바로가기 설명이 "Japanese to Korean …" | `scripts/install-windows.ps1:20` | [측정함] |
| I-11 | README:34 "`scripts\launch-windows.ps1`은 임시 폴더에서 한 번 실행", README:134 "`%TEMP%`에 두지 마세요" | launch 스크립트가 `%TEMP%\SubtitleTranslatorRun`에 복사 후 실행. 이때 저장되는 `ocr/executable` 경로가 %TEMP%를 가리킬 수 있음(없어지면 코드가 번들로 되돌림) | `scripts/launch-windows.ps1:1`, `src/main_window.cpp:343` | [코드 분석 추정] |
| I-12 | README:81 "원문을 가리지 않는 공간이 없으면 별도 결과 창" | 결과 창이 항상 위(StaysOnTop)이고 위치 지정이 없어, 영역과 겹치면 자동 번역이 "앱 창이 자막 영역을 가리고 있어" 상태로 계속 건너뜀(risks R-01) | `src/main_window.cpp:835-837, 967-970, 984-988` | [코드 분석 추정] |

일치 확인(참고): 0.4초 폴링·2회 안정(README:79 ↔ `change_detector.h:8`, `main_window.cpp:133`), 자막 글자 16–36px·4줄(README:81 ↔ `subtitle_overlay.cpp:49,15`), 대체 키 Ctrl+Alt+F8/F9(README:89,101 ↔ `main_window.cpp:367,380`), 번역 30초 제한, 설정 레지스트리 `HKCU\Software\PersonalTools\SubtitleTranslator`.

## 4단계 방법을 정하는 확인 결과

1. **타이밍 로그**: 앱(`SubtitleTranslator.exe`)에는 로그·디버그 출력이 없음(qDebug/OutputDebugString 0건). CLI(`subtitle_cli`)는 JSON에 `ocrMs`, `translationMs`, `totalMs`, 메모리 피크를 남김. → 앱 지연은 오버레이 폴링(±50ms), 구간별은 CLI·테스트 바이너리로 측정.
2. **엔드포인트 변경**: 불가(I-04). 모의 서버 케이스는 [코드 분석 추정] 또는 worktree 계측 빌드에서만 가능.
3. **키 읽는 위치**: 앱=자격 증명 저장소(설정 `translation/rememberKey`=true일 때만 로드), CLI=환경변수→자격 증명 저장소. `DEEPL_API_KEY`는 어디서도 읽지 않음.
4. **설정 위치**: `HKCU\Software\PersonalTools\SubtitleTranslator`(조직·앱 이름 하드코딩, `src/main.cpp:9-10`). 테스트 전용 설정 경로 옵션 없음. 단, `window_checks`는 조직 이름을 바꿔 격리함(`tests/window_checks.cpp:27-28`).
5. **오버레이 방식**: 상주 창 1개(`SubtitleOverlay`)를 show/hide하며 내용만 바꿈 → 표시 시각은 `IsWindowVisible` 전환 + 창 영역 픽셀 변화로 잡아야 함.
