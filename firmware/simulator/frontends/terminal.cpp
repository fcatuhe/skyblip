#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "core/model/ownship.h"
#include "core/units/units.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

struct RawTerminal {
    termios saved{};
    RawTerminal() {
        tcgetattr(STDIN_FILENO, &saved);
        termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    }
    ~RawTerminal() { tcsetattr(STDIN_FILENO, TCSANOW, &saved); }
};

uint32_t now_ms() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

const char* kPages[] = {"radar",      "nearby",    "6-pack", "g meter", "status",
                        "satellites", "radio log", "raw",    "capture", "self test"};
static_assert(sizeof(kPages) / sizeof(kPages[0]) == go::kPageCount,
              "a page with no name here prints past the end of the array");
const char* kAlarms[] = {"none", "ADVISORY"};
constexpr int kRecordLines = 6;
const char* kModes[] = {"dev", "demo", "training"};

void render(simulator::Simulator& simulator) {
    if (!simulator.panel_powered()) {
        std::printf("\033[2J\033[H[e-paper POWERED OFF]  ([o] power on, [q] quit)\n");
        std::fflush(stdout);
        return;
    }
    const go::Glass& canvas = simulator.panel();
    constexpr int step = 4;
    std::printf("\033[2J\033[H+----------------------------------------------------+\n");
    for (int y = 0; y < go::kGlassHeight; y += step) {
        std::printf("|");
        for (int x = 0; x < go::kGlassWidth; x += step) {
            bool black = false;
            for (int dy = 0; dy < step && !black; dy++)
                for (int dx = 0; dx < step && !black; dx++)
                    if (canvas.get_pixel(x + dx, y + dy)) black = true;
            std::printf("%c", black ? '#' : ' ');
        }
        std::printf("|\n");
    }
    std::printf("+----------------------------------------------------+\n");

    const bus::State& state = simulator.product().state();
    const model::Ownship& own = state.own;
    const char* glass = !simulator.panel_refreshing()       ? "idle"
                        : simulator.panel_refresh_is_full() ? "FULL"
                                                            : "partial";
    std::printf(" mode:%-8s page:%-6s backlight:%-3s glass:%-7s (%d presents)\n",
                kModes[static_cast<int>(simulator.mode())],
                kPages[static_cast<int>(simulator.product().screen().page())],
                simulator.backlight() ? "ON" : "off", glass, simulator.present_count());
    std::printf("  fix:%-6s sats:%2u  pos:%.5f,%.5f\n", own.fix_valid ? "3D" : "none",
                own.satellites, own.latitude_1e7 / 1e7, own.longitude_1e7 / 1e7);
    std::printf("  alt:%5dm  spd:%5.1fm/s  trk:%03u  vs:%+.1fm/s\n",
                to_metres(Millimetres(own.altitude_hae_mm)).v, own.speed_mm_s / 1000.0,
                to_degrees(CentiDegrees(own.track_cdeg)).v, own.climb_mm_s / 1000.0);
    std::printf("  batt:%u.%02uV %3u%%%s\n", state.power.battery.cell_mv / 1000,
                (state.power.battery.cell_mv % 1000) / 10, state.power.battery.percent,
                state.power.battery.charging ? " CHG" : "");
    std::printf("  traffic:%d  rx ok/bad:%u/%u  wait/type:%u/%u  tx ok/lost:%u/%u  ALARM:%s\n",
                state.traffic.count(), state.air.rx_ok, state.air.rx_bad, state.air.rx_wait,
                state.air.rx_type, state.air.tx_ok, state.air.tx_lost,
                kAlarms[traffic::to_number(state.alarm_level)]);
    // One line, because a log that only exists on a partition nobody can see is
    // a feature nobody notices is broken.
    const go::FlightLogService& log = simulator.product().flight_log();
    std::printf("  log:%-9s records:%-5u sessions:%u\n",
                !log.available() ? "no flash" : (log.recording() ? "RECORDING" : "idle"),
                log.records_written(), log.sessions_on_flash());
    std::printf(" 868 band, last bursts (phase in the second, channel, heard or not):\n");
    const simulator::Air& air = simulator.world().air();
    char line[160];
    for (int i = air.record_count() - kRecordLines; i < air.record_count(); i++) {
        if (i < 0) continue;
        air.format(i, line, sizeof(line));
        std::printf("   %s\n", line);
    }
    std::printf(
        " device: [p]age [m]enu [t]ouch pad [b]acklight [o]n/off   sensors: [f]ix [n]o-pps\n");
    std::printf(
        " alt a/z  speed s/x  track d/c  turn r/e/w=0   traffic: [g]+1 [j]+ALP-TAS [h]threat"
        " [k]clear   [q]uit\n");
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    simulator::Simulator simulator;
    if (simulator.setup() != Status::Ok) {
        std::printf("simulator setup failed\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--scenario=", 11) != 0) continue;
        if (!simulator.load_file(argv[i] + 11)) {
            std::printf("cannot load scenario %s\n", argv[i] + 11);
            return 1;
        }
    }

    uint32_t last_step_ms = now_ms();
    bool fix = true, pps = true, pad = false;
    int32_t altitude_m = 1000, speed_kt = 45, track_deg = 90, turn_dps = 0;

    RawTerminal raw;
    bool running = true;
    while (running) {
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1) {
            switch (c) {
                case 'q': running = false; break;
                case 'p': simulator.world().tap_pad(); break;
                case 'm': simulator.world().press_button(); break;
                case 't':
                    pad = !pad;
                    simulator.world().hold_pad(pad);
                    break;
                case 'b': simulator.product().screen().set_backlight(!simulator.backlight()); break;
                case 'o': simulator.product().screen().set_power(!simulator.panel_powered()); break;
                case 'f':
                    fix = !fix;
                    simulator.world().set_fix(fix);
                    break;
                case 'n':
                    pps = !pps;
                    simulator.world().set_pps_locked(pps);
                    break;
                case 'a': simulator.world().set_altitude_m(altitude_m += 100); break;
                case 'z': simulator.world().set_altitude_m(altitude_m -= 100); break;
                case 's': simulator.world().set_speed_kt(speed_kt += 5); break;
                case 'x':
                    simulator.world().set_speed_kt(speed_kt = speed_kt > 5 ? speed_kt - 5 : 0);
                    break;
                case 'd':
                    simulator.world().set_track_deg(track_deg = (track_deg + 15) % 360);
                    break;
                case 'c':
                    simulator.world().set_track_deg(track_deg = (track_deg + 345) % 360);
                    break;
                case 'r':
                    simulator.world().set_turn_dps(turn_dps = turn_dps < 24 ? turn_dps + 3 : 24);
                    break;
                case 'e':
                    simulator.world().set_turn_dps(turn_dps = turn_dps > -24 ? turn_dps - 3 : -24);
                    break;
                case 'w': simulator.world().set_turn_dps(turn_dps = 0); break;
                case 'g': simulator.world().add_aircraft(1500, 800, 50, 30, 250); break;
                case 'h': simulator.world().add_threat(); break;
                case 'j':
                    simulator.world().add_aircraft(1500, 800, 50, 30, 250, -1, -1,
                                                   protocol::System::Alptas);
                    break;
                case 'k': simulator.world().clear_aircraft(); break;
                default: break;
            }
        }
        // Slot edges are 5 ms wide, so the loop catches up in 5 ms steps
        // rather than jumping a whole frame and stepping over one.
        const uint32_t target_ms = now_ms();
        while (last_step_ms + simulator::Simulator::kStepMs <= target_ms) {
            last_step_ms += simulator::Simulator::kStepMs;
            simulator.step(last_step_ms);
        }
        render(simulator);
        usleep(50 * 1000);
    }
    const bus::State& state = simulator.product().state();
    std::printf("\nbye: traffic=%d rx=%u failures=%d\n", state.traffic.count(), state.air.rx_ok,
                simulator.world().failures());
    return 0;
}
