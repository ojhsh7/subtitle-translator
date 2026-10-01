# QA 완료 후 검증·설치 결과 — 2026-10-01

## 간단한 사전 설명

Cowork 최종 REPORT.md(12:50 갱신)를 읽고 보류했던 Windows 창·단축키 검증을 완료했다. 새 결함 D-06(자동 시작 최소화 실패)을 재현·수정한 뒤 전체 CTest 7개 그룹을 통과하고 사용자 설치 폴더를 업데이트했다.

**설치 및 실행 검증 완료.** 전체 OCR 결함 해결이나 실제 게임 품질 보장을 의미하지 않는다. 이전 [개발 보고서](qa-hardening-2026-10-01.ko.md)의 “UI 검사·설치 대기” 상태는 이 문서로 갱신한다.

## 자세한 설명

### 수행 결과

| 항목 | 결과 |
|---|---|
| Windows Release 빌드·패키징 | 성공 |
| 최종 CTest | 7/7 그룹, 48.12초 |
| QtTest 집계 | 167개 완료, 예상하지 못한 실패·건너뜀 0 |
| 알려진 OCR 정확도 실패 | 위 집계 안에 XFAIL 3개 포함. 단독 I/O 오독이며 해결된 것으로 세지 않음 |
| Windows UI 검증 | windows_flow 34개, game_windows 7개 |
| 설치된 앱 스모크 | 종료 코드 0, 화면 이미지 생성 및 시각 확인 |
| 설치된 CLI 일본어 OCR | 今日はいい天気です。 정확히 인식, PaddleOCR |
| 설치된 CLI 빈 액자 | blank-frame, 빈 원문, 외부 전송 없음 |
| 설치 파일 동일성 | 앱과 CLI 모두 dist/windows 산출물 SHA-256과 일치 |
| 이번 외부 DeepL 호출 | 0회 |

HTTP 403/429/456 각각 자동 번역 및 마우스 올려두기 모드에서 중지되는 6개 UI 검사가 통과했다. 실제 loopback TCP 응답을 사용했고, 실패 상태 유지·중지 안내·중복 요청 없음도 검사했다. 단색/테두리/액자 입력에서 이전 자막 숨김 및 번역 요청 없음도 통과했다.

실제 Windows 화면의 합성 일본어 문장을 캡처하는 단축키 메시지 처리, 마우스 주변 캡처, 마우스 머무름, 자동 변경 감지, 결과 창 배치, 취소와 재시도도 기존 windows_flow로 실행했다. 단축키 등록 및 WM_HOTKEY 처리 검증이며, 모든 항목을 사용자의 물리 키보드로 수동 조작했다는 의미는 아니다. 테스트 표시 문자열과 모의 번역은 실제 DeepL 성공으로 보고하지 않는다.

### D-06 최소화 결함

먼저 기존 구현을 그대로 실행하는 회귀 검사를 추가했다. Qt의 isMinimized()는 참인데 같은 창 HWND의 Windows IsIconic()은 거짓이었다. 실제 창이 화면에 남는 실패를 확인했다.

Windows 전용 MainWindow의 minimizeForCapture()에서 Qt 최소화 요청 후 실제 HWND 상태를 확인하고, 최소화되지 않았을 때만 ShowWindow(SW_MINIMIZE)로 맞춘다. 자동 번역과 마우스 머무름 모드가 이 경로를 쓴다. 나중에 사용자가 복원한 창을 다시 최소화하는 지연 타이머는 추가하지 않았다.

Microsoft 문서에 따르면 실행자가 STARTUPINFO를 지정한 경우 첫 ShowWindow의 표시 상태 인자가 무시될 수 있다. 이는 이번 Qt/네이티브 상태 불일치와 부합하는 설명이며, 실행자의 모든 창 메시지를 추적해 원인을 확정한 것은 아니다. [공식 ShowWindow 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)

