# 검증 결과 — 2026-09-29

## 간단한 사전 설명

Windows 실행 파일과 Android APK를 실제 빌드했습니다. Windows의 캡처·OCR 흐름과 Android 에뮬레이터의 이미지 입력·자르기·OCR 흐름을 검증했습니다. **Windows에서 사용자 본인의 DeepL API Free 키로 실제 일본어 → 한국어 번역 1회를 검증했습니다(샘플 이미지 1장, CLI 경로). 앱 화면 경로의 실제 번역, Android의 실제 번역·실기기, iOS는 미검증입니다.**

## 자세한 설명

### 구현·검증·제한

| 항목 | 구현 | 실제 검증 | 남은 제한 |
|---|---|---|---|
| 공통 C++ 모델·상태·공백 정리·직전 중복 처리 | 완료 | MSVC, Linux g++, Android NDK arm64/x86_64 빌드 및 JNI 계측 테스트 | 여러 번역 제공자 확장 전 오류 매핑 보강 |
| 저장 이미지 → 일본어 OCR | 완료 | Windows Tesseract, Android bundled ML Kit | 일반 게임 폰트·루비·세로쓰기 정확도 미검증 |
| DeepL HTTPS 번역 | 완료 | 프로토콜 테스트, Windows CLI 실서비스 번역 1회 성공(Free), 잘못된 키의 실제 403 안내 확인 | 앱 화면 경로 실번역, 오프라인·한도(456) 실제 발생, 게임 자막 번역 품질, Android 실번역 미검증 |
| Windows 영역·단축키·별도 창 | 완료 | 실제 화면 캡처·WM_HOTKEY 경로·창 이동·재시도·취소 | 실제 게임·독점 전체화면·고DPI·복수 모니터 별도 |
| Windows 설정·키 저장·키 확인 | 완료 | 앱 빌드·설정 코드 검토, 키 없음 경로 확인, 사용량 응답·`:fx` 판별 프로토콜 테스트 | 실제 키의 저장/읽기 왕복, 실제 키 확인 요청, 사용자 수동 설정 전체 동작 미검증 |
| Android 선택·공유·자르기·회전 | 완료 | 공유 URI, 터치 drag, 회전 유지, picker 취소, 권한 오류, 재시도·background 취소 | 실기기·다양한 사진 제공자·pinch 실사용 조합 별도 |
| Android 캡처·overlay | 미구현 | 없음 | 이미지 번역·실서비스 흐름 검증 이후 별도 기술 검증 |
| iOS | 설계만 | 공식 문서 검토 | macOS/Xcode·Simulator·iPhone 환경 필요 |
| 배포 | 개인용 실행 폴더·debug APK | Windows 로컬 실행 폴더 smoke, Android emulator install | 깨끗한 Windows PC·스토어·서명·완전한 재배포 라이선스 검토 별도 |

### Windows 측정

환경: Windows OS build26200 x64, Intel Core i7-13700KF, RAM 약31.83GiB. MSVC19.44, SDK26100, Qt6.8.3, Release, Tesseract5.5.3.20260724. 일본어 fast 모델 사용.

직접 만든 테스트 이미지(1000×180, 흰 바탕/검정 글자)의 정답은 “今日はいい天気です。”입니다. 실제 OCR 3회 모두 같은 문장을 반환했습니다. 이 한 장의 성공을 게임 전체 정확도로 일반화하지 않습니다.

| 항목 | 측정값 |
|---|---|
| jpn.traineddata | 2,471,260 bytes, 약2.36MiB |
| OCR 3회 | 131 / 130 / 128ms, 중앙값130ms |
| OCR 자식 프로세스 peak working set | 51,970,048–51,986,432 bytes, 약49.6MiB |
| Qt CLI 호스트 peak working set | 17,031,168–18,026,496 bytes |
| 이미지 네트워크 전송 | 없음 — stdin으로 로컬 OCR 프로세스에 전달 |
| 번역 API 요청 | 이 측정 때는 키가 없어 보내지 않음. 이후 결과는 아래 '실제 DeepL 번역' 참고 |
| 실제 번역 시간·전송 바이트 | 아래 '실제 DeepL 번역' 참고 |
| 모바일 성능으로 환산 | 하지 않음 |

