# VS Code Claude 인계 메모 (QA 2026-09-30)

이 폴더는 Cowork 세션에서 진행한 subtitle-translator QA의 중간 결과다. 작업 규칙은 사용자가 준 "QA·성능 검증 프롬프트 v3"를 따른다(원본 소스·문서 수정 금지, 새 파일은 이 폴더에만, commit/push/checkout 금지, 실제 DeepL 호출은 확인 후).

## 먼저 읽을 파일

1. `PROGRESS.md` — 기준 커밋(HEAD `0b8ed75`), 적용한 자리표시자 값, 단계별 상태, 다음 할 일
2. `inconsistencies.md` — 1단계: 문서·가정과 실제 코드가 다른 부분 12건
3. `risks.md` — 2단계 정적 분석 위험 17건(R-01~R-17)
4. `build-qa.ps1` — 저장소 밖(`D:\qa-build\2026-09-30`)에 깨끗하게 빌드하고 기존 CTest를 돌리는 스크립트
5. `windows-verification.md` — Windows 빌드·테스트·실행 검증 결과(신규 결함 D-01 포함)
6. `results/` — 원자료(CSV·JSON·로그)

## 반드시 알아야 할 안전 사항

- 앱은 `DEEPL_API_KEY`를 읽지 않는다. 키는 Windows 자격 증명 저장소(`PersonalTools/SubtitleTranslator/DeepL`)에 있다.
- `subtitle_cli --translate`는 `SUBTITLE_TRANSLATOR_API_KEY`가 비어 있으면 **저장된 실제 키로 호출**한다(src/pipeline_cli.cpp:112-113). 확인 없이 `--translate`를 쓰지 말 것.
- 설정 창에서 "키 저장"을 끄고 저장하면 저장된 키가 삭제된다(src/main_window.cpp:574-580).
- DeepL 주소가 하드코딩되어 있어 모의 서버로 바꿀 수 없다(src/translation_service.cpp:17).
- 설정은 `HKCU\Software\PersonalTools\SubtitleTranslator`. 앱을 실행하는 테스트 전에 `reg export`로 백업.
- `window_checks` 테스트는 전역 단축키를 등록하고 실제 마우스 커서를 움직인다. 실행 중인 앱을 닫고 돌릴 것.

## 다음 할 일 (여기서 이어서)

빌드·기존 테스트·CLI·CER 재측정은 끝났다(windows-verification.md). 남은 것:

1. 프롬프트 7절 형식으로 `plan.md` 작성, risks.md의 "검증 케이스" 칸 채우기
2. **plan.md를 사용자에게 보여 주고 확인받기 전에는 앱 화면 테스트·실제 DeepL 호출·1시간 테스트를 하지 말 것**
3. 앱을 실행하는 테스트 전후로 `reg export`/복원(settings-backup/before.reg 있음)
4. 사용자에게 아직 받지 못한 값: Claude·GPT 대화 로그 경로(LOG_PATHS)
