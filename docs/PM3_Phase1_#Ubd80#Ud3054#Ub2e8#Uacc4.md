# PM3 iOS – Phase 1 이어서: 부팅 4단계 (Vfs 통일 → RECT/COLORFILL → 첫 화면)

## 0. 병합
- 기준 트리: `pm3ios_native`(bridge/display/touch/vfs + ios_native). `tools/x86_boot.cpp` 만 `pm3ios_x86_boot3` 것으로 교체(native 쪽은 부팅 2단계 + 별도 DirectSound HLE 였고, boot3 가 상위 버전).
- 검증: `test_core` 통과(VFS·뷰포트·터치·브리지 포함), `x86_test` EXE abp() 143/143 동일, 경고 0.

## 1. 이번 변경 (`tools/x86_boot.cpp`)
1. **파일 매핑을 `pm3::Vfs` 로 통일** (`host_path` 자체 구현 제거). 대소문자 무시·드라이브/PM3 별칭 제거·쓰기는 `<game>/_save`·읽기는 save 우선. 이전과 실행 결과 동일(106,730,174 스텝 / API 2,721,282회).
   - 부수 효과: 이전엔 새로 만든 `_save` 파일을 다시 읽지 못하는 문제가 있었는데 Vfs 로 해결됨.
2. **원인 발견 – RECT API 가 스텁이었음**: 게임이 `SetRect` 로 채운 RECT 를 `Blt` 에 넘기는데 RECT 가 항상 (0,0,0,0) 이라 빈 사각형만 복사됨. `SetRect/SetRectEmpty/CopyRect/IntersectRect/UnionRect/OffsetRect/InflateRect/PtInRect/IsRectEmpty/EqualRect` 구현.
3. **`Blt` DDBLT_COLORFILL** 구현(`DDBLTFX.dwFillColor`@80, dest NULL = 전체).
4. 디버그: `BLTLOG=1` (모든 Blt 의 대상/원본/RECT/플래그/호출 위치).

## 2. 결과
- 100M 스텝 시점에 primary·백버퍼(640x480 RGB565)에 픽셀이 들어옴. **타이틀 화면(메인 메뉴 4버튼)이 렌더링됨**을 PPM 덤프로 확인.
- 게임은 매 프레임 `백버퍼 COLORFILL(0)` → `백버퍼→primary Blt`(플래그 DDBLT_WAIT 만) 패턴. 스프라이트 합성은 DirectDraw `Blt` 가 아니라 게임 자체 코드가 `Lock` 한 메모리에 직접 수행.

## 3. 아직 확인 못 한 것 / 한계
- 버튼 주변의 검은 테두리·흰 배경 사각형이 **원래 디자인인지 렌더 오류(마스크/컬러키 처리)인지 미확인**. 실제 Windows 화면과 비교한 적 없음.
- 마우스·키 입력은 아직 메시지 주입 수준(`INJECT=1`), 실제 터치 연결 안 됨. `GetCursorPos` 는 (320,240) 고정.
- 오디오(waveOut 믹서, PMMIDI.dll 7개 → TinySoundFont), AVI 재생 미구현. x87 미구현.
- 코어 1프레임 진행 함수 없음(`x86_boot` 는 한 번에 N스텝 실행기) — 메시지 루프 지점에서 호스트로 제어 반환 필요.
- cmake/Xcode/Metal 없음 → CMake 경로와 `ios_native/` 는 실행하지 못함.

## 4. 다음
1. primary 서피스 → `pm3_bridge_present_surface` → SDL 뷰어/`PM3MetalView` 연결
2. `PeekMessage`/`timeGetTime` 지점에서 호스트로 돌아오는 "1프레임 진행" API
3. 터치 → `TouchMouse` → `WM_MOUSEMOVE/LBUTTONDOWN/UP` 메시지 + `GetCursorPos` 반영
4. 버튼 주변 사각형 원인 확인(게임 내부 마스크 처리 vs 내 HLE 결함)
