// The simulated ST7789 behind the emulator's real TFT_eSPI (tft_host/), and the clock's
// drawing speed.
//
// Panel: the bytes TFT_eSPI sends are decoded as the controller would: CASET/RASET set
// the window, RAMWR streams RGB565 pixels into it (big-endian), INVON/INVOFF invert.
// The panel is an IPS that needs INVON for true colours, as on the clock, so pixels
// written while inversion is off show inverted. emuPanel[] is what the glass shows.
//
// Speed: the PC runs the firmware hundreds of times faster than the ESP8266, and its
// floats are hardware where the chip's are software. Each call from TFT_eSPI charges
// the clock's time and sleeps it off:
//   - the bytes, at SPI_FREQUENCY (exact: the SPI clock does not follow the CPU);
//   - the firmware's own time on the PC since the previous call, times CPU_FACTOR
//     (which scales with 160 MHz / F_CPU);
//   - faces.cpp's path loops (the line font, icons, custom paths): float maths, which
//     the ESP8266 does in software. Timing them on the PC varies 2x from run to run,
//     so they count their work instead (EMU_WORK, estimated ESP8266 cycles), charged
//     at F_CPU times WORK_SCALE.
// Time between draws (the loop's delay) is not charged: a gap of over IDLE_US starts
// afresh. The constants come from the clock's measured draw times (/status drawMs) at
// 160 MHz: emulator/calibrate.py fits them again.
// EMU_TIMING=0 turns it off (draws are instant).
#include <Arduino.h>
#include <time.h>

uint16_t emuPanel[240 * 240];

