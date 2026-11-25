# PM3 iOS – Phase 1 이어서: 부팅 5단계 (셸 연결: 워커 스레드 + 프레임 + 터치 → 게임)

## 0. 구조 변경
- `tools/x86_boot.cpp` 의 본체를 **코어 라이브러리** `core/pm3/x86host.{h,cpp}` 로 이동(`pm3::x86host_main(argc, argv, hooks)`). 도구는 5줄짜리 래퍼. 훅 없이 돌리면 이전과 **완전히 동일**(100M 스텝: 106,731,023 스텝 / API 1,843,765회, 리팩터링 전후 일치).
- 훅(`X86Hooks`): `slice_steps`(기본 20만), `save_dir`, `now_ms`(벽시계 → `timeGetTime`), `on_frame`(슬라이스마다 primary 서피스 전달, `idle` 표시), `poll_mouse`(셸 마우스 이벤트).
- 브리지 C API 추가: `pm3_bridge_start_game / stop_game / game_running / copy_frame`. 게임은 **워커 스레드**에서 돌고, 터치 큐·프레임 버퍼는 뮤텍스로 보호. 메시지 폴링만 하는 구간(`idle`)에서는 10ms 씩 잠듦.
- `ios_native/PM3MetalView.mm`: 공유 버퍼 대신 `pm3_bridge_copy_frame` 으로 복사 후 `replaceRegion` (**여전히 컴파일 못 해 봄**).
- `tools/pm3_headless.cpp`: 브리지 → 워커 → 게임 → 프레임, 터치 → 게임까지 헤드리스로 확인하는 도구. CMake 에 Threads 링크와 타깃 추가.

## 1. 이번에 고친 것 (게임 동작)
1. **마우스 이벤트 간격**: 터치 `end` 가 Move+Down+Up 을 한꺼번에 내면 프레임마다 버튼 상태를 읽는 게임이 클릭을 놓침(WndProc 은 3개 다 받았는데 화면 변화 0). 호스트가 버튼 이벤트를 **50ms 이상 간격**으로 전달하도록 변경 → 탭 후 화면 전환 확인(162,402 픽셀 변경).
2. **컬러키**: `SetColorKey`(DDCKEY_SRCBLT) 저장, `BltFast(DDBLTFAST_SRCCOLORKEY)` / `Blt(DDBLT_KEYSRC)` 에서 키 색 건너뜀. 게임은 `BltFast` trans=0x11(키 사용)/0x10(키 없음)을 구분해서 호출함. 이전 화면의 마젠타 사각형 제거(마젠타 계열 픽셀 0개로 확인).
3. 디버그: `MSGLOG=1`(게스트가 받는 마우스 계열 메시지), `BLTLOG=1` 에 `BltFast` 추가.

## 2. 검증 (이 환경: 리눅스 g++)
- `test_core` 통과, `x86_test` 143/143 동일, 경고 0.
- `pm3_headless <game_dir> /tmp/pm3_save 390 260`: 타이틀 → 6초 후 약 30만 비검은 픽셀, 탭(설정 버튼 위치) 후 화면 전환, 정지 시 `running=0`.
- 게임이 한글 대사를 그림(이 판본은 **한글판**으로 보임 → README.TXT 인코딩은 CP949 로 읽어야 함).

## 3. 아직 안 된 것 / 한계
- **iOS 에서 실행해 본 적 없음**: Xcode/Metal/cmake 가 없어 `ios_native/` 와 CMake 경로는 컴파일조차 못 함. 워커 스레드·Metal 업로드는 리눅스 헤드리스 확인까지만.
- 속도: 이 환경 약 55 Minsn/s. 실기(iPhone)에서 JIT 없이 같은 속도가 나오는지 **미확인**. 메시지 폴링 구간은 잠들지만 압축 해제 같은 무거운 구간의 체감은 모름.
- CI(`ios.yml`)는 아직 SDL 뷰어 경로를 빌드. 네이티브 셸(`ios_native`)은 CI 에 연결 안 함.
- 오디오(waveOut 믹서, PMMIDI.dll → TinySoundFont)와 AVI 오프닝은 미구현. x87 미구현.
- 게임 로그(`printf`)가 stdout 으로 계속 나옴(처음 약 4,000줄). 앱에서는 끄는 옵션이 필요.
- 마우스 우클릭·키보드 입력 없음. 한 번 더 누르는 연타/드래그 UX 는 미검증.

## 4. 다음
1. `ios_native` 를 감싸는 최소 iOS 앱 타깃(Xcode 프로젝트 생성: CMake `-G Xcode` 또는 xcodegen) + GitHub Actions 에서 unsigned IPA 빌드 → 컴파일 오류 잡기
2. 오디오: waveOut 믹서 → AudioQueue, `PMMIDI.dll` 7개 → TinySoundFont
3. 로그 끄기 옵션, 게임 폴더 선택/가져오기 UI(Files 앱), 일시정지(백그라운드 진입 시 워커 정지)
4. 실기 성능 측정 후 필요 시 디코드 캐시/블록 캐시