수정 전 실패 로그와 수정 후 성공 로그를 보존했다. 테스트는 --auto 생성자와 main.cpp의 최소화 호출 순서를 재현한다. 사용자 키가 저장된 설치본을 --auto로 켜 실제 게임을 자동 번역하는 검사는 이번에 수행하지 않았다.

### Cowork 최종 결과와의 관계

- Cowork가 검사한 03:40 스냅샷과 이번 새 빌드는 다르다. 원본 REPORT.md는 수정하지 않았다.
- Cowork의 기존 후반부 실패 원인은 보고서에서 중복 프로세스의 단축키 점유 가능성으로 설명했다. 별도로 스크립트의 $R/$r 변수 충돌도 재현 가능하다. 어느 한쪽만 모든 실패의 확정 원인이라고 단정하지 않는다.
- 최신 suite-app.ps1 갱신에서 이전 변수 수정이 사라져 있었다. 최신 케이스 수정은 유지하면서 캡처 영역 참조 22곳을 $captureRegion으로 다시 바꿨다. PowerShell 구문 검사 통과.
- Cowork의 새로운 qa-common.ps1 변경은 보존했다. 이 도구의 전체 실행/강제 프로세스 정리 경로를 이번에 다시 실행하지 않았다.
- 이번에는 Qt 회귀 테스트로 수정 동작을 검증했다. Cowork 전체 suite의 장시간 시나리오를 모두 재실행한 것은 아니다.
- Cowork 1시간 로그는 12:14 종료했지만 35분 이후 큰 메모리/핸들 변화 및 OCR 중단 가능성이 기록돼 있다. 이를 “방해 없는 1시간 연속 번역 무누수 확정”으로 인용하지 않는다.

### 설치

- 폴더: C:/Users/User/AppData/Local/Programs/SubtitleTranslator
- 바로가기: 사용자 바탕화면의 Subtitle Translator.lnk, 기존 --auto 동작 유지
- 이전 앱/CLI 파일은 build/qa-hardening/installed-before-update에 로컬 백업
- API 키·사용자 번역 동의·선택 영역을 변경하지 않았다.
- 앱 SHA-256: 24942EBC50B04CB4330F362A3DC98018F6BCB17EDC23E04301D9184CA2519B29
- CLI SHA-256: A0430235E432C9D90875AE0C34A897925E9ABF51B0E35A13C3A1FC3FD1956FD1

컴퓨터 사용 도구는 초기화 및 재설정 후 재시도 모두 “windows sandbox failed: helper_unknown_error: setup refresh had errors”로 실패했다. 앱 자체 --smoke-test 실행과 Qt 창 검증을 사용했으며, 설치 스모크 이미지의 UI 구성·언어·단축키 안내를 확인했다. 스모크는 정상 종료했고 실행 프로세스를 남기지 않았다.

### 남은 제한

- 기호 번역 후보 25→11/72 개선과 합성 문장 840개 비교 결과는 이전 보고서의 동일 OCR 구현 측정이다. 이번 최소화 변경 뒤 이를 새로 반복 측정한 수치로 표시하지 않는다.
- 잡음 배경 오인식, 고신뢰도 기호 오독, Lv.10 오독, 단독 I/O 오독은 남아 있다.
- R-02: 취소 뒤 실행 중인 Paddle 추론이 즉시 중단되지 않는 문제는 미해결.
- 사용자가 실제로 문장을 놓치는 게임 화면은 아직 제공되지 않아 해결률 미측정.
- 다른 PC, 다중 모니터·혼합 DPI, 모바일 기기는 이번 검증 범위 밖이다.
- 배포 도구의 translations catalog, dxcompiler/dxil, VCINSTALLDIR 경고가 남는다.
- 실제 API 번역 품질 및 한 시간 연속 검증을 이번 새 빌드로 다시 수행하지 않았다.

[원본 로그·설치 해시·스모크 이미지](evidence/qa-hardening-completion-2026-10-01)
