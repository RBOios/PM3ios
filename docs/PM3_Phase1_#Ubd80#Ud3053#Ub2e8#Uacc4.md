# PM3 iOS – Phase 1 이어서: 부팅 3단계 (DirectSound HLE → 메시지 루프 → 서피스 GDI)

## 0. 받은 자료 재현
| 항목 | 문서 | 재현 |
|---|---|---|
| 정지 지점 | `DirectSoundCreate`, 7,616,854 스텝, API 455회 | **동일** (7,616,854 / 455) |
| 핵심 코어 테스트 | 통과 | `test_core` 통과 (이 환경엔 cmake 가 없어 g++ 로 직접 빌드, ctest/CMake 경로는 실행하지 못함) |

## 1. 이번에 추가한 것 (`tools/x86_boot.cpp`)
1. **DirectSound HLE**: IDirectSound(11칸) / IDirectSoundBuffer(21칸). CreateSoundBuffer·Lock(래핑 포함)·Unlock·Play/Stop·GetStatus 등. 게임은 `SetCooperativeLevel` → `Release` 만 호출했고(존재 확인용으로 보임), 실제 소리는 `waveOut*` 경로를 쓰는 것으로 보임.
2. **윈도우/메시지 HLE**: `RegisterClassA` 에서 WndProc 저장(클래스명→프로시저), `CreateWindowExA` 가 `WM_NCCREATE`/`WM_CREATE` 를 게스트 WndProc 로 전달, `ShowWindow` 가 활성화 메시지 묶음(SHOWWINDOW, ACTIVATEAPP, NCACTIVATE, ACTIVATE, SETFOCUS, SIZE) 전달 + `WM_PAINT` 큐잉. `PeekMessage`/`GetMessage`/`DispatchMessage`/`SendMessage`/`PostMessage`/`PostQuitMessage`/`CallWindowProc`/`DefWindowProc`/`BeginPaint`/`EndPaint`/`UpdateWindow`/`InvalidateRect`.
3. **CreateThread**: 시작 함수를 **그 자리에서 끝까지 동기 실행**(협력 스케줄링 전의 임시 방식). 종료 코드는 `GetExitCodeThread` 로 반환, `WaitForSingleObject` 는 즉시 성공.
4. **서피스 DC용 GDI**: `Surface::GetDC` 핸들(0x6000+서피스)에 대해 `SetPixel`/`GetPixel`/`SetDIBitsToDevice`(1·8·16·24·32bpp 중 8/16/24/32, 상하 반전 처리, 팔레트 RGBQUAD).
   - 이유: 로그에 `GetDC → GetPixel/SetPixel → ReleaseDC → Lock → Unlock` 이 12번 반복됨 → 게임이 GDI 로 색을 찍고 Lock 으로 읽어 픽셀 포맷을 확인하는 것으로 **추정**.
5. 디버그: `DUMP_PRIMARY=<ppm>`, `DUMP_ALL=<접두사>` 로 종료 시 서피스를 PPM 으로 저장하고 "픽셀 있음/전부 0" 출력.

## 2. 결과 (60M 스텝)
- 게임이 윈도우 클래스 `PM3JP_WIN95_CLASS004` 를 등록하고 WndProc 이 실제로 돌아 `BeginPaint` 까지 도달.
- 서피스 24개 중 **14개에 픽셀이 들어옴**(이전엔 24개 전부 0). 컬러키 색, 숫자 글꼴 시트, 디더 패턴 등은 정상 색으로 보임.
- 메인 루프(`timeGetTime`/`PeekMessageA`/`GetCursorPos`, `MIDI_GetIsPlay`, `Surface::IsLost`)에 진입해 60M 스텝까지 계속 돎(정지 아님, 스텝 제한).

## 3. 아직 안 되는 것 / 모르는 것
- **primary 와 백버퍼(640x480)는 아직 전부 0** → 화면에 아무것도 그려지지 않음.
- 가설(미검증): 오프닝 동영상(`MOVIE/PM3OP.AVI`)을 `mciSendCommandA` 로 재생하고 종료 알림(`MM_MCINOTIFY`)을 기다리는 중일 수 있음. 지금 MCI 는 항상 0 만 반환.
- 흰색 단색으로 채워진 서피스 몇 개(160x192, 160x128, 64x80 등)는 원인 불명(팔레트/`DIB_PAL_COLORS` 가능성은 확인 못 함).
- `CreateThread` 동기 실행은 임시 방식: 스레드가 이벤트를 기다리며 도는 구조면 막힘(이번 실행에서는 문제 없었음).
- x87 미구현, 실제 Windows 와의 비교 검증 없음(이전과 동일).

