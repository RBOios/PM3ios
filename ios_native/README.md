# ios_native – 네이티브 iOS 앱 (UIKit + Metal, SDL 없음)

**상태: 이 폴더의 Objective-C++ 는 한 번도 컴파일하지 못했다** (작성 환경에 Xcode/ObjC++ 컴파일러 없음). GitHub Actions `ipa-native` 잡이 첫 컴파일이다.
코어(`core/pm3/*`)는 g++ 로 빌드·테스트된다 (`tests/test_core.cpp`).

## 화면 흐름
1. **시작 화면** (`PM3LauncherViewController`): 배경 일러스트(`Resources/launch_bg.jpg`) + 어두운 반투명 둥근 패널
   - `게임 시작하기 ▶` → 선택한 폴더로 게임 실행 (`PM3GameViewController`), 게임이 끝나면 시작 화면으로 복귀
   - `음량 설정` → 패널이 슬라이더 3개로 바뀜: **보이스 / BGM / 효과음** (0~150 %, 기본 100 %, 5 % 단위). 놓을 때 해당 소리가 한 번 재생됨
   - `게임 폴더 선택…` → 파일 앱의 폴더 선택기(`UIDocumentPickerViewController`, 폴더). `PM3WIN.EXE` 가 그 폴더 또는 바로 아래 폴더에 있어야 함
   - `폴더: <이름>` → 현재 선택된 폴더 (없으면 "선택 안 됨", 예전처럼 앱 Documents 에 복사한 경우 자동 인식)
2. **게임 화면** (`PM3GameViewController`): Metal 뷰, 앱이 비활성이 되면 게임 시계 정지

## 저장되는 것 (`PM3Settings`)
| 항목 | 위치 |
|---|---|
| 음량 3종 | `NSUserDefaults` `pm3.volume.0/1/2` (0=보이스, 1=BGM, 2=효과음) |
| 게임 폴더 | `NSUserDefaults` `pm3.gameFolderBookmark` (북마크). 다음 실행 때 복원하고 보안 범위 접근을 다시 연다 |
| 게임이 쓰는 파일(세이브 등) | 앱 `Documents/Saves` (선택한 폴더는 읽기 전용으로만 사용) |

## 음량 그룹이 코어에서 나뉘는 방식
- 보이스 = 게임이 `waveOut` 으로 재생하는 **IMA ADPCM**(22.05 kHz 모노) 스트림, 효과음 = **일반 PCM**(예: 클릭음 8 kHz), BGM = MIDI 합성기.
- `pm3_bridge_set_volume(bridge, PM3_VOL_VOICE|BGM|SFX, 0..2)`; 1.0 은 이전의 고정 음량과 같다. `pm3_bridge_audio_preview()` 는 슬라이더용 샘플.
- 이 분류는 실제 게임으로 확인함: 새 게임 클릭음은 효과음으로 분류되어 효과음 0 → 무음, 보이스 0 → 변화 없음, 효과음 0.5 → 피크 정확히 절반. 보이스(ADPCM)가 나오는 장면에서의 분류는 코드상으로만 확인(미실행).

## 파일
`AppDelegate.mm`(진입), `PM3LauncherViewController.*`, `PM3GameViewController.*`, `PM3Settings.*`, `PM3MetalView.*`(Metal 출력/터치), `PM3Audio.*`(AVAudioEngine 출력), `Resources/launch_bg.jpg`.

## CI 첫 컴파일에서 의심해 볼 곳
- 링크: `UniformTypeIdentifiers`(UTTypeFolder) 를 CMake 에 추가했다. 이전에 `CoreGraphics` 가 빠져 링크가 실패한 적이 있어, 새 프레임워크 심볼이 "Undefined symbols" 로 나오면 `target_link_libraries(pm3native …)` 에 `-framework` 를 더하면 된다.
- 리소스: `launch_bg.jpg` 는 `MACOSX_PACKAGE_LOCATION Resources` 로 번들에 넣고 `[UIImage imageNamed:@"launch_bg.jpg"]` 로 읽는다. 번들 루트에 안 들어가면 배경이 어두운 단색으로 나온다(앱은 동작).
- 폴더 선택기는 iOS 14 이상 API (`initForOpeningContentTypes:`), 앱의 최소 버전이 14.0 이다.
- iCloud Drive 폴더의 파일이 기기에 내려받아져 있지 않으면 게임이 파일을 못 읽는다(앱이 안내 문구를 띄움).
