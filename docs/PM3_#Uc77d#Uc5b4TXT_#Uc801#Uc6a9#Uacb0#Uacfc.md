# 읽어_1.txt 적용 결과 (네이티브 iOS 포팅 설계 → 코어/셸 분리)

기준 코드: pm3ios_merged + x86 인터프리터 작업본(`pm3ios_native.zip`). 갱신본(pm3ios_x86_boot3.zip, PM3_Phase1_부팅3단계.md)은 **아직 열어보지 않음** – 다음 순서.

## 항목별 처리
| 문서 항목 | 처리 | 상태 |
|---|---|---|
| 코어(순수 C++)와 플랫폼 셸 분리 | `core/pm3/bridge.h/.cpp` C API 신설. 셸은 프레임 버퍼·뷰포트·입력·파일만 호출 | ✅ g++ 빌드, 테스트 |
| 프레임 버퍼 `uint32_t` 0xAARRGGBB (BGRA8Unorm 일치) | `display.h/.cpp` `convert_to_argb` (8bpp 팔레트 / RGB565 / RGB555 / BGR24 / BGRX32 → ARGB, 알파 0xFF), RGBQUAD 팔레트 변환 | ✅ 테스트 |
| 4:3 유지 레터박스(aspect fit), 정수 배율 옵션 | `fit_viewport` (iPhone 390x844pt → 390x292.5, iPad 4:3 → 전체), `integer_only` | ✅ 테스트 |
| 터치 좌표 역산 (여백 제거 후 배율 나눔, 0~639/0~479) | `view_to_game`(바깥이면 false, 좌표는 클램프), `game_to_view` | ✅ 테스트 |
| 터치 UX: 이동=호버, 뗄 때=클릭 | `touch.h/.cpp` `TouchMouse`: begin=Move, move=Move, end=Move+Down+Up, cancel=클릭 없음, 첫 손가락만, 여백에서 시작한 터치 무시 | ✅ 테스트 |
| 대소문자 무시 파일 열기 | `vfs.h/.cpp`: 경로 성분별 대소문자 무시 조회, 드라이브/`PM3`·`PM3W`·`PM3DATA` 별칭 제거, `..` 탈출 차단 | ✅ 테스트 |
| 세이브는 Documents (번들 읽기 전용) | `Vfs::set_save_dir`: 쓰기는 save 폴더, 읽기는 save 우선(세이브가 원본을 가림), `r+` 는 원본을 save 로 복사 후 수정 → 원본 불변 | ✅ 테스트 |
| Metal(MTKView) 출력: BGRA8Unorm 640x480 텍스처, replaceRegion, Nearest/Linear | `ios_native/PM3MetalView.h/.mm` (런타임 셰이더 컴파일, MTLViewport 로 레터박스, 포인트→픽셀 변환) | ⚠️ **컴파일 못 함** (Xcode/Metal 없음) |
| 프레임 독립 루프 `Game_UpdateAndRender()` | 셸 쪽 `frameCallback` 자리만 마련 | ❌ **코어 쪽 1프레임 진행 함수 없음** (x86_boot 는 무한 루프 실행기) |
| 오디오: MIDI→TinySoundFont, WAV→AudioQueue | 미착수 (`wav.cpp` 만 있음, PMMIDI.dll `MIDI_*` 7개 대체 필요) | ❌ |
| 글꼴: GDI 대신 비트맵/TTF | 이미 `core/pm3/tfn.cpp`(FONT22/24.TFN) 있음 → stb_truetype 불필요 | ✅ (기존) |
| stb_truetype / stb_vorbis / TinySoundFont 내장 | 네트워크가 없어 받지 못함 | ❌ |

## 검증
- `test_core` 통과 (VFS 13항목, 뷰포트·변환 12항목, 터치 10항목, 브리지 C API 포함), 경고 0 (기존 `wmp_dump` 1건 제외).
- 회귀 없음: EVX 기본 트레이스 바이트 동일, FPA 1505, x86 차분 143/143.
- 한계: cmake/Xcode 없음 → CMake 경로와 `ios_native/` 는 실행하지 못함. 보안 관점의 파일 접근은 `..` 차단만 확인했고 심볼릭 링크 탈출은 고려하지 않음.

## 의도적으로 안 건드린 것
- `tools/x86_boot.cpp` 의 자체 파일 매핑(`host_path`)을 `Vfs` 로 교체하는 작업: 다른 세션의 부팅 3단계 갱신본과 충돌하므로 병합 후 한 번에 수행.
- SDL 뷰어(`app/`)는 그대로. iOS CI 워크플로(unsigned ipa)도 그대로 SDL 경로 사용 – 네이티브 셸은 아직 CI 에 연결하지 않음.

## 다음
1. `pm3ios_x86_boot3.zip` 열어서 재현·병합 (x86_boot 의 파일 매핑을 `Vfs` 로 통일)
2. "코어 1프레임 진행" 래퍼 설계: 메시지 루프 지점(`PeekMessage`/`timeGetTime`)에서 호스트로 제어 반환
3. primary 서피스 → `pm3_bridge_present_surface` 연결
