# PM3 iOS – Phase 1 이어서: 인터프리터 부팅 2단계 (경고 없이 통과 → DirectDraw HLE → DirectSound 도달)

## 0. 받은 자료 재현 (PM3_Phase1_인터프리터_측정.md)
| 항목 | 문서 | 재현 |
|---|---|---|
| EXE abp() 차분 | 143/143 동일 | **143/143 동일**, 54,698,916 명령어 / 2,329,900 바이트 (23.5 insn/byte) |
| 속도 | 약 63 Minsn/s | 이 환경 67~98 Minsn/s (환경 차이) |
| 부팅 프로브 | 12,177 명령어, API 83회, DirectDrawCreate | 동일 (12,177 / 83) |
| 합본 코어 | – | `pm3ios_merged.zip` g++ 빌드 경고 0(기존 wmp_dump 1건 제외), test_core 통과, EVX 기본·`--exe` 트레이스 이전과 바이트 동일, FPA 1505 |
- 주의: x86 파일들은 pm3ios_merged.zip 이 아니라 `py_2f5d23.zip` 의 `py/` 안에 있음. 이번 작업본은 두 개를 합쳐서 CMake 에 x86.cpp/pe.cpp 와 x86_test/x86_boot 를 등록함.

## 1. 단계 1 (경고창 없이 통과) – 해결한 원인들
| 증상 | 원인 | 처리 |
|---|---|---|
| "현재 모드에서는 색이 흐려지거나…" | GetDeviceCaps 가 0 | PLANES=1, BITSPIXEL=16, 640x480 반환 |
| "게임이 설치되지 않았습니다" (1) | `SetCurrentDirectoryA` 를 0(실패)으로 돌려줌 | Win32 규약대로 성공(1) |
| 같은 경고 (2) | 레지스트리 `Software\NINELIVES\Princess Maker3JP\1.0` 의 **InstPath** 가 없음 (0x42D439 가 InstPath → 검증 → 현재 폴더 → `D:\PM3DATA` 순으로 탐색, 반환값 1 이어야 통과) | 가상 레지스트리: InstPath=`C:\PM3`, 그 외 값은 "없음" |
| 스택 손상(저장된 ebp 하위 바이트 0) | `RegQueryValueExA` 가 값이 없을 때 `*lpcbData` 를 그대로 둬서 EXE 가 `buf[*cb]=0` 으로 버퍼 1바이트 뒤(저장된 ebp)를 덮음 | 값 없음 → `*lpcbData=0` 반환 (실제 Windows 동작과 맞음) |
| "CD드라이브가 없습니다" | 드라이브 열거 | `GetLogicalDriveStringsA`="C:\\ D:\\", `GetDriveTypeA(D:)`=CDROM(5), 확인 파일 `D:\PM3W\ID_PM3JP.DAT` 존재 |
- 경고 문구 표: 오류 번호 → 문자열은 EXE 데이터 0x44C094 부터의 포인터 표 (0=설치 안 됨, 1=CD 없음, 2~3=CD 넣기, 4=메모리, 5=파일 읽기 실패, 6=데이터 설정 실패, 7=시스템 초기화, 8~=DirectX).

## 2. 단계 2 (진행) – 파일/메모리/DirectDraw
- **가상 파일 시스템**: `C:\PM3\…`, `D:\PM3W\…`, `D:\…`, 상대 경로를 모두 게임 폴더로 매핑(대소문자 무시), 게임이 새로 만드는 파일은 `<game>/_save`. CreateFileA/ReadFile/WriteFile/SetFilePointer/GetFileSize/CloseHandle 구현.
- **`wsprintfA` 실제 포맷** 구현(%s %d %u %x %c, 폭/0/-). 이게 없으면 경로가 빈 문자열 → "파일 읽기에 실패했습니다" 무한 반복.
- **할당 크기 추적**: EXE 가 `LocalSize` 로 크기를 되물어 FPA 목록(해시 테이블)을 만들기 때문에 LocalSize/HeapSize/LocalReAlloc 이 필요. 이걸 고치자 7.5M 스텝 이상 진행.
- **DirectDraw HLE**: IDirectDraw(24칸) / IDirectDrawSurface(36칸) / Clipper(9) / Palette(7) vtable, 표면 메모리·Lock(포맷 RGB565)·Blt 복사·EnumDisplayModes(콜백을 게스트에서 재진입 호출)·GetCaps.
- 현재 EXE 가 **실제로 호출한 COM 메서드 16종**: DDraw: SetCooperativeLevel, EnumDisplayModes, GetCaps, Release, CreateSurface, CreateClipper, CreatePalette / Clipper: SetHWnd / Surface: SetClipper, SetColorKey, GetDC, ReleaseDC, Lock, Unlock, Blt, SetPalette (문서의 "사용 오프셋 27종" 중 일부; SetDisplayMode/Flip/BltFast 등은 이 시점까지 안 쓰임).
- 열어본 데이터 파일: PM3SYS0/DAT1/DAT2/TAK1/TAK2.FPA 5개 (헤더·목록 구성 확인).

## 3. 현재 정지 지점
`waveOutOpen` (스텁이 성공 반환) 직후 `DirectSoundCreate` 에서 보호용 정지. 7,616,854 스텝, API 호출 455회.

## 4. 남은 작업 (순서)
1. DirectSound COM HLE (IDirectSound / IDirectSoundBuffer 의 사용 오프셋 파악 → WAV 믹서 연결)
2. 메시지 루프(PeekMessage/GetMessage), 타이머(timeGetTime 실제 시간), CreateThread(초상화 로더) – 스레드는 인터프리터에서 협력적으로 스케줄 필요
3. 화면 출력: primary 표면 메모리를 SDL 텍스처로 올리는 경로, 마우스/키 입력 → 메시지 주입
4. PMMIDI.dll MIDI_* 7개 → TinySoundFont, MCI(AVI) → 동영상, waveOut → 믹서
5. 속도: 지금은 매 명령 디코딩(60~100 Minsn/s). 렌더 루프 연결 후 프로파일, 필요하면 디코드 캐시/블록 캐시

## 5. 한계
- 정적 분석 + 인터프리터 실행으로 얻은 결과이고, 실제 Windows 와 비교 검증한 것은 아님.
- x87 미구현(만나면 예외 정지), 스레드/타이머/메시지 루프는 아직 가짜.
- GetCaps 값(4MB VRAM, 일부 caps)은 임의의 안전한 값.
