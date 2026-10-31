# 문서 지도

처음이거나 오랜만이면 루트의 [PROJECT.md](../PROJECT.md)부터 읽는다. 이 파일은 "무엇이 어디에 있는지"만 안내한다.

## 폴더 규칙

| 위치 | 담는 것 | 이름 규칙 |
|---|---|---|
| `PROJECT.md` (루트) | 현재 상태 · 재개 순서 · 빌드/테스트 명령 · 안전 규칙 | 하나만 둔다 |
| `README.md` (루트) | 사용자용 사용법 | 기능이 바뀌면 같이 고친다 |
| `docs/plan/` | 앞으로 할 일과 그 계획·결과 | `BACKLOG.md`, `DECISIONS.md`, `TEMPLATE.md`, `YYYY-MM-DD-이름.ko.md` |
| `docs/` | 기능 설명·검증 기록 (날짜가 있는 기록은 고치지 않고 새로 추가) | `기능이름.ko.md` |
| `docs/evidence/` | 테스트 출력·측정 JSON·화면 캡처 | `이름-YYYY-MM-DD/` 폴더 (예전 파일은 평면 배치) |
| `tests/qa-2026-09-30/` | 외부 QA 세션 자료와 하네스 | 그대로 보존 |

## 계획 (`docs/plan/`)

| 문서 | 언제 읽나 |
|---|---|
| [BACKLOG.md](plan/BACKLOG.md) | 다음에 무엇을 할지 정할 때. 남은 일은 여기에만 적는다 |
| [DECISIONS.md](plan/DECISIONS.md) | "왜 이렇게 했지?"가 궁금할 때, 기존 결정을 바꾸기 전 |
| [TEMPLATE.md](plan/TEMPLATE.md) | 새 기능 계획서를 만들 때 |
| [2026-11-01-ocr-speed](plan/2026-11-01-ocr-speed.ko.md) | 완료: OCR 인식 속도(스레드·미리 불러오기·캡처 대기), 구현 언어·엔진 탐색 (P-26) |
| [2026-10-25-ocr-false-text](plan/2026-10-25-ocr-false-text.ko.md) | 완료: 특수기호·빈 대사창 오인식 (P-01, P-02), 측정 방법과 기준값 근거 |
| [2026-10-25-cleanup-and-reliability](plan/2026-10-25-cleanup-and-reliability.ko.md) | 완료: 취소 지연·429/456·로컬 HTTP 테스트·모델 간 보정·QA 하네스 등 12건 |
| [2026-10-25-mouse-region-usability](plan/2026-10-25-mouse-region-usability.ko.md) | 완료: 게임 창 따라가기·영역 단축키·읽은 범위 표시 |

## 기능 설명

| 문서 | 내용 |
|---|---|
| [product-design.ko.md](product-design.ko.md) | 멀티플랫폼 설계·지원표·흐름 |
| [mouse-translation.ko.md](mouse-translation.ko.md) | 마우스 주변 번역 (Ctrl+Alt+M/F8) |
| [mouse-hover.ko.md](mouse-hover.ko.md) | 마우스 올려두기 번역 (Ctrl+Alt+H/F9) |
| [../platforms/android/README.md](../platforms/android/README.md) | Android 프로토타입 |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | 서드파티·배포 조건 |

## 검증 기록 (시간순)

| 날짜 | 문서 | 내용 |
|---|---|---|
| 2026-09-29 | [validation.md](validation.md) | 누적 검증 기록: OCR 엔진 교체, 14개 언어, 실제 DeepL 1회, 대사창 전체 선택 (이후 날짜 절도 포함) |
| 2026-09-29 | [multilang-recheck.ko.md](multilang-recheck.ko.md) | 다국어 OCR 재확인·재현 방법 |
| 2026-09-30 | [auto-translation-recovery.ko.md](auto-translation-recovery.ko.md) | 자동 번역 누락 수정, 작업 인계 확인 |
| 2026-09-30 ~ 10-25 | [QA 폴더](../tests/qa-2026-09-30/) | 불일치·위험·계획·실행 결과·[보고서](../tests/qa-2026-09-30/REPORT.md) |
| 2026-10-01 | [qa-fixes-2026-10-01.ko.md](qa-fixes-2026-10-01.ko.md) | 단색 빈 화면·결과 창 가림 수정 |
| 2026-10-25 | [plan/2026-10-25-mouse-region-usability.ko.md](plan/2026-10-25-mouse-region-usability.ko.md) 6절 | 편의 기능 수정 전후 CTest 137 → 143 |
| 2026-10-25 | [plan/2026-10-25-ocr-false-text.ko.md](plan/2026-10-25-ocr-false-text.ko.md) 6절 | 오인식 수정 전후 측정, CTest 154 |
| 2026-10-25 | [plan/2026-10-25-cleanup-and-reliability.ko.md](plan/2026-10-25-cleanup-and-reliability.ko.md) 4절 | 취소 대기 전후, 429/456, 타자기, CTest 171 |
| 2026-11-01 | [plan/2026-11-01-ocr-speed.ko.md](plan/2026-11-01-ocr-speed.ko.md) 6절 | 속도 수정 전후 측정(단축키 → 자막 351 → 83ms), CTest 174 |