namespace {
constexpr int PW = 240, PH = 240;   // the visible glass (the controller has 240x320 of RAM)

// Clock time per PC time at 160 MHz, and real cycles per counted cycle (calibrate.py).
constexpr double CPU_FACTOR_160 = 6.4, WORK_SCALE = 1.0;
constexpr double IDLE_US = 5000;

uint8_t cmd = 0;
int argIndex = 0;
uint8_t args[4];
bool dataMode = true, inverted = false, pendingHigh = false;
uint8_t highByte = 0;
int xs = 0, xe = PW - 1, ys = 0, ye = PH - 1, cx = 0, cy = 0;

void put(uint16_t c) {
  if (cx < PW && cy < PH && cx >= 0 && cy >= 0) emuPanel[cy * PW + cx] = inverted ? c : uint16_t(~c);
  if (++cx > xe) { cx = xs; if (++cy > ye) cy = ys; }
}

void command(uint8_t c) {
  cmd = c; argIndex = 0; pendingHigh = false;
  if (c == 0x20) inverted = false;        // INVOFF
  else if (c == 0x21) inverted = true;    // INVON
  else if (c == 0x2C) { cx = xs; cy = ys; }   // RAMWR: from the window's start
}

void data(uint8_t b) {
  if (cmd == 0x2C || cmd == 0x3C) {        // RAMWR / RAMWRC
    if (!pendingHigh) { highByte = b; pendingHigh = true; return; }
    pendingHigh = false;
    put(uint16_t(highByte << 8 | b));
    return;
  }
  if (cmd != 0x2A && cmd != 0x2B) return;
  if (argIndex < 4) args[argIndex++] = b;
  if (argIndex == 4) {
    int a = args[0] << 8 | args[1], e = args[2] << 8 | args[3];
    if (cmd == 0x2A) { xs = a; xe = e; } else { ys = a; ye = e; }
  }
}

// ---- Timing
double nowUs() {   // CLOCK_MONOTONIC: vDSO, so cheap enough to read twice per call
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

// One draw: calls with gaps under IDLE_US between them. The firmware works between
// calls, so the gaps are its CPU time (single thread; the loop's delay() ends a burst).
struct Burst { double cpuUs = 0, workCycles = 0, spiUs = 0, clockUs = 0; uint32_t calls = 0; };

struct Clock {
  bool on;
  double cpuFactor, workUs, bitUs, debtUs = 0, lastExit = -1e18, pendingCycles = 0;
  Burst cur, done[8];
  int doneCount = 0;
  Clock() {
    const char* t = getenv("EMU_TIMING");
    on = !(t && strcmp(t, "0") == 0);
    double scale = 160e6 / double(F_CPU);
    cpuFactor = CPU_FACTOR_160 * scale;
    workUs = WORK_SCALE * 1e6 / double(F_CPU);   // per counted cycle
    bitUs = 1e6 / double(SPI_FREQUENCY);
  }
  void close() {
    if (!cur.calls) return;
    for (int i = 7; i > 0; --i) done[i] = done[i - 1];
    done[0] = cur;
    doneCount = std::min(doneCount + 1, 8);
    cur = Burst();
  }
} clk;

}  // namespace

void emuScreenPush();   // emulator.cpp: the screen page sees a slow draw as it happens

// One TFT_eSPI call that sent `bytes` bytes.
void emuCharge(uint32_t bytes) {
  double now = nowUs(), gap = now - clk.lastExit;
  if (gap >= IDLE_US) { clk.close(); gap = 0; }
  double spi = bytes * 8 * clk.bitUs, cost = gap * clk.cpuFactor + spi + clk.pendingCycles * clk.workUs;
  clk.cur.cpuUs += gap; clk.cur.workCycles += clk.pendingCycles; clk.pendingCycles = 0;
  clk.cur.spiUs += spi; clk.cur.clockUs += cost; clk.cur.calls++;
  clk.debtUs += cost;
  if (!clk.on) clk.debtUs = 0;
  if (clk.debtUs >= 1000) {
    double until = now + clk.debtUs;
    clk.debtUs = 0;
    for (double t = now; t < until; t = nowUs()) {
      double slice = std::min(until - t, 10000.0);
      timespec ts{0, long(slice * 1000)};
      nanosleep(&ts, nullptr);
      emuScreenPush();
    }
  }
  clk.lastExit = nowUs();   // the decoder's and the sleep's time are not the firmware's
}

// faces.cpp's EMU_WORK: the path loops' work, in estimated ESP8266 cycles.
void emuWork(uint32_t cycles) { clk.pendingCycles += cycles; }

// The last draws (newest first): what they cost on the PC and on the modelled clock.
String emuTimingJson(bool reset) {
  if (nowUs() - clk.lastExit >= IDLE_US) clk.close();
  String out = "{\"on\":" + String(clk.on ? "true" : "false") + ",\"fcpuMHz\":" + String(int(F_CPU / 1000000)) +
               ",\"cpuFactor\":" + String(clk.cpuFactor, 2) + ",\"workUs\":" + String(clk.workUs, 5) + ",\"draws\":[";
  for (int i = 0; i < clk.doneCount; ++i) {
    const Burst& b = clk.done[i];
    char e[160];
    snprintf(e, sizeof(e), "%s{\"pcCpuUs\":%.0f,\"workCycles\":%.0f,\"spiUs\":%.0f,\"calls\":%u,\"clockMs\":%.1f}",
             i ? "," : "", b.cpuUs, b.workCycles, b.spiUs, (unsigned)b.calls, b.clockUs / 1000);
    out += e;
  }
  if (reset) clk.doneCount = 0;
  return out + "]}";
}

void emuTftDc(bool d) { dataMode = d; }

void emuTftByte(uint8_t b) {
  if (dataMode) data(b); else command(b);
  emuCharge(1);
}

void emuTftWord(uint16_t w) {
  if (dataMode) { data(w >> 8); data(w & 0xff); }
  else { command(w >> 8); command(w & 0xff); }
  emuCharge(2);
}

void emuTftFill(uint16_t color, uint32_t len) {
  for (uint32_t i = 0; i < len; ++i) { data(color >> 8); data(color & 0xff); }
  emuCharge(len * 2);
}

void emuTftPixels(const uint16_t* p, uint32_t len, bool swap) {
  for (uint32_t i = 0; i < len; ++i) {
    uint16_t v = swap ? uint16_t(p[i] >> 8 | p[i] << 8) : p[i];
    data(v >> 8); data(v & 0xff);
  }
  emuCharge(len * 2);
}
