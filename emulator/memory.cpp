// The clock's memory. While the firmware runs (setup() and each loop()), its
// allocations come from a fixed arena carved the way the chip's allocator (umm_malloc)
// carves its heap: 8-byte blocks, 4 bytes of header per allocation, best fit. So a face
// that needs more than the clock has fails here too, and fragmentation adds up the
// same way. The firmware's own frames are held to the chip's 4 KB stack. Everything
// else (the core's host mock outside loop(), the screen server) keeps the host heap.
//
//   EMU_HEAP   arena size in bytes (default: the clock's free heap at rest, calibrated)
//   EMU_STACK  stack size in bytes (default 4096)
//
// Out of memory in `new` and a stack overflow end as on the chip: a panic, then a
// "Software/System restart". The log gets the cause and a backtrace with file:line.
#include <Arduino.h>
#include <cerrno>
#include <execinfo.h>
#include <map>
#include <new>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <user_interface.h>   // rst_info

extern "C" {
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
void* __libc_memalign(size_t, size_t);
void __libc_free(void*);
}

void emuSetNextReset(const char* reason);   // emulator.cpp
void emuRecordCrash(const char* reason, const char* detail, const String& trace);
extern "C" void custom_crash_callback(struct rst_info* info, uint32_t stack, uint32_t stack_end);   // src/crash.cpp

