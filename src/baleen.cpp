#include <elf.h>
#include <link.h>
#include <cstdlib>
#include <sys/wait.h>
#include <fstream>
#include <unistd.h>

#include "pin.H"

#include "extensions.h"
#include "language.h"
#include "allocation.h"
#include "utilities.h"
#include "registry.h"
#include "object.h"
#include "logger.h"
#include "utilities.h"

using std::cerr;
using std::string;
using std::set;
using std::pair;
using std::endl;

UINT32 use_fff = 0;
set<string> foreign_functions;

// Storage for strings to ensure pointers remain valid during execution
static set<string> rtn_names; 

// Helper to store string and return a stable pointer
const char* StoreString(const string& str) {
    pair<set<string>::iterator, bool> ret = rtn_names.insert(str);
    return ret.first->c_str();
}

Logger logger;
AllocationTracker allocationTracker(logger);
LanguageTracker languageTracker(logger);
ObjectTracker objectTracker(logger);

INT32 Usage() {
    cerr << "Baleen 🐋" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID RecordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Language lang = languageTracker.GetCurrent(tid);
    objectTracker.RecordRead(tid, addr, lang);
}

VOID RecordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Language lang = languageTracker.GetCurrent(tid);
    objectTracker.RecordWrite(tid, addr, lang);
}

VOID BeforeRust(THREADID tid, char* name) {
    logger.Stream(LogSubject::EXECUTION) << "[ENTER RUST] " << name << endl;
    languageTracker.Enter(tid, Language::RUST);
}

VOID AfterRust(THREADID tid, char* name) {
    logger.Stream(LogSubject::EXECUTION) << "[EXIT RUST] " << name << endl;
    languageTracker.Exit(tid);
}

VOID BeforeC(THREADID tid, char* name) {
    logger.Stream(LogSubject::EXECUTION) << "[ENTER C] " << name << endl;
    languageTracker.Enter(tid, Language::C);
}

VOID AfterC(THREADID tid, char* name) {
    logger.Stream(LogSubject::EXECUTION) << "[EXIT C] " << name << endl;
    languageTracker.Exit(tid);
}

VOID Instruction(INS ins, VOID *v) {
    // Instrument memory reads/writes
    UINT32 memOperands = INS_MemoryOperandCount(ins);
    
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_THREAD_ID,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }

        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_THREAD_ID,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

VOID BeforeBaleen(THREADID tid, ADDRINT addr, ADDRINT size, ADDRINT name) {
    Language lang = languageTracker.GetCurrent(tid);
    objectTracker.RegisterObject(tid, addr, size, lang, name);
}

VOID BeforeMalloc(THREADID tid, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforeMalloc(tid, size, lang);
}

VOID AfterMalloc(THREADID tid, ADDRINT returned) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.AfterMalloc(tid, returned, lang, objectTracker);
}

VOID BeforePosixMemalign(THREADID tid, ADDRINT memptr, USIZE alignment, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforePosixMemalign(tid, memptr, alignment, size, lang);
}

VOID AfterPosixMemalign(THREADID tid, ADDRINT memptr, INT32 result) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.AfterPosixMemalign(tid, memptr, result, lang, objectTracker);
}

VOID BeforeRealloc(THREADID tid, ADDRINT addr, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforeRealloc(tid, addr, size, lang);
}

VOID AfterRealloc(THREADID tid, ADDRINT addr) {
    allocationTracker.AfterRealloc(tid, addr, objectTracker);
}

VOID BeforeFree(THREADID tid, ADDRINT addr) {
    allocationTracker.BeforeFree(tid, addr, objectTracker);
}

