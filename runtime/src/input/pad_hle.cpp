// vpad / vpadbase / padscore HLE. The GamePad (VPAD channel 0) is fed from input::gamepad();
// no Wii Remotes / Pro Controllers are reported (WPADProbe -> no controller).
#include <algorithm>
#include <cmath>

#include "../hle.h"
#include "../log.h"
#include "input.h"

namespace {
uint32_t g_last_vpad_hold = 0;
uint32_t g_last_kpad_hold = 0;
uint32_t g_sampling_cb = 0;
bool g_last_touch = false;

void wf(uint32_t a, float v) { mem::wf32(a, v); }
}  // namespace

// int32 VPADRead(chan, VPADStatus* buf, uint32 count, int32* err)
HLE_EXPORT(vpad, VPADRead) {
    uint32_t chan = ARG(0), buf = ARG(1), count = ARG(2), err = ARG(3);
    if (chan != 0 || !count) {
        if (err) mem::w32(err, (uint32_t)-2);
        RET(0);
        return;
    }
    input::PadState s = input::gamepad();
    memset(mem::host(buf), 0, 0xAC);
    uint32_t hold = s.buttons;
    // stick emulation bits (games use them for menu navigation)
    auto stick_bits = [](float x, float y, uint32_t base) {
        uint32_t b = 0;
        if (x < -0.5f) b |= base << 3;   // LEFT
        if (x > 0.5f) b |= base << 2;    // RIGHT
        if (y > 0.5f) b |= base << 1;    // UP
        if (y < -0.5f) b |= base;        // DOWN
        return b;
    };
    hold |= stick_bits(s.lx, s.ly, 0x08000000) | stick_bits(s.rx, s.ry, 0x00800000);
    if (hold != g_last_vpad_hold || s.touch_down != g_last_touch) {
        LOG("input probe VPAD: hold=%08x trigger=%08x release=%08x L=%+.2f,%+.2f touch=%d @%u,%u", hold,
            s.pressed, s.released, s.lx, s.ly, s.touch_down, (unsigned)(s.touch_x * 1280),
            (unsigned)(s.touch_y * 720));
        g_last_vpad_hold = hold;
        g_last_touch = s.touch_down;
    }
    mem::w32(buf + 0x00, hold);
    mem::w32(buf + 0x04, s.pressed);
    mem::w32(buf + 0x08, s.released);
    wf(buf + 0x0C, s.lx);
    wf(buf + 0x10, s.ly);
    wf(buf + 0x14, s.rx);
    wf(buf + 0x18, s.ry);
    wf(buf + 0x30, 1.0f);  // accXY
    input::Motion m;
    if (input::motion(&m)) {  // controller gyro / accelerometer
        for (int k = 0; k < 3; k++) {
            wf(buf + 0x1C + 4 * k, m.acc[k]);
            wf(buf + 0x38 + 4 * k, m.gyro_change[k]);
            wf(buf + 0x44 + 4 * k, m.orientation[k]);
        }
        wf(buf + 0x28, m.acc_magnitude);
        wf(buf + 0x2C, m.acc_acceleration);
        for (int k = 0; k < 9; k++) wf(buf + 0x6C + 4 * k, m.dir[k]);
    } else {
        wf(buf + 0x20, -1.0f);  // acc: pad lying flat
        wf(buf + 0x28, 1.0f);   // acc magnitude
        // dir: identity orientation matrix (x, y, z axes)
        wf(buf + 0x6C, 1.0f);
        wf(buf + 0x7C, 1.0f);
        wf(buf + 0x8C, 1.0f);
    }
    const uint16_t tx = (uint16_t)std::clamp(s.touch_x * 1280.0f, 0.0f, 1279.0f);
    const uint16_t ty = (uint16_t)std::clamp(s.touch_y * 720.0f, 0.0f, 719.0f);
    // VPAD touch samples: normal, first filtered and second filtered coordinates.
    for (uint32_t off : {0x52u, 0x5Au, 0x62u}) {
        mem::w16(buf + off, tx);
        mem::w16(buf + off + 2, ty);
        mem::w16(buf + off + 4, s.touch_down ? 1 : 0);
        mem::w16(buf + off + 6, 0);  // valid sample
    }
    mem::w8(buf + 0xA0, 0x40);  // slide volume
    mem::w8(buf + 0xA1, 6);     // battery full
    if (err) mem::w32(err, 0);
    RET(1);
}
// int32 VPADControlMotor(chan, uint8* pattern, uint8 lengthInBits): rumble, see input::push_rumble
HLE_EXPORT(vpad, VPADControlMotor) {
    uint32_t bits = std::min<uint32_t>(ARG(2) & 0xFF, 120);
    if (ARG(0) != 0 || !ARG(1) || !bits) {
        input::clear_rumble();
        RET(0);
        return;
    }
    RET(input::push_rumble(mem::host(ARG(1)), (int)bits) ? 0 : (uint32_t)-1);
}
HLE_EXPORT(vpad, VPADStopMotor) { input::clear_rumble(); }
HLE_EXPORT(vpad, VPADDisableGyroDirRevise) {}
HLE_EXPORT(vpad, VPADEnableGyroAccRevise) {}
HLE_EXPORT(vpad, VPADSetGyroAccReviseParam) {}
HLE_EXPORT(vpad, VPADSetGyroDirection) {}
HLE_EXPORT(vpad, VPADSetGyroZeroDriftMode) {}
HLE_EXPORT(vpad, VPADSetSamplingCallback) {
    uint32_t prev = g_sampling_cb;
    g_sampling_cb = ARG(1);
    RET(prev);
}
// void VPADGetTPCalibratedPoint(chan, VPADTPData* out, const VPADTPData* in)
HLE_EXPORT(vpad, VPADGetTPCalibratedPoint) { memmove(mem::host(ARG(1)), mem::host(ARG(2)), 8); }
HLE_EXPORT(vpadbase, VPADBASEGetHeadphoneStatus) { RET(0); }