namespace {
constexpr size_t BLOCK = 8, HEADER = 4, MAX_BLOCKS = 16384;   // up to 128 KB
constexpr uint16_t NONE = 0xFFFF;
alignas(16) uint8_t arena[MAX_BLOCKS * BLOCK];
// Per run of blocks, kept at its first block: length, previous run, in use.
uint16_t runLen[MAX_BLOCKS], runPrev[MAX_BLOCKS];
bool runUsed[MAX_BLOCKS];
size_t blocks = 0, freeBlocks = 0, lowFree = 0;
bool fwRunning = false;   // allocations go to the arena

// GET :8080/heapmap: each live block's call chain, taken at allocation (only with
// EMU_HEAPMAP=1: unwinding every allocation is slow).
constexpr int SITE_FRAMES = 10;
void* site[MAX_BLOCKS][SITE_FRAMES];
int heapMap = -1;
bool tracing = false;
void noteSite(size_t i) {
  if (heapMap < 0) heapMap = getenv("EMU_HEAPMAP") && *getenv("EMU_HEAPMAP") == '1';
  if (!heapMap || tracing) return;
  tracing = true;   // backtrace() may allocate the first time: from the host heap
  bool was = fwRunning;
  fwRunning = false;
  void* f[SITE_FRAMES + 3];
  int n = backtrace(f, SITE_FRAMES + 3);
  fwRunning = was;
  tracing = false;
  // Return addresses, less one: the call itself, not the line after it.
  for (int k = 0; k < SITE_FRAMES; ++k) site[i][k] = k + 3 < n ? static_cast<char*>(f[k + 3]) - 1 : nullptr;   // past noteSite, arenaAlloc, malloc
}

void arenaInit() {
  if (blocks) return;
  const char* e = getenv("EMU_HEAP");
  size_t bytes = e && *e ? strtoul(e, nullptr, 10) : 25600;
  blocks = std::min(MAX_BLOCKS, std::max<size_t>(64, bytes / BLOCK));
  runLen[0] = blocks; runPrev[0] = NONE; runUsed[0] = false;
  freeBlocks = lowFree = blocks;
}

bool inArena(const void* p) { return p >= arena && p < arena + sizeof(arena); }
size_t indexOf(const void* p) { return (static_cast<const uint8_t*>(p) - arena) / BLOCK; }
size_t need(size_t size) { return std::max<size_t>(1, (size + HEADER + BLOCK - 1) / BLOCK); }

// Cuts run i to n blocks; the rest becomes a free run (merged with a free next run).
void split(size_t i, size_t n) {
  size_t len = runLen[i];
  if (len <= n) return;
  size_t rest = i + n, next = i + len;
  runLen[i] = n;
  runLen[rest] = len - n; runPrev[rest] = i; runUsed[rest] = false;
  if (next < blocks) {
    if (!runUsed[next]) {
      runLen[rest] += runLen[next];
      if (next + runLen[next] < blocks) runPrev[next + runLen[next]] = rest;
    } else {
      runPrev[next] = rest;
    }
  }
}

void* arenaAlloc(size_t size) {
  arenaInit();
  size_t n = need(size), best = NONE;
  for (size_t i = 0; i < blocks; i += runLen[i])
    if (!runUsed[i] && runLen[i] >= n && (best == NONE || runLen[i] < runLen[best])) best = i;
  if (best == NONE) return nullptr;
  split(best, n);
  runUsed[best] = true;
  freeBlocks -= n;
  lowFree = std::min(lowFree, freeBlocks);
  noteSite(best);
  return arena + best * BLOCK;
}

void arenaFree(void* p) {
  size_t i = indexOf(p);
  if (!runUsed[i]) return;
  runUsed[i] = false;
  freeBlocks += runLen[i];
  size_t next = i + runLen[i];
  if (next < blocks && !runUsed[next]) {   // merge with the next run
    runLen[i] += runLen[next];
    next = i + runLen[i];
  }
  if (next < blocks) runPrev[next] = i;
  size_t prev = runPrev[i];
  if (prev != NONE && !runUsed[prev]) {   // and with the previous one
    runLen[prev] += runLen[i];
    if (next < blocks) runPrev[next] = prev;
  }
}

size_t usable(void* p) { return runLen[indexOf(p)] * BLOCK - HEADER; }

void* arenaRealloc(void* p, size_t size) {
  size_t i = indexOf(p), n = need(size), len = runLen[i];
  if (n <= len) {   // shrink in place
    split(i, n);
    freeBlocks += len - n;
    return p;
  }
  size_t next = i + len;
  if (next < blocks && !runUsed[next] && len + runLen[next] >= n) {   // grow into the next run
    size_t got = runLen[next];
    runLen[i] += got;
    if (i + runLen[i] < blocks) runPrev[i + runLen[i]] = i;
    freeBlocks -= got;
    split(i, n);
    freeBlocks += len + got - n;
    lowFree = std::min(lowFree, freeBlocks);
    return p;
  }
  void* q = arenaAlloc(size);
  if (!q) return nullptr;
  memcpy(q, p, std::min(usable(p), size));
  arenaFree(p);
  return q;
}

size_t largestRun() {
  size_t best = 0;
  for (size_t i = 0; i < blocks; i += runLen[i])
    if (!runUsed[i]) best = std::max<size_t>(best, runLen[i]);
  return best;
}

// ---- The firmware's stack
// The firmware runs on a stack of its own, but only the firmware's frames are counted:
// it is built with -finstrument-functions (build.sh), and each function entry notes how
// deep it is. The host mock's own calls under it (getifaddrs, getaddrinfo, glibc's
// printf) are far deeper than their chip counterparts and are left out.
constexpr size_t HOST_STACK = 1 << 20;
size_t stackSize = 0, pageSize = 0, stackMaxUsed = 0;
uintptr_t stackTop = 0;          // where the firmware's stack starts (it grows down)
uint8_t* guard = nullptr;        // a page below the host-size stack, for runaway recursion
bool overflowing = false;
ucontext_t hostCtx, fwCtx;
sigjmp_buf hostJump;
int exitCode = -1;
void (*fwEntry)() = nullptr;
void (*firstEntry)() = nullptr;   // setup()
bool panicRequested = false;
uint8_t altStack[64 * 1024];

String lastTrace;   // the last backtraceToLog(), for the crash record

void backtraceToLog(int skip) {
  void* frames[48];
  int n = backtrace(frames, 48);
  char cmd[64 + 48 * 12];
  int at = snprintf(cmd, sizeof(cmd), "addr2line -f -C -p -e /proc/%d/exe", getpid());
  for (int i = skip; i < n; ++i) at += snprintf(cmd + at, sizeof(cmd) - at, " %p", frames[i]);
  bool was = fwRunning;
  fwRunning = false;   // addr2line's pipe is the host's, not the clock's
  fflush(stdout);
  lastTrace = "";
  if (FILE* f = popen(cmd, "r")) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      printf("  %s", line);
      if (strncmp(line, "?? ", 3) != 0 && !strstr(line, "__cyg_profile_func_enter")) lastTrace += line;
    }
    pclose(f);
  } else {
    backtrace_symbols_fd(frames + skip, n - skip, STDOUT_FILENO);
  }
  fflush(stdout);
  fwRunning = was;
}

