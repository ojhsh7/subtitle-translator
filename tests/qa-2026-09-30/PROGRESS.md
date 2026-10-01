# QA 진행 상황 (subtitle-translator, 2026-09-30)

## 기준점

- HEAD: `0b8ed7516758ac4daa2c5acee22956c4fc05d198` (ocr-accuracy-foreign-only), merge-base `bfbcfaf` (main)
- 커밋되지 않은 코드 변경: 없음(텍스트 파일 비교). diff 해시: 빈 diff
- 새 세션은 HEAD가 위 값과 같은지 먼저 확인할 것

## 적용한 자리표시자 값 (사용자 미지정 → 예시 값 사용)

| 자리 | 값 | 비고 |
|---|---|---|
| DATE | 2026-09-30 | 작업일 |
| LOG_PATHS | **미지정** | 대화 로그 대조는 저장소 docs 인계 기록으로 대신함. 경로를 주면 추가 대조 |
| GAME_SHOTS | 없음 | |
| DEEPL_BUDGET / CHAR_BUDGET | 30회 / 5,000자 | 예시 값 |
| CPU_TARGET / LEAK_MB | 3% / 20MB | 예시 값 |
| BASE_BRANCH | main | |

## 측정 대상 코드 (중요)

- 지금까지의 모든 Windows 측정(빌드 00:10, 실행 00:27·01:03, B-04 01:40)은 **`0b8ed75` 그대로의 코드**를 `D:\qa-build\2026-09-30`에 빌드한 것이다.
- 그 뒤 00:49–00:58에 작업 트리에서 `src/main_window.cpp/.h`, `src/ocr_service.cpp/.h`가 수정됨(커밋 안 됨): 단색 이미지 OCR 생략(D-01 대응), 결과 창이 자막 영역을 가리면 옮기거나 자동 번역 중지(R-01 대응). **이 수정본은 아직 검증하지 않았다.**
- 정적 검토: 단색 판정은 모든 픽셀이 완전히 같을 때만 참이다. 테두리·그라데이션·잡음이 있는 빈 대사창(A-05 frame 등), 그리고 B-04 특수기호 오인식은 이 수정으로 해결되지 않는다 [코드 분석 추정].
- B-04 수치 표기: 기호만 있는 입력 72가지(이미지 36 × 원문 언어 2) × 3회 = 216회. 번역 조각 발생 25가지 × 3회 = 75회(35%). 결과가 결정적이라 두 표기는 같은 값이다.

## 실행 환경 제약

- 사용자 PC의 로컬 셸(device_bash)이 시작되지 않음 → 정적 분석은 클라우드 복사본으로. Windows 실행은 탐색기에서 .cmd를 더블클릭하는 방식으로 진행(run-qa.cmd, rerun-verify.cmd, probe-blank.cmd).
- QA 빌드: `D:\qa-build\2026-09-30` (out=빌드, dist=배포). 1차 실행은 windeployqt 단계에서 중단 → deploy-qa.ps1로 재배포. 2차·3차(수정된 verify-runtime.ps1, 10회 반복) 측정값이 유효하며 서로 일치.
- 앱은 `DEEPL_API_KEY`를 읽지 않음(inconsistencies I-03). 실제 과금 위험 경로: 자격 증명 저장소의 저장 키(앱: rememberKey=true일 때, CLI `--translate`: 항상 대체 사용).

## 단계

| 단계 | 상태 | 산출물 |
|---|---|---|
| 1 현황 파악 | 완료 | inconsistencies.md, results/git_log.txt |
| 2 빌드·정적 분석 | 완료 (빌드 성공·경고 0, CTest 7/7 통과) | risks.md, existing_tests.csv, results/build.json, results/ctest-junit.xml |
| 3 테스트 계획 | 확인 완료 (2026-10-01): 범위 **전체**, 실제 DeepL **허용**(계획 T 목록), 1시간 테스트 **이번에 같이**, B-04 즉시 실행 | plan.md, risks.md |
| 4 실행 | 수정본 재검증 완료(windows-verification.md 11절). B-04 완료(특수기호 오인식 재현, windows-verification.md 10절). 일부 완료: 앱 스모크, CLI 종료 코드, CLI OCR 반복, 672/168장 CER 재측정, 빈 화면 OCR(D-01). 앱 화면 기능·지연·CPU·누수는 plan 확인 후 | windows-verification.md, results/*.csv |
| 5 보고서 | 미착수 | |

## DeepL 실제 사용량

- 호출 0회 / 0자 (예산 30회 / 5,000자)

## 사용자 설정·자격 증명

- 백업: settings-backup/before.reg, after.reg. QA 실행 전후 동일(해시 비교). 자격 증명은 읽거나 바꾸지 않음. 결과·빌드 로그의 키 형식 문자열 0건.

## 백그라운드 작업

- 없음

## 다음 할 일

1. 수정본(`D:\qa-build\2026-10-01`) 재검증 결과: windows-verification.md 11절. 단색 빈 화면은 해결, 거의 빈 화면·특수기호(B-04)는 그대로.
2. 하네스 버그 6건 수정 완료, A-01·A-02·A-05(흰 화면) 동작 확인. A-05 패널·액자는 사용자 창이 영역을 가려 재실행 필요.
3. 전체 실행: `run-full.cmd` (수정본 대상, 결과 `results-full\`, 약 2시간 40분, DeepL 실사용 포함). **PC를 쓰지 않고 다른 창을 모두 최소화한 상태에서** 실행해야 유효.
4. 실행 후 results-full\*, deepl_calls.csv 분석 → REPORT.md(5단계)
