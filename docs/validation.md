# 검증 결과 — 2026-09-29

## 간단한 사전 설명

Windows 실행 파일과 Android APK를 실제 빌드했습니다. Windows의 캡처·OCR 흐름과 Android 에뮬레이터의 이미지 입력·자르기·OCR 흐름을 검증했습니다. **사용자 DeepL API 키가 없으므로 실제 한국어 번역 성공은 검증하지 못했습니다. Android 실기기와 iOS도 미검증입니다.**

## 자세한 설명

### 구현·검증·제한

| 항목 | 구현 | 실제 검증 | 남은 제한 |
|---|---|---|---|
| 공통 C++ 모델·상태·공백 정리·직전 중복 처리 | 완료 | MSVC, Linux g++, Android NDK arm64/x86_64 빌드 및 JNI 계측 테스트 | 여러 번역 제공자 확장 전 오류 매핑 보강 |
| 저장 이미지 → 일본어 OCR | 완료 | Windows Tesseract, Android bundled ML Kit | 일반 게임 폰트·루비·세로쓰기 정확도 미검증 |
| DeepL HTTPS 번역 | 연결 완료 | 요청/응답 형식·403/429/456·잘못된 JSON 등 Windows 프로토콜 테스트 | 실서비스 키 없음. 실제 번역·비용·API 지연 미검증 |
| Windows 영역·단축키·별도 창 | 완료 | 실제 화면 캡처·WM_HOTKEY 경로·창 이동·재시도·취소 | 실제 게임·독점 전체화면·고DPI·복수 모니터 별도 |
| Windows 설정·키 저장 | 완료 | 앱 빌드·설정 코드 검토, 키 없음 경로 확인 | 실제 키의 저장/읽기 왕복 및 사용자 수동 설정 전체 동작 미검증 |
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
| 번역 API 요청 | 키 없어 보내지 않음 |
| 실제 번역 시간·전송 바이트 | 미측정 |
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

- 본인 API 키·동의 설정 후 실제 일본어 → 한국어 1회, 오프라인·키 오류·한도 오류·사용자 취소 확인.
- Android 실제 기기에서 picker·공유·핀치·회전·background·큰 이미지 및 화면 크기 확인.
- 게임의 실제 자막 샘플 여러 장으로 OCR 품질 측정.
- 위 흐름 검증 이후에만 Android MediaProjection·overlay를 별도 프로젝트 단계로 검증.
- iOS 구현은 macOS/Xcode 환경 확보 후 별도 착수.

### Windows 글자만 표시하는 자막 — 추가 검증

사용자의 화면 예시에 맞춰 번역을 게임 원문 아래에 배경·제목 표시줄·테두리 없이 표시합니다. 기존 일본어 → 한국어 방향을 유지합니다. 앱의 '원문 아래에 번역 글자만 표시'가 기본값이며, '위치 미리보기'는 실제 번역이 아님을 명시합니다. 원문 아래 여백이 없으면 위쪽을 사용하며 어느 쪽도 공간이 없으면 별도 결과 창으로 표시합니다. 장문은 4줄로 줄이고 전체 결과는 기존 결과 창에 보존합니다.

Release 빌드와 CTest 4개 그룹 모두 통과했습니다(전체 2.02초). Windows UI 테스트는 init/cleanup 포함 8개 통과(기능 테스트 6개)입니다. 실제 캡처·일본어 OCR, 캡처 전 화면 자막 숨김, 표시 해제, 취소, 음수 모니터 좌표의 배치, 배경 alpha, Windows layered/input-transparent/no-activate 플래그와 표시 전후 foreground 창 유지를 확인했습니다. 실제 게임의 클릭·독점 전체화면·고DPI 조합은 미검증입니다. API 키가 없으므로 실제 번역 결과가 화면에 나오는 실서비스 경로는 여전히 미검증이며, 테스트와 미리보기를 번역 성공으로 취급하지 않습니다.

검증 원자료: [UI 테스트 로그](evidence/windows-caption-tests.txt), [배경 없는 자막 렌더링 PNG](evidence/windows-transparent-caption.png). PNG의 문구는 위치 검증용 안내이고 실제 번역문이 아닙니다.

구현 근거: [Qt 6.8 투명 창](https://doc.qt.io/qt-6.8/qwidget.html#creating-translucent-windows), [입력 통과·포커스 플래그](https://doc.qt.io/qt-6.8/qt.html#WindowType-enum), [사용 가능한 모니터 좌표](https://doc.qt.io/qt-6.8/qscreen.html#availableGeometry-prop), [Microsoft 확장 창 스타일](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles). 변경은 Windows UI 어댑터에만 적용했고 공통 코어와 Android는 변경하지 않았습니다.