// A panic on the chip: the next boot reports "Software/System restart".
// The core's reason codes for the firmware's crash record: 253 a stack smash, 254 a panic.
[[noreturn]] void chipPanic(const char* what, uint32_t reason) {
  fwRunning = false;
  char detail[120];
  snprintf(detail, sizeof(detail), "heap %u free, largest block %u, stack %u used of %u",
           unsigned(freeBlocks * BLOCK), unsigned(largestRun() * BLOCK),
           unsigned(stackMaxUsed), unsigned(stackSize));
  printf("emulator: panic: %s (%s)\n", what, detail);
  emuRecordCrash(what, detail, lastTrace);
  rst_info info{};
  info.reason = reason;
  custom_crash_callback(&info, 0, 0);   // as the core does: the firmware keeps its record
  emuSetNextReset("panic");
  exitCode = 1;
  siglongjmp(hostJump, 1);
}

void onSegv(int, siginfo_t* info, void*) {
  uint8_t* at = static_cast<uint8_t*>(info->si_addr);
  if (guard && at >= guard && at < guard + pageSize) {
    static const char msg[] = "emulator: runaway recursion (1 MB of stack)\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    backtraceToLog(2);
    chipPanic("stack overflow", 253);
  }
  signal(SIGSEGV, SIG_DFL);   // anything else: a real crash, as before
  raise(SIGSEGV);
}

void stackInit() {
  if (guard) return;
  const char* e = getenv("EMU_STACK");
  pageSize = sysconf(_SC_PAGESIZE);
  stackSize = e && *e ? strtoul(e, nullptr, 10) : 4096;
  guard = static_cast<uint8_t*>(mmap(nullptr, HOST_STACK + pageSize, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  mprotect(guard, pageSize, PROT_NONE);
  stack_t alt{};
  alt.ss_sp = altStack; alt.ss_size = sizeof(altStack);
  sigaltstack(&alt, nullptr);
  struct sigaction sa{};
  sa.sa_sigaction = onSegv;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sa, nullptr);
}

void trampoline() {
  stackTop = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  fwEntry();
}
}  // namespace

// Called at the entry of every firmware function (-finstrument-functions).
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void*, void*) {
  if (!fwRunning || overflowing) return;
  size_t used = stackTop - reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  if (used <= stackMaxUsed) return;
  stackMaxUsed = used;
  if (used > stackSize) {
    overflowing = true;
    printf("emulator: stack overflow (%u bytes of the chip's %u)\n", unsigned(used), unsigned(stackSize));
    backtraceToLog(1);
    chipPanic("stack overflow", 253);
  }
}
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void*, void*) {}

// Runs f (the firmware's setup() or loop()) on the firmware's stack, with the clock's heap.
void emuRunFirmware(void (*f)()) {
  arenaInit();
  stackInit();
  if (sigsetjmp(hostJump, 1)) {
    fwRunning = false;
    fflush(stdout);
    exit(exitCode);   // from the host stack: the core's atexit handler saves the flash image
  }
  if (panicRequested && f != firstEntry) {   // POST :8080/panic, in loop(), not setup()
    panicRequested = false;
    fwRunning = true;
    chipPanic("requested (POST /panic)", 254);
  }
  if (!firstEntry) firstEntry = f;
  fwEntry = f;
  getcontext(&fwCtx);
  fwCtx.uc_stack.ss_sp = guard + pageSize;
  fwCtx.uc_stack.ss_size = HOST_STACK;
  fwCtx.uc_link = &hostCtx;
  makecontext(&fwCtx, trampoline, 0);
  fwRunning = true;
  swapcontext(&hostCtx, &fwCtx);
  fwRunning = false;
}

// POST :8080/panic: the next loop() panics, to try the crash record and crash-loop protection.
void emuRequestPanic() { panicRequested = true; }

// exit() for code that may be on the clock's stack (ESP.restart()): leaves through the host.
void emuExit(int code) {
  if (!fwRunning) exit(code);
  exitCode = code;
  siglongjmp(hostJump, 1);
}