VOID InstrumentImage(IMG img, VOID *v) {
    logger.Stream(LogSubject::INSTRUMENTATION) << "Instrumenting image: " << IMG_Name(img) << endl;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            string rtnName = RTN_Name(rtn);

            string file;
            INT32 line;
            PIN_GetSourceLocation(RTN_Address(rtn), NULL, &line, &file);

            if (EndsWith(file, ".rs") || RTN_IsRust(rtn)) {
                logger.Stream(LogSubject::INSTRUMENTATION) << "(RUST) " << rtnName << endl;

                // Store string for safe pointer usage
                const char* safe_name = StoreString(rtnName);

                RTN_Instrument(img, rtn, IPOINT_BEFORE,
                             (AFUNPTR) BeforeRust,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name,
                             IARG_END);
                
                RTN_Instrument(img, rtn, IPOINT_AFTER,
                             (AFUNPTR) AfterRust,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name,
                             IARG_END);
            } else {
                logger.Stream(LogSubject::INSTRUMENTATION) << "(NOT RUST) " << rtnName << endl;
            }

            if (foreign_functions.count(rtnName) > 0) {
                // Store string for safe pointer usage
                const char* safe_name = StoreString(rtnName);

                RTN_Instrument(img, rtn, IPOINT_BEFORE,
                             (AFUNPTR) BeforeC,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name,
                             IARG_END);
                
                RTN_Instrument(img, rtn, IPOINT_AFTER,
                             (AFUNPTR) AfterC,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name,
                             IARG_END);
            }
        }
    }

    logger.Stream(LogSubject::INSTRUMENTATION) << endl;

    RTN_InstrumentByName(img, "baleen", IPOINT_BEFORE,
                         (AFUNPTR) BeforeBaleen,
                         IARG_THREAD_ID,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // Address
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2); // Name

    if (IMG_Name(img).find("libc") != string::npos) {
        RTN_InstrumentByName(img, "malloc", IPOINT_BEFORE,
                             (AFUNPTR) BeforeMalloc,
                             IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0);
        
        RTN_InstrumentByName(img, "malloc", IPOINT_AFTER,
                             (AFUNPTR) AfterMalloc,
                             IARG_THREAD_ID,
                             IARG_FUNCRET_EXITPOINT_VALUE);

        RTN_InstrumentByName(img, "realloc", IPOINT_BEFORE,
                             (AFUNPTR) BeforeRealloc,
                             IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 1);
        
        RTN_InstrumentByName(img, "realloc", IPOINT_AFTER,
                             (AFUNPTR) AfterRealloc,
                             IARG_THREAD_ID,
                             IARG_FUNCRET_EXITPOINT_VALUE);

        RTN_InstrumentByName(img, "free", IPOINT_BEFORE,
                             (AFUNPTR) BeforeFree,
                             IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0);
		
		RTN_InstrumentByName(img, "posix_memalign", IPOINT_BEFORE,
                             (AFUNPTR) BeforePosixMemalign,
                             IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // memptr
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // alignment
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 2); // size

		RTN_InstrumentByName(img, "posix_memalign", IPOINT_AFTER,
                             (AFUNPTR) AfterPosixMemalign,
                             IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // memptr
                             IARG_FUNCRET_EXITPOINT_VALUE);    // result
    }
}

VOID PrintReport(INT32 code, VOID *v) {
    ofstream report("./.baleen/report.txt");

    allocationTracker.Report(report);
    objectTracker.Report(report);
    
    report.close();
}

int main(int argc, char *argv[]) {
    // Create file to hold list of foreign functions
    Run("touch .baleen/foreign-functions.txt");

    // Run the foreign function finder (FFF) to generate a list of foreign functions
    const char* command = "bfff --output .baleen/foreign-functions.txt >/dev/null 2>&1";
    
    int status = Run(command);
    if (status == -1) {
        std::cerr << "Failed to complete foreign function analysis" << std::endl;
        exit(1);
    } else if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            std::cerr << "The Foreign Function Finder failed, please make sure it works manually" << std::endl;
            exit(1);
        }
    } else {
        std::cerr << "The Foreign Function Finder was interrupted unexpectedly" << std::endl;
        exit(1);
    }

    // Read the collected foreign functions
    std::ifstream input_file(".baleen/foreign-functions.txt");
    
    std::string line;
    while (std::getline(input_file, line)) {
        if (!line.empty()) {
            foreign_functions.insert(line);
        }
    }
    
    input_file.close();

    // Initialize Pin
	PIN_InitSymbols();
    
    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    IMG_AddInstrumentFunction(InstrumentImage, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(PrintReport, 0);
 
    PIN_StartProgram();

    return 0;
}