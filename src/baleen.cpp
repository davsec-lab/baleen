#include <elf.h>
#include <link.h>

#include "pin.H"

#include "extensions.h"
#include "language.h"
#include "allocation.h"
#include "utilities.h"
#include "registry.h"
#include "object.h"

using std::cerr;

ofstream messages;
ofstream calls;
ofstream routines;
ofstream allocations;
ofstream accesses;
ofstream foreigns;

static set<string> rtn_names; 

AllocationTracker allocationTracker;
LanguageTracker languageTracker;
ObjectTracker objectTracker;

INT32 Usage() {
    cerr << "Baleen 🐋 (State-Based Model)" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID CheckLanguageState(THREADID tid, ADDRINT newLang, const string* rtnName) {
    Language lang = (Language) newLang;
    languageTracker.CheckState(tid, lang, rtnName);
}

VOID RecordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Language lang = languageTracker.GetCurrent(tid);
    objectTracker.RecordRead(tid, addr, lang);
}

VOID RecordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Language lang = languageTracker.GetCurrent(tid);
    objectTracker.RecordWrite(tid, addr, lang);
}

std::string extractFileName(const std::string& fullPath) {
    // Find the position of the last '/' character.
    size_t lastSlashPos = fullPath.rfind('/');

    // Check if a '/' was found.
    if (lastSlashPos == std::string::npos) {
        // If no slash is found, the entire string is the "filename."
        return fullPath;
    } else {
        // Extract the substring starting immediately after the slash.
        size_t substringStart = lastSlashPos + 1;

        return fullPath.substr(substringStart);
    }
}

VOID RecordIndirectCall(THREADID tid, ADDRINT caller_ip, ADDRINT target, string* file, UINT32 line) {
    if (!EndsWith(*file, ".rs")) {
        return; // Caller is not Rust
    }
    
    PIN_LockClient();
    RTN target_rtn = RTN_FindByAddress(target);

	ADDRINT rtnAddr = RTN_Address(target_rtn);
	IMG img = IMG_FindByAddress(rtnAddr);

	string timg = extractFileName(IMG_Name(img));

	if (IMG_IsRuntime(timg) || RTN_IsRuntime(target_rtn) || RTN_IsPLTStub(target_rtn)) {
		PIN_UnlockClient();
		return;
	};

    if (RTN_Valid(target_rtn)) {
        bool target_is_rust = RTN_IsRust(target_rtn);
        
        if (!target_is_rust) {
            string target_name = RTN_Name(target_rtn);
            foreigns << "IND call to '" << target_name 
                     << "' from Rust file '" << *file
					 << " in image '" << timg
					 << "'" << endl;
        }
    }
    
    PIN_UnlockClient();
}



VOID Instruction(INS ins, VOID *v) {
	if (!INS_IsCall(ins)) {
        return; // Only instrument call instructions
    }

    ADDRINT call_addr = INS_Address(ins);
    IMG caller_img = IMG_FindByAddress(call_addr);
    
    if (!IMG_Valid(caller_img)) {
        return;
    }

    // Get source location of the caller
    INT32 line;
    string file;
    PIN_GetSourceLocation(call_addr, NULL, &line, &file);

    // Check if this is a direct call (has a target)
    if (INS_IsDirectCall(ins)) {
        ADDRINT target = INS_DirectControlFlowTargetAddress(ins);
        RTN target_rtn = RTN_FindByAddress(target);
        
        if (RTN_Valid(target_rtn)) {
            IMG target_img = IMG_FindByAddress(target);

			string timg = extractFileName(IMG_Name(target_img));

			// foreigns << "inside image '" << timg << "'" << endl;

            string target_name = RTN_Name(target_rtn);

			if (IMG_IsRuntime(timg) || RTN_IsRuntime(target_rtn) || RTN_IsPLTStub(target_rtn)) {
				return;
			}
            
            // Determine languages
            bool caller_is_rust = EndsWith(file, ".rs");
            bool target_is_rust = RTN_IsRust(target_rtn);
            
            // Rust calling C function
            if (caller_is_rust && !target_is_rust) {
                foreigns << "DIR call to '" << target_name 
                         << "' from Rust file '" << file
						 << "' in image '" << timg
						 << "'" << endl;
            }
        }
    } else {
        // Indirect call - we need runtime instrumentation
        INS_InsertCall(
            ins, IPOINT_BEFORE,
            (AFUNPTR)RecordIndirectCall,
            IARG_THREAD_ID,
            IARG_INST_PTR,
            IARG_BRANCH_TARGET_ADDR,
            IARG_PTR, new string(file),
            IARG_UINT32, line,
            IARG_END
        );
    }

    // Instrument memory reads
    UINT32 memOperands = INS_MemoryOperandCount(ins);
    
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_THREAD_ID,  // Add this
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }

        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_THREAD_ID,  // Add this
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

