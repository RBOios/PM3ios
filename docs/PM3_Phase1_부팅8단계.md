# PM3 iOS – 부팅 8단계: 로그 끄기 · 스레드 로딩 중 응답성 · waveOut 오디오 (기준: pm3ios_native_fix2)

## 0. 기준
사용자 `fix2` 를 베이스로 함(CI 링크 수정, 64MB 메모리/해제 리스트 힙, 서피스 refcount, 스레드 호출 상한, `pm3_tapdrive`, workflow 입력). 그 위에 아래만 추가. `fix2` 와 겹치는 파일은 `x86host.cpp`, `CMakeLists.txt` 뿐이며 사용자 변경을 그대로 유지했다.

## 1. 변경
1. **로그 끄기** (`x86host.cpp`): 모든 `printf` → `hostlog`. 셸(훅) 모드에서는 기본 무음, `X86Hooks::verbose=true` 또는 환경변수 `PM3_VERBOSE=1` 로 켬. 배치 도구(`x86_boot`)는 이전처럼 출력. (헤드리스 8초 실행 출력: 수천 줄 → 2줄)
2. **스레드 로딩 중 응답성**: `call_guest`(중첩 호출, `CreateThread` 포함)를 슬라이스(20만 스텝)로 쪼개 슬라이스마다 `on_frame` 을 호출(`pump`). 로더 스레드가 도는 동안에도 화면 갱신, **일시정지, 종료**가 즉시 먹는다.
   - 측정(100M 스텝, 첫 프레임 이후 `on_frame` 간 최대 간격): fix2 **6,930,345 스텝**(약 0.1초+) → **346,609 스텝**.
3. **waveOut HLE**: `waveOutOpen/PrepareHeader/Write/UnprepareHeader/Reset/Close`. 형식 읽기(게임은 8000Hz·1ch·16bit, `CALLBACK_WINDOW`), `WAVEHDR.dwFlags`(PREPARED/INQUEUE/DONE) 갱신, 재생 시간(바이트/바이트율)이 지나면 DONE + `MM_WOM_DONE`(0x3BD) 메시지(창 콜백) 또는 함수 콜백 호출. PCM 은 `X86Hooks::on_pcm` 으로 전달.
4. **브리지 오디오 큐**: `pm3_bridge_pull_audio(b, int16 stereo*, frames)`, 출력 44.1kHz. 8/16bit·모노/스테레오·임의 샘플레이트 → 선형 보간 변환, 동시에 시작한 소리는 **더해서 믹스**.
5. **iOS 출력**: `ios_native/PM3Audio.{h,mm}` — `AVAudioEngine` + `AVAudioSourceNode` 가 위 함수를 당겨 감. `AppDelegate.mm` 에서 게임 시작 직후 `start`, 해제 시 `stop`. 무음 스위치 존중(Ambient), 전화 등 인터럽션 후 재시작. CMake: AVFoundation/AudioToolbox 추가.
6. 헤드리스: `PM3_WAV=<파일>` 이면 셸이 재생했을 오디오를 WAV 로 저장하고 큰 소리 샘플 수를 출력.

## 2. 검증 (이 환경: 리눅스 g++)
- `test_core` 통과, `x86_test` 143/143 동일, 배치 100M 스텝 결과 106,731,023 스텝(API 호출 +2 = waveOut 처리), 새 경고 없음(`wmp_dump` 1건은 기존).
- `pm3_headless … 264 380`(NEW GAME 탭) + `PM3_WAV`: 탭 직후 **0.5초짜리 소리 1개**(피크 9100, RMS 1632, 클리핑 0%, L=R)가 큐로 도달. 8000Hz → 44.1kHz 변환 확인.

## 3. 한계 (정직하게)
- **MIDI 배경음악은 여전히 없음.** `PMMIDI.dll` 의 MIDI_* 는 스텁(항상 0). 재생하려면 사운드폰트(.sf2)가 필요(TinySoundFont) 또는 iOS `AVMIDIPlayer` 기본 뱅크 사용 → 어느 쪽으로 갈지 결정 필요.
- 동기 스레드 방식 자체는 그대로: 로딩 중 **게임 로직은 여전히 멈춤**(화면 갱신·일시정지·종료만 살아남). 협력 스케줄링은 미구현.
- iOS 코드(`PM3Audio.mm` 포함)는 Xcode 로 한 번도 컴파일하지 못했다. CI(`ipa-native`)가 첫 컴파일.
- waveOut 은 이 환경에서 타이틀~NEW GAME 첫 탭까지만 소리 확인. 이후 장면의 음성/효과음 형식(스테레오, 22kHz 등)은 미확인. 오디오 지연(큐 길이)/끊김은 실기에서 확인 필요.
- AVI 오프닝(MCI) 미구현, x87 미구현.

## 4. 다음
1. MIDI BGM 방식 결정(사운드폰트 번들 vs iOS 기본 뱅크) 후 구현
2. 협력 스레드(진짜 비동기 로더)
3. CI 에서 `ipa-native` 재실행 → 오디오 포함 컴파일 오류 확인
