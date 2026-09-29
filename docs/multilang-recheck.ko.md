# 다국어 OCR 재개 후 최종 확인 — 2026-09-29

## 확인한 상태

Claude Code가 추가한 자동 감지 점수 보정과 단계별 모델 실행을 보존했습니다.
이어서 기본 자동 감지의 중국어 한자만 있는 결과에 `source_lang=JA`를 강제하던 조건을 제거했습니다.
일본어·중국어를 직접 선택하면 해당 언어를 보내며, 자동 감지에서 한자만 있으면 DeepL의 언어 감지를 사용합니다.
일본어 대상 번역에서 가나를 보고 한자를 보존하는 규칙도 **한 줄씩** 적용합니다. 다른 줄의 중국어까지 건너뛰지 않습니다.
공통 변경은 표준 C++17 문자열 처리뿐이며 Windows ICU·Qt·ONNX Runtime은 Windows 어댑터에 남습니다.

## Windows 실측

- 환경: Windows 11 Pro 10.0.26200, Intel Core i7-13700KF(16코어/24스레드), 물리 RAM 약 32GB.
- Qt 6.8.3/MSVC2022 x64 Release, ONNX Runtime CPU 1.30.0. 앱과 동일한 `OcrService`를 실행했습니다.
- 14종(중국어 간체·번체 별도) × 문장 2개 × 스타일 4개 × 12·14·18px × 영역 2개 = **672장**.
- NFC 정규화 후 연속 공백을 하나로 합친 Unicode 코드포인트 Levenshtein 문자 오류율(CER). 낮을수록 좋습니다. 역순 허용이나 비슷한 글자 치환은 없습니다.
- 합성 문장 28개를 변형한 자료입니다. 독립적인 실제 게임 품질 평가나 모바일 성능 추정이 아닙니다.

| 모드 | CER | 완전 일치 | OCR 중앙값 / p95 | 프로세스 최대 작업 집합 |
|---|---:|---:|---:|---:|
| 원문 언어 직접 선택 | 4.406% | 413/672 | 40.64 / 118.18ms | 114.08MiB |
| 기본 한·중·일·영 자동 감지 | 33.909% | 240/672 | 89.98 / 179.48ms | 189.83MiB |
| 다국어 13종 자동 감지 | 6.640% | 383/672 | 194.45 / 411.41ms | 466.62MiB |

기본 모드는 14종 전체를 대상으로 한 모드가 아닙니다. 이 표의 전체 CER를 기본 모드의 지원 언어 품질로 해석하면 안 됩니다.
다국어 모드는 베트남어를 지원하지 않으므로 베트남어를 직접 선택해야 합니다. 베트남어는 Tesseract `vie`를 사용합니다.
기본 모드 1장은 아무 글자도 읽지 못해 전부 오류로 반영했습니다. 나머지 두 모드는 모든 이미지에서 비어 있지 않은 결과를 반환했습니다.
힌디어 21.0%, 중국어 번체 9.6%, 태국어 7.8% 등 어려운 언어의 한계는 기존 [검증 문서](validation.md)에 그대로 기록돼 있습니다.

시간은 이미지 디코딩 이후 OCR만 잰 값이며 화면 캡처·번역 네트워크·그리기는 제외합니다.
선택 모델 목록이 바뀐 첫 입력을 시간 통계에서 제외했습니다. 단계별 자동 감지에서 나중에 필요해진 모델의 최초 로딩은 일부 포함될 수 있습니다.
일반 사용 중인 PC에서 측정했으며 백그라운드 앱을 통제하지 않았습니다. 앞선 낮 시간 측정과 오류율은 같고 시간은 달랐습니다.
메모리는 벤치마크 호스트 프로세스의 최대 작업 집합입니다. Tesseract 자식 프로세스 메모리는 원자료 각 행의 `ocrChildPeakWorkingSetBytes`로 분리했습니다.

## 모델과 네트워크

Paddle 인식 모델 7개 합계 **69,815,852바이트(66.58MiB)**, 추가 베트남어 모델 **531,275바이트**입니다.
크기·출처·SHA-256은 [Paddle 목록](../scripts/paddle-models.json), [베트남어 목록](../scripts/tesseract-extra-models.json)에 있습니다.
기존 Tesseract 모델·Qt·ONNX DLL을 포함한 전체 설치 크기와는 다릅니다.

