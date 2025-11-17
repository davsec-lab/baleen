#ifndef BALEEN_MEMORY_TRACKER_H
#define BALEEN_MEMORY_TRACKER_H

#include "language.h"
#include "pin.H"
#include "registry.h"
#include "statistics.h"
#include "types.h"

#include <fstream>
#include <map>

namespace baleen {

struct ReallocContext {
  ADDRINT addr;
  USIZE size;
};

struct MallocContext {
  Language lang;
  USIZE size;
};

class MemoryTracker {
public:
  MemoryTracker(Registry &reg, Statistics &stats, LanguageTracker &langTracker,
                std::ofstream &logFile);
  ~MemoryTracker();

  // Instrumentation callbacks
  void recordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr);
  void recordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr);

  // Baleen registration callback
  void beforeBaleen(ADDRINT addr, ADDRINT size, ADDRINT name);

  // Malloc callbacks
  void beforeMalloc(THREADID tid, USIZE size);
  void afterMalloc(THREADID tid, ADDRINT ret);

  // Realloc callbacks
  void beforeRealloc(THREADID tid, ADDRINT addr, USIZE size);
  void afterRealloc(THREADID tid, ADDRINT ret);

  // Free callback
  void beforeFree(ADDRINT addr);

private:
  Registry &registry;
  Statistics &statistics;
  LanguageTracker &languageTracker;
  std::ofstream &memLog;

  PIN_LOCK reallocLock;
  std::map<THREADID, ReallocContext> pendingRealloc;

  PIN_LOCK mallocLock;
  std::map<THREADID, MallocContext> pendingMalloc;

  // Maps object name to current start address
  std::map<std::string, ADDRINT> objectStarts;
};

} // namespace baleen

#endif // BALEEN_MEMORY_TRACKER_H
