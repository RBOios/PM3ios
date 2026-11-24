# ios_native – 네이티브 iOS 셸 (Metal) – **미검증**

이 폴더의 소스는 Xcode/Metal 이 없는 환경에서 작성되어 **한 번도 컴파일·실행하지 못했습니다.** 코어 쪽(`core/pm3/{vfs,display,touch,bridge}`)은 g++ 로 빌드되고 `tests/test_core.cpp` 로 검증됩니다.

## 구성
| 계층 | 파일 | 역할 |
|---|---|---|
| 코어(C++17, OS 의존 없음) | `core/pm3/bridge.h/.cpp` (C API) | 640x480 `uint32_t` 0xAARRGGBB 프레임 버퍼, 레터박스 뷰포트, 터치→마우스 이벤트 큐, 대소문자 무시 파일 열기 |
| iOS 셸(ObjC++) | `PM3MetalView.h/.mm` | `MTKView` 서브클래스: BGRA8Unorm 텍스처로 프레임 버퍼를 올려 aspect-fit 로 그림, 터치를 코어로 전달 |

## 앱 쪽에서 해야 할 연결 (예시)
```objc
// AppDelegate / ViewController
NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
Pm3Bridge *b = pm3_bridge_create();
pm3_bridge_set_paths(b, docs.UTF8String /* 게임 데이터: 사용자가 Files 앱으로 넣음 */, [docs stringByAppendingPathComponent:@"Saves"].UTF8String);
PM3MetalView *v = [[PM3MetalView alloc] initWithFrame:self.view.bounds bridge:b];
pm3_bridge_start_game(b);   // 워커 스레드에서 PM3WIN.EXE 를 실행(x86 인터프리터 + Win32 HLE). 프레임/터치는 브리지가 연결
[self.view addSubview:v];   // frameCallback 은 필요 없음
```
- Info.plist: `UIFileSharingEnabled = YES`, `LSSupportsOpeningDocumentsInPlace = YES` (Files 앱에서 Documents 가 보이도록).
- 게임 폴더는 읽기 전용으로 쓰고, 세이브/설정은 `Saves/` 로 갑니다. 같은 이름이 있으면 세이브 쪽이 우선합니다.

## 아직 연결되지 않은 것 (정직하게)
- **게임 실행은 워커 스레드 방식으로 연결됨**(`pm3_bridge_start_game/stop_game/copy_frame`, `core/pm3/x86host.cpp`). 코어는 UI 스레드를 막지 않습니다. 이 경로는 `tools/pm3_headless`(g++, 리눅스)로 프레임 출력과 탭 입력까지 확인했지만 **iOS 에서는 실행해 보지 못했습니다**. 게임이 메시지만 폴링하는 구간에서는 워커가 10ms 씩 잠듭니다.
- 오디오(MIDI/WAV): 코어의 `wav.cpp` 는 있으나 AudioQueue/AudioUnit 출력과 TinySoundFont(MIDI) 연결이 없습니다. PMMIDI.dll 의 `MIDI_*` 7개를 TinySoundFont 로 대체하는 작업이 남았습니다.
- 폰트: GDI 대신 `core/pm3/tfn.cpp`(FONT22/24.TFN 비트맵)로 메모리 버퍼에 직접 그릴 수 있습니다 (stb_truetype 불필요).
- 터치: 이동=호버, 손가락 뗄 때=클릭. 우클릭/길게 누르기는 아직 없습니다.
