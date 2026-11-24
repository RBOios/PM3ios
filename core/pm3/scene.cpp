#include <cctype>
#include "pm3/scene.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

#include "pm3/abp.h"
#include "pm3/evx.h"
#include "pm3/wmp.h"

namespace pm3 {
namespace {

struct Key { bool has = false; uint8_t c[3] = {0, 0, 0}; };

// Most frequent magenta-ish colour (G <= 0x0c, R,B >= 0xb0) in x>=640 part of the sheet (whole image when <= 640 wide);
// ties resolve to the colour seen first (Counter.most_common(1) semantics).
Key find_key(const Image& im) {
    int x0 = im.w > 640 ? 640 : 0;
    std::map<uint32_t, std::pair<int, int>> cnt;   // colour -> (count, first index)
    int order = 0;
    for (int y = 0; y < im.h; y++)
        for (int x = x0; x < im.w; x++) {
            const uint8_t* p = &im.rgba[((size_t)y * im.w + x) * 4];
            if (p[1] <= 0x0c && p[0] >= 0xb0 && p[2] >= 0xb0) {
                uint32_t k = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                auto it = cnt.find(k);
                if (it == cnt.end()) cnt[k] = {1, order++};
                else it->second.first++;
            }
        }
    Key k;
    int best = 0, bestOrder = 0;
    for (auto& kv : cnt)
        if (kv.second.first > best || (kv.second.first == best && kv.second.second < bestOrder)) {
            best = kv.second.first; bestOrder = kv.second.second;
            k.has = true; k.c[0] = (uint8_t)(kv.first >> 16); k.c[1] = (uint8_t)(kv.first >> 8); k.c[2] = (uint8_t)kv.first;
        }
    return k;
}

// Copies the sheet rect (page*256+srcx, srcy, w, h) to dst at (dstx, dsty); key colour is transparent,
// reads outside the sheet give opaque black (PIL crop padding), writes are clipped to dst.
void blit(const Image& sheet, const WmpRec& r, const Key& key, Image& dst) {
    for (int yy = 0; yy < r.h; yy++) {
        int dy = r.dsty + yy;
        if (dy < 0 || dy >= dst.h) continue;
        int sy = r.srcy + yy;
        for (int xx = 0; xx < r.w; xx++) {
            int dx = r.dstx + xx;
            if (dx < 0 || dx >= dst.w) continue;
            int sx = r.page * 256 + r.srcx + xx;
            uint8_t px[3] = {0, 0, 0};
            if (sx >= 0 && sx < sheet.w && sy >= 0 && sy < sheet.h) std::memcpy(px, &sheet.rgba[((size_t)sy * sheet.w + sx) * 4], 3);
            if (key.has && px[0] == key.c[0] && px[1] == key.c[1] && px[2] == key.c[2]) continue;
            uint8_t* o = &dst.rgba[((size_t)dy * dst.w + dx) * 4];
            o[0] = px[0]; o[1] = px[1]; o[2] = px[2]; o[3] = 255;
        }
    }
}

}  // namespace

WmpScene wmp_scene_load(const uint8_t* r, size_t n) {
    WmpInfo info = wmp_parse(r, n);
    Image sheet = decode_wmp(r, n);
    Key key = find_key(sheet);

    Image bg; bg.w = 640; bg.h = 480; bg.rgba.assign((size_t)640 * 480 * 4, 255);
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 640; x++) {
            uint8_t* o = &bg.rgba[((size_t)y * 640 + x) * 4];
            if (x < sheet.w && y < sheet.h) std::memcpy(o, &sheet.rgba[((size_t)y * sheet.w + x) * 4], 3);
            else o[0] = o[1] = o[2] = 0;
        }

    struct Run { uint16_t k[4]; std::vector<const WmpRec*> recs; };
    std::vector<Run> runs;
    std::vector<const std::vector<WmpRec>*> statics;
    for (size_t g = 1; g < info.groups.size(); g++) {
        const auto& grp = info.groups[g];
        if (grp.size() == 1) {
            const WmpRec& rc = grp[0];
            uint16_t k[4] = {rc.dstx, rc.dsty, rc.w, rc.h};
            if (!runs.empty() && std::memcmp(runs.back().k, k, sizeof k) == 0) runs.back().recs.push_back(&rc);
            else { Run ru; std::memcpy(ru.k, k, sizeof k); ru.recs.push_back(&rc); runs.push_back(std::move(ru)); }
        } else {
            statics.push_back(&grp);
        }
    }
    uint64_t L = 1;
    for (auto& ru : runs) {
        uint64_t a = L, b = ru.recs.size();
        while (b) { uint64_t t = a % b; a = b; b = t; }
        L = L * ru.recs.size() / a;
    }
    L = L > 1 ? std::min<uint64_t>(L, 24) : 1;
    size_t nframes = runs.empty() ? 1 : (size_t)L;

    WmpScene sc;
    for (size_t t = 0; t < nframes; t++) {
        Image f = bg;
        for (auto* g : statics) for (const WmpRec& rc : *g) blit(sheet, rc, key, f);
        for (auto& ru : runs) blit(sheet, *ru.recs[t % ru.recs.size()], key, f);
        sc.frames.push_back(std::move(f));
    }
    return sc;
}

