# PM3 iOS – Phase 1 이어서: 부팅 6단계 (네이티브 iOS 앱 타깃 + GitHub Actions IPA 잡)

## 0. 요약
네이티브 셸(UIKit + Metal)을 **실제 앱 타깃**으로 묶고, GitHub Actions 에 unsigned IPA 를 만드는 잡(`ipa-native`)을 추가했다. **이 환경엔 Xcode/cmake/Apple SDK 가 없어 iOS 쪽 코드는 한 줄도 컴파일하지 못했다.** CI 첫 실행이 첫 컴파일이며, 오류가 나면 로그를 받아 고치는 순서.

## 1. 추가/변경
| 파일 | 내용 |
|---|---|
| `ios_native/AppDelegate.mm` | 스토리보드 없는 UIKit 앱. `Documents/`(또는 바로 아래 하위 폴더)에서 `PM3WIN.EXE` 를 찾아 `pm3_bridge_start_game()`. 세이브는 `Documents/Saves`. 없으면 안내 문구, 게임 종료 시 안내. 앱이 비활성/활성 될 때 `pm3_bridge_set_paused()`. 가로 고정. |
| `ios/Info_native.plist.in` | 기존 plist 복사 + 최소 iOS 14.0, `UIRequiresFullScreen`. 파일 앱 노출(`UIFileSharingEnabled`) 유지. |
| `CMakeLists.txt` | `pm3native` 타깃(iOS 전용, `PM3_BUILD_NATIVE_APP`), `-fobjc-arc`, Foundation/UIKit/Metal/MetalKit/QuartzCore, `CMAKE_OBJCXX_STANDARD 17`. |
| `.github/workflows/ios.yml` | 잡 `ipa-native` 추가(`core-tests` 통과 후, `-DPM3_BUILD_APP=OFF`로 SDL 미사용, 서명 없이 빌드 → `pm3native-unsigned.ipa` 아티팩트). 기존 `ipa`(SDL 뷰어)는 그대로. |
| `core/pm3/bridge.{h,cpp}` | `pm3_bridge_set_paused()`: 게임 슬라이스에서 대기하며 **게스트 시계(timeGetTime)가 멈춘 시간을 건너뜀**. |
| `ios_native/PM3MetalView.mm` | Metal 파이프라인 생성 실패 시 `NSLog`. |
| `tools/pm3_headless.cpp` | `PM3_PAUSE=1` 로 일시정지 검증. |

## 2. 검증 (이 환경: 리눅스 g++)
- `test_core` 통과, 경고 0. `x86_boot` 100M 스텝 결과 불변(106,731,023 스텝 / 1,843,765 API).
- `pm3_headless`: 3초 일시정지 동안 화면 변화 0픽셀, 재개 후 탭(설정 버튼) 정상 반응(90,452 픽셀 변경), 정지 시 `running=0`.
- 워크플로 YAML 파싱 확인(잡 3개: core-tests, ipa, ipa-native).

## 3. 한계 (정직하게)
- **iOS 코드 미컴파일**: `AppDelegate.mm`, `PM3MetalView.mm`, CMake 의 iOS 분기, 워크플로 `ipa-native` 는 한 번도 실행되지 않음. 흔한 실패 후보: ObjC++ 컴파일 옵션(ARC/표준), 프레임워크 링크, 아이콘/plist 경로, 앱 번들 이름(`pm3native.app`) 탐색.
- unsigned IPA 는 그대로는 설치되지 않는다. AltStore/Sideloadly 등으로 **직접 서명**해야 하고, 기기에서 **JIT 없이** 인터프리터가 충분히 빠른지는 **미확인**(리눅스 약 55 Minsn/s).
- 게임 데이터(원본 파일)는 앱에 넣지 않는다. 사용자가 파일 앱으로 `Documents/PM3` 등에 복사.
- 오디오(waveOut→AudioQueue, PMMIDI.dll→TinySoundFont), AVI 오프닝, x87, 키보드/우클릭 입력 미구현. 게임 로그 stdout 출력 끄기 옵션 없음.

## 4. 다음
1. GitHub 에 올려 `ios-ipa` 워크플로 `ipa-native` 실행 → 컴파일 오류 로그를 가져오면 수정(첫 컴파일이라 몇 번 반복 예상).
2. 실기에서 속도/발열 측정 → 필요 시 디코드 캐시.
3. 오디오 구현.