// GET :8080/stats?resetlow=1: the lowest free heap starts again from now.
void emuResetLow() { arenaInit(); lowFree = freeBlocks; }

// The memory part of GET :8080/stats.
String emuMemoryJson() {
  arenaInit();
  char b[220];
  snprintf(b, sizeof(b),
           "{\"heap\":{\"size\":%u,\"free\":%u,\"low\":%u,\"maxBlock\":%u},"
           "\"stack\":{\"size\":%u,\"maxUsed\":%u}}",
           unsigned(blocks * BLOCK), unsigned(freeBlocks * BLOCK), unsigned(lowFree * BLOCK),
           unsigned(largestRun() * BLOCK), unsigned(stackSize), unsigned(stackMaxUsed));
  return b;
}

// A frame in the firmware's own sources (src/), not a library's src/ folder.
bool firmwareFile(const std::string& w) {
  for (const char* f : {"/main.cpp:", "/faces.cpp:", "/integrations.cpp:", "/weather.cpp:", "/crash.cpp:",
                        "/config.h:", "/weather.h:", "/faces.h:", "/integrations.h:", "/ascii.cpp:"})
    if (w.find(f) != std::string::npos && w.find("/src/") != std::string::npos) return true;
  return false;
}

// GET :8080/heapmap: the live blocks grouped by the first firmware function (src/) in
// their call chain, largest first: "bytes blocks function (file:line) <- the caller".
String emuHeapMap(bool raw) {
  arenaInit();
  if (heapMap != 1) return "Start the emulator with EMU_HEAPMAP=1\n";
  // Every distinct address, through one addr2line run.
  std::vector<void*> addrs;
  for (size_t i = 0; i < blocks; i += runLen[i])
    if (runUsed[i]) for (void* a : site[i]) if (a) addrs.push_back(a);
  std::sort(addrs.begin(), addrs.end());
  addrs.erase(std::unique(addrs.begin(), addrs.end()), addrs.end());
  std::map<void*, std::string> where;
  bool was = fwRunning;
  fwRunning = false;
  for (size_t at = 0; at < addrs.size(); at += 200) {
    std::string cmd = "addr2line -f -C -p -i -e /proc/" + std::to_string(getpid()) + "/exe";
    char a[24];
    size_t end = std::min(addrs.size(), at + 200);
    for (size_t k = at; k < end; ++k) { snprintf(a, sizeof(a), " %p", addrs[k]); cmd += a; }
    if (FILE* f = popen(cmd.c_str(), "r")) {
      // With -i an address prints the function inlined there first, then " (inlined by) ..."
      // lines out to the function the code sits in: that outermost one is kept.
      char* line = nullptr;   // getline, not fgets: template names run past any fixed buffer
      size_t cap = 0, k = at;
      while (getline(&line, &cap, f) > 0) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t in = s.find("(inlined by) ");
        if (in != std::string::npos && in < 4) {
          if (k > at) where[addrs[k - 1]] = s.substr(in + 13);
        } else if (k < end) {
          where[addrs[k++]] = s;
        }
      }
      free(line);
      pclose(f);
    }
  }
  if (raw) {   // ?raw=1: every live block with its whole call chain
    std::string out;
    for (size_t i = 0; i < blocks; i += runLen[i]) {
      if (!runUsed[i]) continue;
      out += std::to_string(runLen[i] * BLOCK) + " B\n";
      for (int k = 0; k < SITE_FRAMES && site[i][k]; ++k) {
        char a[24];
        snprintf(a, sizeof(a), "%p ", site[i][k]);
        out += "    " + std::string(a) + where[site[i][k]].substr(0, 140) + "\n";
      }
      if (out.size() > 48000) { out += "...\n"; break; }
    }
    fwRunning = was;
    return String(out.c_str());
  }
  std::map<std::string, std::pair<size_t, size_t>> groups;   // site -> bytes, blocks
  for (size_t i = 0; i < blocks; i += runLen[i]) {
    if (!runUsed[i]) continue;
    std::string key = "(no firmware frame)", lib;
    for (int k = 0; k < SITE_FRAMES && site[i][k]; ++k) {
      const std::string& w = where[site[i][k]];
      if (firmwareFile(w)) {
        // The function and its file, without the line: one row per function.
        std::string file = w.substr(w.rfind('/') + 1);
        key = w.substr(0, w.find(" at ")) + " (" + file.substr(0, file.find(':')) + ")";
        if (k + 1 < SITE_FRAMES && site[i][k + 1]) {
          const std::string& up = where[site[i][k + 1]];
          key += " <- " + up.substr(0, up.find('('));
        }
        break;
      }
      if (lib.empty() && w.find("operator new") == std::string::npos && w.find("malloc") == std::string::npos &&
          w.find("realloc") == std::string::npos && w.find("arena") == std::string::npos)
        lib = w.substr(0, w.find('('));
    }
    if (key == "(no firmware frame)" && !lib.empty()) key += " " + lib;
    groups[key].first += runLen[i] * BLOCK;
    groups[key].second++;
  }
  fwRunning = was;
  std::vector<std::pair<std::pair<size_t, size_t>, std::string>> rows;
  for (auto& g : groups) rows.push_back({g.second, g.first});
  std::sort(rows.rbegin(), rows.rend());
  std::string out;
  char head[96];
  snprintf(head, sizeof(head), "%u bytes in use of %u\n", unsigned((blocks - freeBlocks) * BLOCK), unsigned(blocks * BLOCK));
  out += head;
  for (auto& r : rows) {
    snprintf(head, sizeof(head), "%6u %4u ", unsigned(r.first.first), unsigned(r.first.second));
    out += head + r.second + "\n";
  }
  return String(out.c_str());
}