std::string evx_clean_text(const std::string& r) {
    std::string o;
    // Name placeholders: the EXE keeps these four tokens (0x4EB30..) and swaps in the player's names at draw time.
    static const char* const kNames[][2] = {{"fmlyname2", "\xbc\xba\x32"}, {"frstname2", "\xc0\xcc\xb8\xa7\x32"}, {"fmlyname", "\xbc\xba"}, {"frstname", "\xc0\xcc\xb8\xa7"}};   // 성2 이름2 성 이름
    for (size_t i = 0; i < r.size();) {
        uint8_t c = (uint8_t)r[i];
        bool sub = false;
        for (auto& nm : kNames) { size_t l = std::strlen(nm[0]); if (r.compare(i, l, nm[0]) == 0) { o += nm[1]; i += l; sub = true; break; } }
        if (sub) continue;
        if (c == 0x1b && i + 1 < r.size()) {
            char k = r[i + 1]; i += 2;
            if (k == '/') { o += '?'; while (i < r.size() && isdigit((uint8_t)r[i])) i++; }   // variable value (not modelled)
            else if (k == 's') { while (i < r.size() && isdigit((uint8_t)r[i])) i++; }
            continue;                                                                          // '[' ']' etc.: markers only
        }
        if (c >= 0x81 && i + 1 < r.size()) { o += r[i]; o += r[i + 1]; i += 2; }
        else { o += r[i]; i++; }
    }
    return o;
}

bool evx_scene_info(const Fpa& sys0, const FpaEntry& e, EvxSceneInfo& out) {
    out = EvxSceneInfo();
    FpaEntry e2 = e;
    e2.begin -= 12;   // EVX: the ABP_ header starts 12 bytes before the index offset
    Bytes raw, data;
    if (!sys0.read(e2, raw) || !abp_decompress(raw.data(), raw.size(), data)) return false;
    try {
        EvxScript sc = EvxScript::load(e.name, data);
        EvxVm vm(sc, 1);
        vm.V[4] = 12;
        vm.run(60000);
        ScenePortrait slots[4]; bool loaded[4] = {false, false, false, false};
        int px[4] = {0, 0, 0, 0}, py[4] = {0, 0, 0, 0};   // slot position variables (persist; reset to 0 when a portrait loads)
        auto snapshot = [&](SceneStep& st) {
            st.bg = out.bg(); st.sprite = out.sprite();
            for (int i = 0; i < 4; i++) if (loaded[i]) { ScenePortrait q = slots[i]; q.x = px[i]; q.y = py[i]; st.portraits.push_back(q); }
        };
        for (const EvxEvent& ev : vm.events) {
            if (ev.kind == EvxEvent::Text) {
                if (ev.a == 0x71 || ev.a == 0x84) {
                    SceneStep st; st.op = ev.a; st.who = ev.b; st.text = evx_clean_text(ev.text);
                    if (st.text.empty() || st.text.find("fwin Close Error") != std::string::npos) continue;
                    snapshot(st);
                    if (!out.steps.empty() && out.steps.back().text == st.text) { st.ticks = out.steps.back().ticks; out.steps.back() = std::move(st); }   // 71 echo + 84 display of one line
                    else out.steps.push_back(std::move(st));
                }
            } else if (ev.kind == EvxEvent::Wait) {
                if (!out.steps.empty() && ev.a > 0) out.steps.back().ticks += ev.a;
            } else if (ev.kind == EvxEvent::Background) out.backgrounds.push_back(ev.a);
            else if (ev.kind == EvxEvent::Sprites) out.sprites.push_back(ev.a);
            else if (ev.kind == EvxEvent::Portrait && ev.a >= 0 && ev.a < 4) {
                px[ev.a] = py[ev.a] = 0; loaded[ev.a] = true;
                slots[ev.a] = {ev.a, ev.c, ev.d, 0, 0};
            } else if (ev.kind == EvxEvent::Move && ev.a >= 0 && ev.a < 4) {
                px[ev.a] = ev.b; py[ev.a] = ev.c;   // every mode ends at (x, y): 0 = jump, 1/2 = glide
            }
        }
        for (int i = 0; i < 4; i++)
            if (loaded[i]) { slots[i].x = px[i]; slots[i].y = py[i]; out.portraits.push_back(slots[i]); }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void draw_portrait(const Wbd& w, const Image& frame, int x, int y, Image& canvas) {
    int x0, y0, x1, y1;
    w.bbox(x0, y0, x1, y1);
    int ox = x0 + 2 * x - 256, oy = y0 + 2 * y;
    for (int yy = 0; yy < frame.h; yy++) {
        int cy = oy + yy;
        if (cy < 0 || cy >= canvas.h) continue;
        for (int xx = 0; xx < frame.w; xx++) {
            int cx = ox + xx;
            if (cx < 0 || cx >= canvas.w) continue;
            const uint8_t* sp = &frame.rgba[((size_t)yy * frame.w + xx) * 4];
            if (!sp[3]) continue;
            uint8_t* dp = &canvas.rgba[((size_t)cy * canvas.w + cx) * 4];
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255;
        }
    }
}

}  // namespace pm3