## 4. 다음 순서 제안
1. 메인 루프 정체 파악: 루프 안에서 어떤 변수를 기다리는지(MCI 알림 / 마우스 / 키) 추적 → 필요한 메시지 주입 (`MM_MCINOTIFY`, `WM_LBUTTONDOWN`, `WM_KEYDOWN`)
2. 동영상(MCI)을 건너뛰기/알림 처리
3. primary 서피스 → SDL/Metal 텍스처 출력, 마우스·키 입력 → 메시지 큐
4. waveOut 믹서, `PMMIDI.dll` 7개 → TinySoundFont
5. 속도 프로파일링

---
## 5. 이어서 조사한 것 (메인 루프가 왜 아무것도 안 그리는가)
디버그 환경변수 추가 (`tools/x86_boot.cpp`): `TRACE_API`(MCI/MIDI/PeekMessage·Blt·Lock 호출 위치), `DUMPMEM=addr,...`, `PROFILE=<워밍업>,<샘플>`(EIP 히스토그램), `STATS`(API 호출 횟수), `INJECT=1`(마우스/키 주기 주입). `GetCursorPos` 는 (320,240), `ScreenToClient` 는 성공(1) 반환으로 변경.

**배제된 가설**
- MCI 동영상 대기: `mciSendCommandA` 는 시작 시 `MCI_OPEN(0x803)`/`MCI_CLOSE(0x804)` 두 번뿐이고 이후 호출 없음 → 영상 재생 종료 대기가 아님.
- 프레임 게이트 플래그(`0x4a1bb8`, `0x4a1b9c`, `0x4a1ca0`, `0x4a1c50`)는 전부 0 → 프레임은 정상으로 돎(33ms 간격, `timeGetTime`=steps/1000).
- 입력 대기: 400M 스텝 동안 마우스 이동/클릭/Enter 를 15회 주입해도 primary·백버퍼는 전부 0.
- 속도: 600M 스텝에 약 17초(≈35 Minsn/s, 이 환경 기준).

**관찰된 사실**
- 30M 스텝 부근 핫스팟은 ABP 압축 해제 루프(`0x404f86`~, 비트리더+4096 윈도우). 정상 상태(200M~)에서는 `0x428bc0`(락 래퍼) / `0x427340`(메시지 루프) / `0x4261xx`(마우스 히트 테스트) 위주.
- 정상 상태 API: `PeekMessage`·`GetCursorPos`·`timeGetTime` 폴링, `IntersectRect`/`SetRect`/`ScreenToClient`(히트 테스트), `MIDI_GetIsPlay`. 새 `SetDIBitsToDevice`/`Lock`/`Blt` 는 거의 없음(100M 스텝에 SetDIBits 22회).
- 지금까지의 `Surface::Lock`/`Blt` 호출 위치는 전부 초기화 코드(`0x404ad7`, `0x404af2`, `0x404e73`, `0x4067c8`, `0x406a92`).
- 프레임 본체(`0x428d68`)는 `0x425726` → `0x426dbd` → `0x429230`(둘 다 0 이면 `WM_SYSCOMMAND/SC_CLOSE` 전송) → `0x403c85` → `0x41b9c3` → `0x425738` 순으로 호출. 이 함수들 내부는 아직 분석 못 함.

**현재 가장 유력한 단서(미검증)**: `CreateThread` 를 "그 자리 동기 실행"으로 바꾼 것이 게임의 기대(비동기 로더 스레드 + 완료 플래그)와 어긋났을 수 있음. 장면 시작 변수/플래그를 찾아야 함.

**다음 단계 제안**
1. `0x426dbd`·`0x429230`·`0x403c85` 의 반환값/분기 조건을 BP 로 확인하고, 장면 번호로 보이는 전역 변수를 찾아 WATCH 로 첫 변경 지점 추적.
2. 스레드 시작 함수(`CreateThread` 의 start)와 그것이 쓰는 전역을 확인 → 필요하면 인터프리터에서 협력적 스레드(스텝 단위 번갈아 실행)로 구현.
3. 그 뒤에도 primary 가 비면 `Blt`/`Lock` 호출 위치(위 목록)에서 위로 거슬러 올라가 그리기 함수의 호출 조건을 역추적.