// What the core reports on the chip, from the arena and the stack.
uint32_t EspClass::getFreeHeap() { arenaInit(); return freeBlocks * BLOCK; }
uint32_t EspClass::getMaxFreeBlockSize() { arenaInit(); return largestRun() * BLOCK; }
uint32_t EspClass::getFreeContStack() { return stackSize - stackMaxUsed; }
void EspClass::getHeapStats(uint32_t* hfree, uint32_t* hmax, uint8_t* hfrag) {
  uint32_t f = getFreeHeap(), m = getMaxFreeBlockSize();
  if (hfree) *hfree = f;
  if (hmax) *hmax = m;
  if (hfrag) *hfrag = f ? 100 - m * 100 / f : 0;
}
void EspClass::getHeapStats(uint32_t* hfree, uint16_t* hmax, uint8_t* hfrag) {
  uint32_t m32;
  getHeapStats(hfree, &m32, hfrag);
  if (hmax) *hmax = std::min<uint32_t>(m32, 0xFFFF);
}

// ---- The C and C++ allocators, routed by who is running and whose pointer it is.
// A block of 64 KB or more cannot exist on the chip (80 KB of RAM in all): it is the host
// mock's own (a UDP socket takes 128 KB) and stays on the host heap.
constexpr size_t HOST_ONLY = 64 * 1024;
bool toArena(size_t n) { return fwRunning && n < HOST_ONLY; }

extern "C" {
void* malloc(size_t n) { return toArena(n) ? arenaAlloc(n) : __libc_malloc(n); }
void free(void* p) {
  if (inArena(p)) arenaFree(p);
  else __libc_free(p);
}
void* calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size) return nullptr;
  if (!toArena(n * size)) return __libc_calloc(n, size);
  void* p = arenaAlloc(n * size);
  if (p) memset(p, 0, n * size);
  return p;
}
void* realloc(void* p, size_t n) {
  if (!p) return malloc(n);
  if (inArena(p)) return arenaRealloc(p, n);
  return __libc_realloc(p, n);
}
void* memalign(size_t align, size_t n) {
  return toArena(n) && align <= BLOCK ? arenaAlloc(n) : __libc_memalign(align, n);
}
int posix_memalign(void** out, size_t align, size_t n) {
  *out = memalign(align, n);
  return *out ? 0 : ENOMEM;
}
void* aligned_alloc(size_t align, size_t n) { return memalign(align, n); }
}

// On the chip, `new` that cannot allocate panics (the core is built without exceptions).
void* operator new(size_t n) {
  void* p = malloc(n);
  if (!p) {
    char what[64];
    snprintf(what, sizeof(what), "out of memory (new of %u bytes)", unsigned(n));
    printf("emulator: %s\n", what);
    backtraceToLog(1);
    chipPanic(what, 254);
  }
  return p;
}
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
