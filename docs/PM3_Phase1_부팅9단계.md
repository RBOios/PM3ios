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

## 5. 추가 (fix6): 음성 재생 시 찢어지는 소리
- 원인: 게임은 음성(TAK)을 **IMA ADPCM(포맷 0x11, 22.05kHz 모노, 블록 512B)** 으로 `waveOut` 에 쓰는데, HLE 가 포맷 태그를 무시하고 압축 바이트를 16bit PCM 으로 재생 → 풀스케일 잡음(실기 녹화에서 0.4초 구간 RMS 14000, 클리핑 137샘플, 영교차 ≈4.8kHz 로 확인).
- 수정: `waveOutOpen` 에서 태그/블록 크기 저장, `waveOutWrite` 에서 IMA ADPCM 을 PCM16 으로 디코드해서 `on_pcm` 으로 전달. 재생 시간(WOM_DONE 타이밍)도 디코드된 길이로 계산(이전엔 4배 짧게 계산됨 → 음성 버퍼가 너무 빨리 반환).
- 확인: 같은 입력 흐름에서 해당 구간이 영교차 400~1800Hz 의 음성 형태로 바뀜(잡음 사라짐). 디코더는 `wav.cpp` 의 IMA 와 같은 레이아웃(4바이트 헤더 + 4바이트 그룹).
- 디버그: `PM3_PCMLOG=1` 이면 waveOut 오픈 포맷과 PCM 블록(크기/피크)을 stderr 로 출력, `PM3_PCMDUMP=1` 은 22.05kHz 블록을 /tmp 에 저장.

## 6. 음량 비율 (fix6b)
- 음성/효과음 : BGM ≈ 2 : 1 (`bridge.cpp` 의 `kVoiceGain=0.6`, `kBgmGain=1.54`(2.2 의 70%)). 측정: 음성 구간 RMS ≈ 5000, BGM 단독 구간 ≈ 2000~3000, 최대 피크 ≈ 24000(클리핑 없음). 바꾸려면 두 상수만 조정.
