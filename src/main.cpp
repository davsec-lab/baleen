#include "../include/instrumentation.h"
#include "../include/language.h"
#include "../include/memory.h"
#include "../include/registry.h"
#include "../include/statistics.h"
#include "pin.H"

#include <fstream>
#include <iostream>

using namespace baleen;

// Global objects
std::ofstream memoryLog;
std::ofstream functionLog;
Registry registry;
LanguageTracker languageTracker;
Statistics statistics;

// Forward declarations for Pin callbacks
static void OnImageLoad(IMG img, VOID *v);
static void OnInstruction(INS ins, VOID *v);
static void OnFinish(INT32 code, VOID *v);

// Print usage information
INT32 Usage() {
  std::cerr << "Baleen - Memory profiling tool for Rust/C interop" << std::endl;
  std::cerr << std::endl;
  std::cerr << "This tool tracks memory allocations and accesses across"
            << std::endl;
  std::cerr << "Rust and C code boundaries." << std::endl;
  std::cerr << std::endl;
  std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
  return -1;
}

int main(int argc, char *argv[]) {
  // Initialize symbol processing (must be before PIN_Init)
  PIN_InitSymbols();

  // Initialize Pin
  if (PIN_Init(argc, argv)) {
    return Usage();
  }

  // Open log files
  memoryLog.open("memory.baleen");
  functionLog.open("functions.baleen");

  if (!memoryLog.is_open() || !functionLog.is_open()) {
    std::cerr << "Error: Could not open log files" << std::endl;
    return 1;
  }

  // Create memory tracker
  MemoryTracker memoryTracker(registry, statistics, languageTracker, memoryLog);

  // Create instrumentation manager
  Instrumentation instrumentation(memoryTracker, languageTracker, functionLog);
  Instrumentation::setInstance(&instrumentation);

  // Register Pin callbacks
  IMG_AddInstrumentFunction(Instrumentation::onImageLoad, nullptr);
  INS_AddInstrumentFunction(Instrumentation::instrumentInstruction, nullptr);
  PIN_AddFiniFunction(Instrumentation::onFinish, nullptr);

  std::cout << "Baleen Pintool initialized" << std::endl;
  std::cout << "Starting program instrumentation..." << std::endl;

  // Start the program
  PIN_StartProgram();

  return 0;
}

// Finalization callback
void OnFinish(INT32 code, VOID *v) {
  std::cout << "Program finished, generating reports..." << std::endl;

  // Print summary to log
  statistics.printSummary(memoryLog);
  memoryLog << "Instrumentation finished" << std::endl;
  memoryLog.close();
  functionLog.close();

  // Export CSV files
  statistics.exportToCSV("baleen-objects.csv", "baleen-allocs.csv");

  std::cout << "Reports generated:" << std::endl;
  std::cout << "  - baleen-objects.csv (object access statistics)" << std::endl;
  std::cout << "  - baleen-allocs.csv (allocation statistics)" << std::endl;
  std::cout << "  - memory.baleen (detailed memory log)" << std::endl;
  std::cout << "  - functions.baleen (instrumented functions log)" << std::endl;
}