// ---- padscore: no remotes connected ---------------------------------------------------------------
HLE_EXPORT(padscore, KPADInitEx) {}
HLE_EXPORT(padscore, KPADGetMplsWorkSize) { RET(0x5FE0); }
HLE_EXPORT(padscore, KPADSetMplsWorkarea) {}
// int32 KPADReadEx(chan, KPADStatus* buf, uint32 count, int32* err)
HLE_EXPORT(padscore, KPADReadEx) {
    const uint32_t chan = ARG(0), buf = ARG(1), count = ARG(2), err = ARG(3);
    if (chan != 0 || !count) {
        if (err) mem::w32(err, (uint32_t)-2);  // invalid controller / request
        RET(0);
        return;
    }

    // Some BotW dialogue-choice code polls the Pro Controller (KPAD) path instead of VPAD.
    // Keep D-pad directions digital in VPAD (weapon selection/gameplay), but expose them as a
    // Pro Controller stick here for UI lists that only navigate from analog input.
    input::PadState s = input::gamepad();
    float kpad_lx = s.lx, kpad_ly = s.ly;
    if (std::abs(kpad_lx) < 0.15f)
        kpad_lx = ((s.buttons & input::BTN_RIGHT) ? 1.0f : 0.0f) - ((s.buttons & input::BTN_LEFT) ? 1.0f : 0.0f);
    if (std::abs(kpad_ly) < 0.15f)
        kpad_ly = ((s.buttons & input::BTN_UP) ? 1.0f : 0.0f) - ((s.buttons & input::BTN_DOWN) ? 1.0f : 0.0f);
    // BotW polls KPAD as a Wii U Pro Controller for its dialogue and choice UI.
    // Mirror face buttons and shoulders here as well as in VPAD. Without this,
    // navigation reached KPAD but confirm/cancel and the other actions vanished.
    auto map_buttons = [](uint32_t buttons) {
        uint32_t out = 0;
        if (buttons & input::BTN_UP) out |= 0x00000001;
        if (buttons & input::BTN_LEFT) out |= 0x00000002;
        if (buttons & input::BTN_ZR) out |= 0x00000004;
        if (buttons & input::BTN_X) out |= 0x00000008;
        if (buttons & input::BTN_A) out |= 0x00000010;
        if (buttons & input::BTN_Y) out |= 0x00000020;
        if (buttons & input::BTN_B) out |= 0x00000040;
        if (buttons & input::BTN_ZL) out |= 0x00000080;
        if (buttons & input::BTN_R) out |= 0x00000200;
        if (buttons & input::BTN_PLUS) out |= 0x00000400;
        if (buttons & input::BTN_HOME) out |= 0x00000800;
        if (buttons & input::BTN_MINUS) out |= 0x00001000;
        if (buttons & input::BTN_L) out |= 0x00002000;
        if (buttons & input::BTN_DOWN) out |= 0x00004000;
        if (buttons & input::BTN_RIGHT) out |= 0x00008000;
        if (buttons & input::BTN_STICK_R) out |= 0x00010000;
        if (buttons & input::BTN_STICK_L) out |= 0x00020000;
        return out;
    };
    uint32_t hold = map_buttons(s.buttons);
    if (s.buttons & input::BTN_UP) hold |= 0x00000001;
    if (s.buttons & input::BTN_LEFT) hold |= 0x00000002;
    if (s.buttons & input::BTN_DOWN) hold |= 0x00004000;
    if (s.buttons & input::BTN_RIGHT) hold |= 0x00008000;
    if (kpad_lx < -0.5f) hold |= 0x00040000;
    if (kpad_lx > 0.5f) hold |= 0x00080000;
    if (kpad_ly > 0.5f) hold |= 0x00200000;
    if (kpad_ly < -0.5f) hold |= 0x00100000;
    if (s.rx < -0.5f) hold |= 0x00400000;
    if (s.rx > 0.5f) hold |= 0x00800000;
    if (s.ry > 0.5f) hold |= 0x02000000;
    if (s.ry < -0.5f) hold |= 0x01000000;

    uint32_t pressed = 0, released = 0;
    auto map_edges = [](uint32_t edges) {
        uint32_t out = 0;
        if (edges & input::BTN_UP) out |= 0x00000001;
        if (edges & input::BTN_LEFT) out |= 0x00000002;
        if (edges & input::BTN_ZR) out |= 0x00000004;
        if (edges & input::BTN_X) out |= 0x00000008;
        if (edges & input::BTN_A) out |= 0x00000010;
        if (edges & input::BTN_Y) out |= 0x00000020;
        if (edges & input::BTN_B) out |= 0x00000040;
        if (edges & input::BTN_ZL) out |= 0x00000080;
        if (edges & input::BTN_R) out |= 0x00000200;
        if (edges & input::BTN_PLUS) out |= 0x00000400;
        if (edges & input::BTN_HOME) out |= 0x00000800;
        if (edges & input::BTN_MINUS) out |= 0x00001000;
        if (edges & input::BTN_L) out |= 0x00002000;
        if (edges & input::BTN_DOWN) out |= 0x00004000;
        if (edges & input::BTN_RIGHT) out |= 0x00008000;
        if (edges & input::BTN_STICK_R) out |= 0x00010000;
        if (edges & input::BTN_STICK_L) out |= 0x00020000;
        if (edges & 0x10000000) out |= 0x00200000;
        if (edges & 0x08000000) out |= 0x00100000;
        if (edges & 0x40000000) out |= 0x00040000;
        if (edges & 0x20000000) out |= 0x00080000;
        if (edges & 0x01000000) out |= 0x02000000;
        if (edges & 0x00800000) out |= 0x01000000;
        if (edges & 0x04000000) out |= 0x00400000;
        if (edges & 0x02000000) out |= 0x00800000;
        return out;
    };
    pressed = map_edges(s.pressed);
    released = map_edges(s.released);
    // The D-pad direction mirrored into Pro left-stick input has its own emulated-stick edge.
    if (s.pressed & input::BTN_UP) pressed |= 0x00200000;
    if (s.pressed & input::BTN_DOWN) pressed |= 0x00100000;
    if (s.pressed & input::BTN_LEFT) pressed |= 0x00040000;
    if (s.pressed & input::BTN_RIGHT) pressed |= 0x00080000;
    if (s.released & input::BTN_UP) released |= 0x00200000;
    if (s.released & input::BTN_DOWN) released |= 0x00100000;
    if (s.released & input::BTN_LEFT) released |= 0x00040000;
    if (s.released & input::BTN_RIGHT) released |= 0x00080000;
    if (hold != g_last_kpad_hold)
        LOG("input probe KPAD: hold=%08x trigger=%08x release=%08x ProLeft=%d,%d", hold, pressed, released,
            (int16_t)std::lround(kpad_lx * 2047), (int16_t)std::lround(kpad_ly * 2047));
    g_last_kpad_hold = hold;

    if (!(hold | released)) {
        if (err) mem::w32(err, (uint32_t)-1);  // no samples
        RET(0);
        return;
    }

    memset(mem::host(buf), 0, 0xF0);  // sizeof KPADStatus
    mem::w8(buf + 0x5C, 0x1F);       // WPAD_EXT_PRO_CONTROLLER
    mem::w8(buf + 0x5F, 0x16);       // WPAD_FMT_PRO_CONTROLLER
    mem::w32(buf + 0x60, hold);
    mem::w32(buf + 0x64, pressed);
    mem::w32(buf + 0x68, released);
    // KPAD Pro Controller sticks are signed 16-bit values in [-2048, 2047], unlike VPAD's floats.
    auto stick = [](float v) { return (uint16_t)(int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 2047.0f); };
    mem::w16(buf + 0x6C, stick(kpad_lx));
    mem::w16(buf + 0x6E, stick(kpad_ly));
    mem::w16(buf + 0x74, stick(s.rx));
    mem::w16(buf + 0x76, stick(s.ry));
    if (err) mem::w32(err, 0);
    RET(1);
}
// int32 WPADProbe(chan, uint32* type)
HLE_EXPORT(padscore, WPADProbe) {
    if (ARG(1)) mem::w32(ARG(1), 253);  // WPAD_DEV_NOT_FOUND
    RET((uint32_t)-1);                  // WPAD_ERR_NO_CONTROLLER
}
HLE_EXPORT(padscore, WPADControlMotor) {}
HLE_EXPORT(padscore, WPADControlSpeaker) { RET((uint32_t)-1); }
HLE_EXPORT(padscore, WPADCanSendStreamData) { RET(0); }
HLE_EXPORT(padscore, WPADSendStreamData) { RET((uint32_t)-1); }
HLE_EXPORT(padscore, WPADStartSyncDevice) { RET(0); }
HLE_EXPORT(padscore, WPADEnableURCC) {}
HLE_EXPORT(padscore, WENCGetEncodeData) { RET(0); }
