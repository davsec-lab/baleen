#include <elf.h>
#include <link.h>
#include <cstdlib>
#include <sys/wait.h>

#include "pin.H"

#include "extensions.h"
#include "language.h"
#include "allocation.h"
#include "utilities.h"
#include "registry.h"
#include "object.h"

using std::cerr;
using std::string;
using std::set;
using std::pair;

ofstream messages;
ofstream calls;
ofstream routines;
ofstream allocations;
ofstream accesses;
ofstream foreigns;

PIN_LOCK logLock;

UINT32 use_fff = 0;
set<string> foreign_functions;

// Storage for strings to ensure pointers remain valid during execution
static set<string> rtn_names; 

// Helper to store string and return a stable pointer
const char* StoreString(const string& str) {
    pair<set<string>::iterator, bool> ret = rtn_names.insert(str);
    return ret.first->c_str();
}

AllocationTracker allocationTracker;
LanguageTracker languageTracker(foreigns);
ObjectTracker objectTracker;

INT32 Usage() {
    cerr << "Baleen 🐋 (State-Based Model)" << endl;
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
	PIN_GetLock(&logLock, tid + 1);
    foreigns << "[ENTER RUST] " << name << endl;
	PIN_ReleaseLock(&logLock);

    languageTracker.Enter(tid, Language::RUST);
}

VOID AfterRust(THREADID tid, char* name) {
	PIN_GetLock(&logLock, tid + 1);
    foreigns << "[EXIT RUST] " << name << endl;
	PIN_ReleaseLock(&logLock);

    languageTracker.Exit(tid);
}

VOID BeforeC(THREADID tid, char* name) {
	PIN_GetLock(&logLock, tid + 1);
    foreigns << "[ENTER C] " << name << endl;
	PIN_ReleaseLock(&logLock);

    languageTracker.Enter(tid, Language::C);
}

VOID AfterC(THREADID tid, char* name) {
	PIN_GetLock(&logLock, tid + 1);
    foreigns << "[EXIT C] " << name << endl;
	PIN_ReleaseLock(&logLock);

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
    // objectTracker.RegisterObject(tid, addr, size, name);
}

VOID BeforeMalloc(THREADID tid, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforeMalloc(tid, size, lang);
}

VOID AfterMalloc(THREADID tid, ADDRINT returned) {
    Language lang = languageTracker.GetCurrent(tid);
	foreigns << "malloc" << endl;
    allocationTracker.AfterMalloc(tid, returned, lang, objectTracker);
}

VOID BeforePosixMemalign(THREADID tid, ADDRINT memptr, USIZE alignment, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforePosixMemalign(tid, memptr, alignment, size, lang);
}

VOID AfterPosixMemalign(THREADID tid, ADDRINT memptr, INT32 result) {
    Language lang = languageTracker.GetCurrent(tid);
    foreigns << "posix_memalign" << endl;
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
    messages << "Instrumenting image: " << IMG_Name(img) << endl;
    routines << "Instrumenting image: " << IMG_Name(img) << endl;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            string rtnName = RTN_Name(rtn);

            string file;
            INT32 line;
            PIN_GetSourceLocation(RTN_Address(rtn), NULL, &line, &file);

            if (EndsWith(file, ".rs") || RTN_IsRust(rtn)) {
                routines << "(RUST) " << rtnName << endl;

                // Store string for safe pointer usage
                const char* safe_name = StoreString(rtnName);

                RTN_Instrument(img, rtn, IPOINT_BEFORE,
                             (AFUNPTR) BeforeRust,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name, // Pass name
                             IARG_END);
                
                RTN_Instrument(img, rtn, IPOINT_AFTER,
                             (AFUNPTR) AfterRust,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name, // Pass name
                             IARG_END);
            } else {
                routines << "(NOT RUST) " << rtnName << endl;
            }

            if (foreign_functions.count(rtnName) > 0) {
                // Store string for safe pointer usage
                const char* safe_name = StoreString(rtnName);

                RTN_Instrument(img, rtn, IPOINT_BEFORE,
                             (AFUNPTR) BeforeC,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name, // Pass name
                             IARG_END);
                
                RTN_Instrument(img, rtn, IPOINT_AFTER,
                             (AFUNPTR) AfterC,
                             IARG_THREAD_ID,
                             IARG_PTR, safe_name, // Pass name
                             IARG_END);
            }
        }
    }

    routines << endl;

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
    ofstream report("baleen-messages.log", std::ios::app);
    if (!report) {
        cerr << "Could not open report file." << endl;
        return;
    }

    allocationTracker.Report(report);
    objectTracker.Report(report);
    
    report.close();
}

int main(int argc, char *argv[]) {
    

    foreigns.open(".baleen/foreigns.log");
    allocations.open(".baleen/allocations.log");
    routines.open(".baleen/routines.log");
    messages.open("baleen-messages.log");
    calls.open("baleen-calls.log");
    accesses.open("baleen-accesses.log");

    std::system("mkdir -p .baleen");
    
    const char* command = "/mnt/disk/.cargo/bin/bfff --output .baleen/foreign-functions.txt 2>&1";
    
    std::cout << "Executing: " << command << std::endl;
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        std::cerr << "popen() failed: " << strerror(errno) << std::endl;
        exit(1);
    } else {
        char buffer[256];
        std::cout << "=== Command Output ===" << std::endl;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::cout << buffer;
        }
        std::cout << "=== End Output ===" << std::endl;
        
        int status = pclose(pipe);
        std::cout << "Raw status: " << status << std::endl;
        
        if (status == -1) {
            std::cerr << "pclose() failed: " << strerror(errno) << std::endl;
            exit(1);
        } else if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            std::cout << "Exit code: " << exit_code << std::endl;
            
            if (exit_code == 0) {
                std::cout << "Command executed successfully." << std::endl;
                
                // Open and read the file
                std::ifstream input_file(".baleen/foreign-functions.txt");
                if (!input_file.is_open()) {
                    std::cerr << "Error: Could not open file .baleen/foreign-functions.txt" << std::endl;
                    exit(1);
                } else {
                    std::string line;
                    while (std::getline(input_file, line)) {
                        if (!line.empty()) {
                            foreign_functions.insert(line);
                        }
                    }
                    input_file.close();
                }
            } else {
                std::cerr << "Command failed with exit code: " << exit_code << std::endl;
                exit(1);
            }
        } else {
            std::cerr << "Command terminated abnormally" << std::endl;
            exit(1);
        }
    }

	PIN_InitSymbols();
    
    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    IMG_AddInstrumentFunction(InstrumentImage, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(PrintReport, 0);
 
    PIN_StartProgram();

    allocations.close();
    routines.close();
    messages.close();
    calls.close();
    accesses.close();
    foreigns.close();

    return 0;
}