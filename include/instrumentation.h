#ifndef BALEEN_INSTRUMENTATION_H
#define BALEEN_INSTRUMENTATION_H

#include "language.h"
#include "memory.h"
#include "pin.H"

#include <fstream>

namespace baleen {

class Instrumentation {
public:
  Instrumentation(MemoryTracker &memTracker, LanguageTracker &langTracker,
                  std::ofstream &functionLog);

  // Pin callback for instruction instrumentation
  static void instrumentInstruction(INS ins, VOID *v);

  // Pin callback for image loading
  static void onImageLoad(IMG img, VOID *v);

  // Pin callback for program finish
  static void onFinish(INT32 code, VOID *v);

  // Set the singleton instance (for static callbacks)
  static void setInstance(Instrumentation *inst);

private:
  MemoryTracker &memoryTracker;
  LanguageTracker &languageTracker;
  std::ofstream &log;

  // Helper to instrument a specific routine
  template <typename... Args>
  void instrumentRoutine(IMG img, const char *routineName, IPOINT ipoint,
                         AFUNPTR analysisFunc, Args... args);

  // Instance instrumentation methods
  void doInstructionInstrumentation(INS ins);
  void doImageInstrumentation(IMG img);
  void doFinalization(INT32 code);

  // Singleton for static callbacks
  static Instrumentation *instance;
};

} // namespace baleen

#endif // BALEEN_INSTRUMENTATION_H