OCR 시간에는 비동기 PNG 인코딩·프로세스 시작·실제 인식이 포함됩니다. 이미지 디스크 디코딩 이전부터의 시간이 아닙니다. 각 프로세스 메모리 최댓값의 관측 시점은 다를 수 있으므로 합계를 동시 peak로 해석하면 안 됩니다.

모델 SHA-256: 1f5de9236d2e85f5fdf4b3c500f2d4926f8d9449f28f5394472d9e8d83b91b4d.

원자료: [측정 JSON](evidence/windows-pipeline.json), [테스트 이미지](evidence/japanese-fixture.png), [CTest 로그](evidence/windows-tests.txt), [실행 화면](evidence/windows-main.png).

CTest 4개 그룹 통과: translation_protocol, local_checks, windows_flow, shared_core. Windows UI 테스트는 실제 OCR과 실제 캡처를 이용했고, 전역 단축키 등록과 WM_HOTKEY 메시지 처리 경로를 확인했습니다. 물리 키 입력·게임과의 단축키 충돌 전부를 검증한 것은 아닙니다.

Qt 개발 PATH를 제거한 로컬 실행 폴더에서도 --smoke-test가 exit0으로 완료됐습니다. WSL UNC 경로에서 직접 실행하는 방식은 검증이 끝나지 않아 Windows 로컬로 복사하는 launch-windows.ps1을 제공합니다.

### Android 측정 — 실기기 아님

환경: Windows 호스트의 WHPX, Android15 API35 x86_64 에뮬레이터, Pixel5 프로필, debug APK, ML Kit 일본어 모델 포함 버전16.0.1. 실기기 수치의 추정값으로도 사용하지 않습니다.

| 항목 | 측정값 |
|---|---|
| debug APK, arm64+x86_64 포함 | 35,906,759 bytes, 약34.24MiB |
| APK 내부 OCR model assets 합계(비압축) | 3,454,500 bytes, 약3.29MiB |
| 실제 OCR 1회 | 720ms (최종 APK 1회, 1000×180 fixture에서 선택 영역 crop 포함) |
| 인식 후 테스트 프로세스 PSS | 98,753KiB, 약96.44MiB |
| 네트워크 조건 | Wi-Fi·모바일 데이터 비활성화 상태에서 OCR 성공 |
| 실제 번역 API | 키 없음, 미검증 |
| 실기기 지연·메모리·배터리 | 미측정 |

PSS는 peak가 아닌 한 시점의 관측이며 계측 테스트 코드가 포함된 프로세스 값입니다. Windows working set과 직접 비교하지 않습니다. APK 크기는 설치 크기가 아니며 model assets에는 일본어 외 보조 문자·레이아웃 모델이 포함됩니다. SDK 진단 트래픽의 전체 패킷 계측은 하지 않았습니다.

최종 APK의 3개 instrumentation 테스트가 7.905초에 통과했습니다. 최초 fixture 제공자 오류는 테스트 코드에서 수정했으며 최종 통과 결과에 포함되지 않습니다. 권한 거부 테스트는 테스트 provider가 SecurityException을 반환한 경우이고, OS 사진 권한 dialog를 거부한 경우는 아닙니다. 이 앱의 문서 선택 방식에는 전체 사진 권한 dialog가 없습니다.

원자료: [에뮬레이터 측정 JSON](evidence/android-emulator-metrics.json), [APK 자산 크기](evidence/android-artifact-size.json), [계측 테스트 로그](evidence/android-tests.txt), [이미지 선택 화면](evidence/android-image.png), [실제 OCR 화면](evidence/android-ocr.png).

### 다음 검증 완료 기준

