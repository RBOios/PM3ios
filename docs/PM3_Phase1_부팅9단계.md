# PM3 iOS – 부팅 9단계: MIDI 배경음악(내장 신스) · 협력 스레드 (기준: pm3ios_native_fix4_full)

## 1. 변경
1. **MIDI BGM** — `core/pm3/midi.{h,cpp}`: SMF(포맷 0/1) 파서 + 시퀀서(템포 변화, 러닝 스테이터스, 서스테인) + 소프트웨어 신스(40 보이스, GM 음색군별 파형, 드럼은 합성). 사운드폰트 불필요.
   - `PMMIDI.dll` 의 `MIDI_FileOpen / Play / Stop / GetIsPlay` 를 `x86host.cpp` 에서 처리: 게임이 주는 경로(`C:\PM3\MIDI\Mxxx.MID`)를 VFS 로 읽어 `X86Hooks::midi`(= 브리지가 가진 `MidiPlayer`)에 로드/재생. `Play` 의 세 번째 인자 ≠ 0 이면 반복.
   - `pm3_bridge_pull_audio` 가 SE/음성 PCM 위에 BGM 을 합성해 더함(80% 이상은 부드럽게 눌러 클리핑 방지). 일시정지 중엔 BGM 도 멈춤. iOS 쪽(`PM3Audio.mm`)은 변경 없음.
   - 배치 도구(`x86_boot`, 훅 없음)는 예전처럼 스텁(0).
2. **협력 스레드**(셸 모드): `CreateThread` 로더 스레드를 메인 슬라이스 사이에 번갈아 실행 → 장면 전환 로딩 중에도 게임 루프가 계속 돎. 배치 모드는 동기 실행(기준 106,731,023 스텝 불변).
3. 도구: `midi_render in.mid out.wav [초]`(신스 단독 렌더+RMS/피크), `pm3_tapdrive` 에 `PM3_WAV=<파일>`(오디오 캡처).
4. fix4 의 우클릭(두 손가락 탭/길게 누르기), Blt 개선 등은 **그대로 유지**(내 쪽 우클릭 변경은 넣지 않음).

## 2. 검증 (리눅스 g++)
- `test_core` 통과(MIDI 단위 테스트 추가: 노트 길이/종료 후 무음/반복/잘못된 파일), `x86_test` 143/143, ASan+UBSan 으로 MIDI 렌더 오류 없음.
- 실제 게임 헤드리스: 타이틀에서 `MIDI\M000.MID` 열림, 이후 장면에서 `Play(loop)` 호출 확인. 캡처 오디오에 BGM 구간 RMS 약 700~1400 추가(피크 클리핑 없음 – 소프트 니).
- MIDI 34곡(M000–M033) 렌더: RMS 0.017~0.06, 피크 0.15~0.35.

## 3. 한계
- 신스 음색은 간이형(GM 악기 소리가 아님). 소리는 나지만 원곡 음색과는 다름. 나중에 사운드폰트(TinySoundFont) 연결 가능하게 `MidiPlayer` 인터페이스(load/play/stop/render)를 분리해 둠.
- BGM 이 장면마다 바뀌는지, 전환 시 끊김은 실기 확인 필요. 게임 폴더에 `MIDI/` 폴더(원본)가 있어야 소리가 남.
- iOS 코드는 여전히 로컬 컴파일 불가 → CI 첫 빌드가 확인.
- 타이틀 글씨 모양: fix4 의 Blt 수정으로 바뀌었는지 실기 확인 필요(내가 직접 비교할 기준 사진 없음).

## 4. 추가 (fix5b)
- 무음(진동) 모드에서도 소리: `AVAudioSession` 카테고리를 `Ambient` → `Playback`(+`MixWithOthers`)로 변경. 이제 무음 스위치와 상관없이 미디어 볼륨으로 재생된다.
- 워크플로: `ipa`(SDL 뷰어) 잡 없음 — `core-tests`, `ipa-native` 만 남음(`target` 선택: all / core-tests / ipa-native).
