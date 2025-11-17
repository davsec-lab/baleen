#include "../include/instrumentation.h"
#include <iostream>

namespace baleen {

// Static member initialization
Instrumentation *Instrumentation::instance = nullptr;

Instrumentation::Instrumentation(MemoryTracker &memTracker,
                                 LanguageTracker &langTracker,
                                 std::ofstream &functionLog)
    : memoryTracker(memTracker), languageTracker(langTracker),
      log(functionLog) {}

void Instrumentation::setInstance(Instrumentation *inst) { instance = inst; }

// Static callback wrappers
void Instrumentation::instrumentInstruction(INS ins, VOID *v) {
  if (instance) {
    instance->doInstructionInstrumentation(ins);
  }
}

void Instrumentation::onImageLoad(IMG img, VOID *v) {
  if (instance) {
    instance->doImageInstrumentation(img);
  }
}

void Instrumentation::onFinish(INT32 code, VOID *v) {
  if (instance) {
    instance->doFinalization(code);
  }
}

// Helper template implementation
template <typename... Args>
void Instrumentation::instrumentRoutine(IMG img, const char *routineName,
                                        IPOINT ipoint, AFUNPTR analysisFunc,
                                        Args... args) {
  RTN rtn = RTN_FindByName(img, routineName);

  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, ipoint, analysisFunc, args..., IARG_END);
    RTN_Close(rtn);
  }
}

// Instruction instrumentation
void Instrumentation::doInstructionInstrumentation(INS ins) {
  UINT32 memOperands = INS_MemoryOperandCount(ins);

  for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
    if (INS_MemoryOperandIsRead(ins, memOp)) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                               (AFUNPTR)&MemoryTracker::recordMemRead, IARG_PTR,
                               &memoryTracker, IARG_THREAD_ID, IARG_INST_PTR,
                               IARG_MEMORYOP_EA, memOp, IARG_END);
    }

    if (INS_MemoryOperandIsWritten(ins, memOp)) {
      INS_InsertPredicatedCall(
          ins, IPOINT_BEFORE, (AFUNPTR)&MemoryTracker::recordMemWrite, IARG_PTR,
          &memoryTracker, IARG_THREAD_ID, IARG_INST_PTR, IARG_MEMORYOP_EA,
          memOp, IARG_END);
    }
  }
}

// Image instrumentation
void Instrumentation::doImageInstrumentation(IMG img) {
  log << "[PIN] Loading image: " << IMG_Name(img) << std::endl;
  log << "[PIN] Load offset: 0x" << std::hex << IMG_LoadOffset(img) << std::dec
      << std::endl;

  Language defaultLang =
      IMG_IsMainExecutable(img) ? Language::RUST : Language::C;

  // Instrument all routines
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      std::string rtnName = RTN_Name(rtn);

      if (languageTracker.shouldSkip(rtnName)) {
        continue;
      }

      log << "Instrumenting " << rtnName << std::endl;

      RTN_Open(rtn);

      Language lang = languageTracker.isC(rtnName) ? Language::C : defaultLang;

      RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)&LanguageTracker::push,
                     IARG_PTR, &languageTracker, IARG_THREAD_ID, IARG_UINT32,
                     lang, IARG_END);

      RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)&LanguageTracker::pop,
                     IARG_PTR, &languageTracker, IARG_THREAD_ID, IARG_END);

      RTN_Close(rtn);
    }
  }

  // Instrument special functions
  instrumentRoutine(
      img, "baleen", IPOINT_BEFORE, (AFUNPTR)&MemoryTracker::beforeBaleen,
      IARG_PTR, &memoryTracker, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
      IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2);

  instrumentRoutine(img, "free", IPOINT_BEFORE,
                    (AFUNPTR)&MemoryTracker::beforeFree, IARG_PTR,
                    &memoryTracker, IARG_FUNCARG_ENTRYPOINT_VALUE, 0);

  instrumentRoutine(img, "malloc", IPOINT_BEFORE,
                    (AFUNPTR)&MemoryTracker::beforeMalloc, IARG_PTR,
                    &memoryTracker, IARG_THREAD_ID,
                    IARG_FUNCARG_ENTRYPOINT_VALUE, 0);

  instrumentRoutine(
      img, "malloc", IPOINT_AFTER, (AFUNPTR)&MemoryTracker::afterMalloc,
      IARG_PTR, &memoryTracker, IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE);

  instrumentRoutine(
      img, "realloc", IPOINT_BEFORE, (AFUNPTR)&MemoryTracker::beforeRealloc,
      IARG_PTR, &memoryTracker, IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE,
      0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1);

  instrumentRoutine(
      img, "realloc", IPOINT_AFTER, (AFUNPTR)&MemoryTracker::afterRealloc,
      IARG_PTR, &memoryTracker, IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE);

  funcLog << std::endl;
}

// Finalization
void Instrumentation::doFinalization(INT32 code) {}

} // namespace baleen