- ~~CLI에서 실제 일본어 → 한국어 1회, 잘못된 키 오류 확인~~ (완료, 아래 '실제 DeepL 번역').
- 앱 화면에서 **키 확인** 성공 → 동의 설정 후 실제 번역 1회, 오프라인·한도 오류·사용자 취소 확인.
- Android 실제 기기에서 picker·공유·핀치·회전·background·큰 이미지 및 화면 크기 확인.
- 게임의 실제 자막 샘플 여러 장으로 OCR 품질 측정. 자동 번역을 실제 게임에서 켜고 대사 전환·글자 출력 효과·배경 애니메이션에서 한 대사당 한 번만 번역되는지 확인.
- 위 흐름 검증 이후에만 Android MediaProjection·overlay를 별도 프로젝트 단계로 검증.
- iOS 구현은 macOS/Xcode 환경 확보 후 별도 착수.

### Windows 글자만 표시하는 자막 — 추가 검증

사용자의 화면 예시에 맞춰 번역을 게임 원문 아래에 배경·제목 표시줄·테두리 없이 표시합니다. 기존 일본어 → 한국어 방향을 유지합니다. 앱의 '원문 아래에 번역 글자만 표시'가 기본값이며, '위치 미리보기'는 실제 번역이 아님을 명시합니다. 원문 아래 여백이 없으면 위쪽을 사용하며 어느 쪽도 공간이 없으면 별도 결과 창으로 표시합니다. 장문은 4줄로 줄이고 전체 결과는 기존 결과 창에 보존합니다.

Release 빌드와 CTest 4개 그룹 모두 통과했습니다(전체 2.02초). Windows UI 테스트는 init/cleanup 포함 8개 통과(기능 테스트 6개)입니다. 실제 캡처·일본어 OCR, 캡처 전 화면 자막 숨김, 표시 해제, 취소, 음수 모니터 좌표의 배치, 배경 alpha, Windows layered/input-transparent/no-activate 플래그와 표시 전후 foreground 창 유지를 확인했습니다. 실제 게임의 클릭·독점 전체화면·고DPI 조합은 미검증입니다. API 키가 없으므로 실제 번역 결과가 화면에 나오는 실서비스 경로는 여전히 미검증이며, 테스트와 미리보기를 번역 성공으로 취급하지 않습니다.

검증 원자료: [UI 테스트 로그](evidence/windows-caption-tests.txt), [배경 없는 자막 렌더링 PNG](evidence/windows-transparent-caption.png). PNG의 문구는 위치 검증용 안내이고 실제 번역문이 아닙니다.