이 벤치마크는 이미지를 로컬에서 읽으며 DeepL·기타 번역 API를 호출하지 않았습니다. 전체 PC의 패킷량을 측정한 것은 아닙니다.
새 언어의 실제 DeepL 응답 품질은 이번 작업에서 검증하지 않았습니다. 기존 실제 API 검증 기록과 구분합니다.
Android/iOS 빌드·실기기 성능은 이번 변경으로 검증하지 않았습니다.

## 재현과 원자료

- [이번 측정 요약](evidence/windows-multilang-recheck.json)
- [앞선 Claude 측정 요약](evidence/windows-multilang-summary.json)도 보존했습니다.
- 원자료: `evidence/windows-multilang-recheck-raw.json.gz` — 3개 모드의 실제 인식 문자열·정답·시간·오류 포함.
- 생성기: `scripts/make-multilang-fixtures.py` (Python/Pillow/NumPy/libraqm은 테스트용 도구).
- 실행기: `tests/ocr_benchmark.cpp`; 채점기: `scripts/summarize-ocr-benchmark.py`.

```powershell
python scripts/make-multilang-fixtures.py --output build/ocr-benchmark
$tc = 'D:\SubtitleTranslatorToolchain'
$env:PATH = "$tc\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
foreach ($mode in 'selected','legacy','multi') {
    & "$tc\windows-build\Release\ocr_benchmark.exe" --manifest build/ocr-benchmark/manifest.json --models "$tc\paddle" --tesseract "$tc\ocr\tesseract.exe" --tessdata "$tc\ocr\tessdata" --mode $mode --output "build/ocr-$mode.json"
}
python scripts/summarize-ocr-benchmark.py build/ocr-selected.json build/ocr-legacy.json build/ocr-multi.json --output build/ocr-summary.json
```

## 공식 자료

- [PaddleOCR 다국어 모델](https://www.paddleocr.ai/main/en/version3.x/algorithm/PP-OCRv5/PP-OCRv5_multi_languages.html): 모델별 문자 계열.
- [ONNX Runtime 스레드 설정](https://onnxruntime.ai/docs/performance/tune-performance/threading.html): 세션별 스레드 수와 유휴 spinning을 제한했습니다.
- [Windows ICU](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-): 현재 `icu.lib` 연결은 Windows 10 1903 이상이 필요합니다. 이번 실제 실행 검증은 Windows 11입니다.
- [ICU 양방향 문자 API](https://unicode-org.github.io/icu-docs/apidoc/dev/icu4c/ubidi_8h.html): 아랍어 시각 순서를 논리 순서로 복원합니다. 복잡한 양방향 혼합 문장은 별도 검증이 필요합니다.
- [DeepL 지원 언어](https://developers.deepl.com/docs/getting-started/supported-languages): 현재 서비스가 지원하는 언어와 조회 API.

## 최종 빌드·설치 검증

최종 변경을 반영한 Windows Release 빌드 완료. CTest 6개 그룹 모두 통과했습니다.
번역 프로토콜 13, 로컬 입력·OCR 19, Paddle/다국어 36, 화면 변화 감지 9, 화면 흐름 14개가 QtTest 보고 기준으로 통과했고 공통 코어도 통과했습니다.
화면 흐름에는 실제 단축키 메시지 → 캡처 → OCR, 자동 캡처 시작·중지, 중복 억제, 자막 창 동작 검사가 포함됩니다.
Windows 공통 코어와 같은 소스를 Ubuntu 22.04의 g++ `-std=c++17 -Wall -Wextra`로 빌드·실행해 통과했습니다. Android/iOS 검증을 의미하지 않습니다.

기존 앱을 사용자가 종료한 뒤 `%LOCALAPPDATA%/Programs/SubtitleTranslator`에 설치했습니다.
설치된 EXE와 `dist/windows/SubtitleTranslator.exe`의 SHA-256 일치를 확인했습니다.
바탕화면 `Subtitle Translator` 바로가기가 설치 폴더를 가리킵니다. 사용자 설정과 자격 증명은 설치 파일과 별도로 보존됩니다.

원자료: [최종 검사 요약](evidence/windows-final-checks.json), [화면 테스트](evidence/windows-final-ui-tests.txt).