VOID BeforeBaleen(THREADID tid, ADDRINT addr, ADDRINT size, ADDRINT name) {
    objectTracker.RegisterObject(tid, addr, size, name);
}

VOID BeforeMalloc(THREADID tid, USIZE size) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.BeforeMalloc(tid, size, lang);
}

VOID AfterMalloc(THREADID tid, ADDRINT returned) {
    Language lang = languageTracker.GetCurrent(tid);
    allocationTracker.AfterMalloc(tid, returned, lang);
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

VOID InstrumentTrace(TRACE trace, VOID *v) {
    // Get the address of the first instruction in the trace
    ADDRINT trace_addr = TRACE_Address(trace);

    // We must lock Pin to safely look up routine information
    PIN_LockClient();
    
    RTN rtn = RTN_FindByAddress(trace_addr);

    Language trace_lang = Language::SHARED;
    string rtnName = "unknown";

    if (RTN_Valid(rtn)) {
        SEC sec = RTN_Sec(rtn);
        if (SEC_Valid(sec)) {
            IMG img = SEC_Img(sec);
            if (IMG_Valid(img)) {
                // Use our existing helper to find the language of this routine
                trace_lang = RTN_Language(img, rtn);
                rtnName = RTN_Name(rtn);
            }
        }
    }
    
    // Unlock Pin as we are done looking up symbols
    PIN_UnlockClient();

    if (trace_lang != Language::SHARED) {
        // Get a stable pointer to the routine name string
        auto it = rtn_names.insert(rtnName);
        const string* rtnNamePtr = &(*it.first);

        // Insert a call to our analysis function (CheckLanguageState)
        TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)CheckLanguageState,
                         IARG_THREAD_ID,
                         IARG_ADDRINT, (ADDRINT)trace_lang,
                         IARG_PTR, rtnNamePtr,
                         IARG_END);
    }
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

            if (EndsWith(file, ".rs")) {
                routines << "(RUST) " << rtnName << endl;
            }

            // Determine the routine language
            // Language language = RTN_Language(img, rtn);

            // routines << "Inspecting " << rtnName << " (" << LanguageToString(language) << ")" << endl;
        }
    }

    routines << endl;

    return

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
    // Initialize symbol processing
    PIN_InitSymbols();
    
    // Initialize Pin
    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    // Open logging files
    foreigns.open(".baleen/foreigns.log");
    allocations.open(".baleen/allocations.log");
    routines.open(".baleen/routines.log");
    messages.open("baleen-messages.log");
    calls.open("baleen-calls.log");
    accesses.open("baleen-accesses.log");

    // Register TRACE instrumentation callback
    TRACE_AddInstrumentFunction(InstrumentTrace, 0);

    // Register IMAGE instrumentation callback
    IMG_AddInstrumentFunction(InstrumentImage, 0);

    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini callback
    PIN_AddFiniFunction(PrintReport, 0);
 
    // Start the program
    PIN_StartProgram();

    // Close logging files
    allocations.close();
    routines.close();
    messages.close();
    calls.close();
    accesses.close();
    foreigns.close();

    return 0;
}