구현 근거: [Qt 6.8 투명 창](https://doc.qt.io/qt-6.8/qwidget.html#creating-translucent-windows), [입력 통과·포커스 플래그](https://doc.qt.io/qt-6.8/qt.html#WindowType-enum), [사용 가능한 모니터 좌표](https://doc.qt.io/qt-6.8/qscreen.html#availableGeometry-prop), [Microsoft 확장 창 스타일](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles). 변경은 Windows UI 어댑터에만 적용했고 공통 코어와 Android는 변경하지 않았습니다.

### DeepL 키 확인 — 추가 검증

사용자가 DeepL API 키를 직접 발급해 입력하는 방식에 맞춰, 첫 캡처 전에 키와 Free/Pro 설정을 확인하는 **키 확인** 버튼을 설정 화면에 추가했습니다. `GET /v2/usage`를 호출하므로 API 키만 전송되며 번역 글자는 차감되지 않습니다. 인식 텍스트는 보내지 않습니다. 키 끝이 `:fx`인지에 따라 Free/Pro 요금제를 자동으로 맞추며, 요금제가 바뀌면 안내 문구를 표시합니다.

Release 빌드(/W4 경고 없음)와 CTest 4개 그룹이 모두 통과했습니다(전체 2.03초). translation_protocol은 9개 항목이 통과했으며 새 항목은 3개입니다. 사용량 응답 파싱(DeepL 문서 예시 형식), 잘못된 응답·HTTP 오류 거부와 서버 본문 비노출, `:fx` 판별입니다. 배포 폴더의 `--smoke-test`는 exit0입니다. 이전 실행본이 `Ctrl+Alt+T`를 점유하면 windows_flow의 단축키 테스트가 실패하므로 이전 실행본을 종료한 뒤 측정했습니다.

**실제 키로 키 확인 요청을 보낸 적은 없습니다.** 테스트 응답은 프로토콜 fixture이며 실서비스 성공이 아닙니다. 실제 번역 경로도 여전히 미검증입니다. 번역 기능과 캡처 흐름, 공통 코어, Android는 변경하지 않았습니다. 번역 요청의 네트워크 처리 코드를 사용량 조회와 공유하도록 정리했지만 번역 요청 형식과 오류 문구는 그대로입니다.

검증 원자료: [프로토콜 테스트 로그](evidence/windows-key-check-tests.txt). 구현 근거: [DeepL 인증](https://developers.deepl.com/docs/getting-started/auth), [사용량 조회](https://developers.deepl.com/api-reference/usage-and-quota).

### 실제 DeepL 번역 — Windows CLI

사용자의 요청으로 Windows 자격 증명 관리자에 저장된 사용자 본인의 키를 사용해 실제 번역을 1회 실행했습니다. 키는 프로그램 안에서만 읽었고 출력·로그·문서에 남기지 않았습니다. 입력은 위와 같은 테스트 이미지(1000×180)입니다.

| 항목 | 결과 |
|---|---|
| OCR 원문 | 今日はいい天気です。 |
| 한국어 번역 (DeepL 실제 응답) | 오늘은 날씨가 좋습니다. |
| 요금제·엔드포인트 | DeepL API Free · api-free.deepl.com |
| OCR 시간 | 131ms |
| 번역 시간 | 1,195ms (연결 설정 포함 1회 측정) |
| 전체 시간 | 1,327ms |
| 외부 전송 텍스트 | 30 bytes (UTF-8 일본어 10자). TLS·HTTP 전송량은 미측정 |
| 이미지 외부 전송 | 없음 |
| 사용 글자 수 | 약 10자 |

잘못된 키 경로: 사용자 키가 아닌 가짜 키(`:fx` 형식)로 같은 요청을 보냈습니다. DeepL이 인증 실패를 반환했고 앱은 "DeepL 인증 실패. API 키와 Free/Pro 설정을 확인하세요."를 표시했습니다. 일본어 원문은 유지됐고 종료 코드는 5입니다(1,099ms).

한 번 측정한 값이므로 평균 지연으로 일반화하지 않습니다. 앱 화면 경로(단축키 → 화면 자막 표시)의 실제 번역, 오프라인, 한도 초과(456)는 이번에 실제로 발생시키지 않았습니다.

원자료: [실제 번역 JSON](evidence/windows-live-translation.json), [잘못된 키 JSON](evidence/windows-invalid-key.json).

### 자동 번역·영역 기억·바탕화면 바로가기 — 추가 검증

사용자 요청: 바탕화면 바로가기로 실행하면 바로 게임 대사 아래 줄에 번역이 뜨도록 한다. 사용자가 보낸 예시에 맞춰 번역문을 원문 줄 바로 아래에, 원문 시작 위치에 맞춰 왼쪽 정렬로 표시하도록 바꿨습니다(이전: 가운데 정렬). 글자 크기는 선택한 원문 줄 높이의 60%(16–36px)입니다.

| 항목 | 구현 | 실제 검증 | 남은 제한 |
|---|---|---|---|
| 자동 번역 (0.4초 폴링·변화 감지) | 완료 | 실제 화면 캡처·실제 OCR로 첫 대사 인식, 1.6초간 같은 대사 재인식 없음, 대사 변경 시 새로 인식, `Ctrl+Alt+P` 메시지로 켜기/끄기 | 실제 게임의 글자 출력 효과·배경 애니메이션·대사창 페이드, 장시간 CPU·배터리 사용량 미측정 |
| 변화 감지 규칙 | 완료 | 단위 테스트 7개: 정지 화면 1회 처리, 타자기 효과 완료까지 대기, 다음 대사 재처리, 한 글자 변화 감지, 1픽셀 잡음 무시, 움직이는 배경은 최대 3초마다, 초기화 | 임계값은 합성 이미지 기준이며 실제 게임별 조정 필요 가능 |
| 영역 기억 | 완료 | 저장 후 새 창에서 같은 모니터·영역 복원, 삭제 후 복원 안 함 | 해상도·배율이 바뀌면 복원하지 않고 재선택 안내(설계), 게임 창 이동은 감지하지 않음 |
| `--auto` 실행·바로가기 | 완료 | 설치 폴더에서 `--auto --smoke-test` exit0, 바로가기 대상·인수·작업 폴더 확인 | 실제 더블클릭 실행과 게임 위 자동 번역 전체 흐름은 사용자 확인 필요 |
| 원문 왼쪽 맞춤 배치 | 완료 | 배치 테스트에 왼쪽 정렬 조건 추가, 실제 캡처 테스트에서 원문과 겹치지 않음 확인 | 대사창 줄 간격과의 정확한 일치는 게임마다 다름 |

Release 빌드(/W4 경고 없음)와 CTest 5개 그룹 모두 통과했습니다: translation_protocol, local_checks, change_detection(신규), windows_flow, shared_core. windows_flow는 init/cleanup 포함 11개 통과, 건너뛴 항목 없음입니다. 이 테스트에는 API 키를 넣지 않았으므로 자동 번역 테스트가 확인한 것은 캡처·변화 감지·OCR·표시 경로이며 번역 성공이 아닙니다. 실제 번역은 위 'Windows CLI' 결과가 유일한 실서비스 검증입니다.

폴링 중에는 창을 숨기지 않습니다(깜빡임 방지). 대신 앱 창·결과 창·화면 자막이 자막 영역과 겹치면 그 프레임을 건너뜁니다. 자동 번역 중 빈 대사창은 오류로 표시하지 않고 화면 자막만 지웁니다. 설정에 자동 번역 시 대사마다 새 원문이 전송된다는 안내를 추가했습니다. 저장된 Tesseract 경로가 사라졌으면(예: 임시 폴더 정리) 앱에 포함된 OCR로 되돌아갑니다.

설치 위치: `%LOCALAPPDATA%\Programs\SubtitleTranslator`(사용자 폴더, 관리자 권한 없음). 바로가기: 실제 바탕화면 경로(OneDrive 리디렉션 포함)의 `Subtitle Translator.lnk`, 인수 `--auto`. 공통 코어와 Android는 변경하지 않았습니다. 변화 감지는 Qt 이미지를 쓰는 Windows 어댑터에 두었고, 모바일 화면 캡처 단계에서 공통 코어로 옮길 후보입니다.

검증 원자료: [변화 감지 테스트 로그](evidence/windows-change-detection-tests.txt), [Windows UI 테스트 로그](evidence/windows-auto-tests.txt).

### 다국어 OCR·번역 언어 선택 — 추가 검증

사용자 보고: 인식이 제대로 되지 않음. 원인: 설치된 OCR 모델이 일본어(`jpn`) 하나뿐이었고 명령에 `-l jpn`이 고정돼 있었습니다. 사용자가 보낸 한국어 게임 스크린샷의 대사 한 줄을 일본어 모델로 읽으면 한국어가 전혀 나오지 않는 무의미한 문자열이 됐습니다.

변경:

- Tesseract 공식 tessdata_fast(Apache-2.0)에서 `eng`, `kor`, `chi_sim`, `chi_tra`를 받아 GitHub의 git blob 해시로 검증했습니다. 기존 `jpn`도 같은 저장소 파일과 해시가 일치했습니다.

| 모델 | bytes | git blob SHA-1 |
|---|---|---|
| jpn | 2,471,260 | c4178f89991bde90b7fdc647e3e1901868423bd0 |
| eng | 4,113,088 | bbef4675053b5b468cdb477053e28b1c698ba08e |
| kor | 1,677,415 | 60986d44497689f3abace0b148199476d93292e1 |
| chi_sim | 2,469,156 | 388bac276d033d06e5ed5ba7a7ad14ae58f97dab |
| chi_tra | 2,366,642 | 1955cd8b8d076b898616e9c2c1ace65b43105356 |

- 메인 창에 원문 언어(기본: 자동 감지 `jpn+eng+kor`)와 번역 언어(기본 `KO`) 선택을 추가했습니다. 허용 목록에 있는 코드만 Tesseract 인수와 DeepL 요청에 들어갑니다.
- 자동 감지에서는 DeepL에 `source_lang`을 보내지 않아 DeepL이 언어를 감지하며, 응답의 `detected_source_language`를 결과 창에 표시합니다. 한 언어를 고르면 해당 `source_lang`을 보냅니다.
- 자막 배치 기본값을 "자동"으로 바꿨습니다. 높이 64px 이하 영역은 한 줄(PSM 7)로 읽습니다. 여러 모델을 불러온 상태의 블록 모드(PSM 6)가 한 줄짜리 한국어 대사의 첫 음절을 반복하는 현상을 관찰했기 때문입니다. 40px 미만 영역은 2배 확대합니다. Tesseract가 영어의 단독 `I`를 `|`로 읽는 경우를 보정합니다.
- 공통 데이터 형식의 `koreanText`를 `translatedText`로 바꾸고 원문·번역 언어 필드를 추가했습니다. Android는 변경하지 않았으며 여전히 일본어 → 한국어입니다.

측정(Windows, 빌드된 `subtitle_cli.exe`, 자동 감지 `jpn+eng+kor`, 자동 배치):

| 입력 | OCR 결과 | OCR 시간 |
|---|---|---|
| 사용자 한국어 게임 스크린샷의 대사 한 줄(400×42) | 한 글자(님→닝)를 제외하고 원문과 일치 | 264ms |
| 합성 일본어 한 줄(640×48, Yu Gothic 30px) | 원문과 일치 | 253ms |
| 합성 영어 한 줄(760×48, Segoe UI 30px) | 원문과 일치(`I` 보정 포함) | 229ms |
| 합성 영어 작은 글씨(260×24, 어두운 배경 흰 글자 14px) | 원문과 일치 | 209ms |

같은 이미지를 Tesseract로 직접 비교했을 때 한 언어 모델은 약 72–122ms, `jpn+eng+kor`는 약 188–236ms, 중국어까지 넣은 4개 조합은 약 300–373ms였습니다. 합성 이미지와 한 장의 스크린샷 결과이므로 게임 전체 정확도로 일반화하지 않습니다. 중국어 원문은 실제로 측정하지 않았습니다. 사용자 스크린샷 원본은 저장소에 넣지 않았습니다.

실제 번역 1회(사용자 키, 새 요청 형식): 합성 영어 작은 글씨 "Press any key to continue."(26 bytes)를 자동 감지 → 한국어로 요청했습니다. DeepL이 `EN`을 감지했고 결과는 "계속하려면 아무 키나 누르세요."입니다(번역 1,244ms). 사용자 게임 문장은 DeepL에 보내지 않았습니다.

테스트: CTest 5개 그룹 모두 통과. local_checks 12개 통과(실제 엔진으로 영어·한국어·일본어 자동 감지, 허용되지 않은 언어 거부, `|`→`I`, 확대, 자동 배치), translation_protocol 12개 통과(요청 본문의 `target_lang`과 선택적 `source_lang`, 감지 언어 파싱, 언어 목록 검증), windows_flow 11개 통과. 건너뛴 항목은 없습니다.

미검증: 실제 게임 화면에서의 자동 감지 품질, 중국어 원문, 한국어가 아닌 번역 언어의 실제 요청(요청 형식은 프로토콜 테스트로만 확인), Android 다국어.

원자료: [언어 OCR 테스트 로그](evidence/windows-language-ocr-tests.txt), [번역 프로토콜 테스트 로그](evidence/windows-translation-protocol-tests.txt), [자동 감지 실제 번역 JSON](evidence/windows-auto-detect-translation.json), [Windows UI 테스트 로그](evidence/windows-auto-tests.txt).
