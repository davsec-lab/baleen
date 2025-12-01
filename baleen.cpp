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

// Maps every starting address to its object (name and size).
Registry objects;

// Maps the name of every object to its starting address.
map<string, ADDRINT> starts;

// Maps the name of every object to its read count.
map<string, map<Language, int>> reads;

// Maps the name of every object to its write count.
map<string, map<Language, int>> writes;

VOID RecordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr) {
    auto node = objects.find(addr);

    if (node) {
        Language currentLang = languageTracker.GetCurrent(tid);
        
        accesses << "[READ] Read from " << std::hex << addr 
                << " ('" << node->object
                << "')\n" << std::dec << endl;

        // Increment count for this object
        reads[node->object][currentLang]++;
    }
}

VOID RecordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr) {
    auto node = objects.find(addr);

    if (node) {
        Language currentLang = languageTracker.GetCurrent(tid);
        
        accesses << "[WRITE] Write to " << std::hex << addr 
                << " ('" << node->object
                << "')\n" << std::dec << endl;

        // Increment count for this object
        writes[node->object][currentLang]++;
    }
}

VOID Instruction(INS ins, VOID *v) {
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

VOID BeforeBaleen(ADDRINT addr, ADDRINT size, ADDRINT name) {
    // Read the string name
    std::string objectName;
    if (name != 0) {
        char buffer[256];
        PIN_SafeCopy(buffer, (void*)name, sizeof(buffer) - 1);
        buffer[255] = '\0';
        objectName = buffer;
    } else {
        objectName = "unnamed";
    }

    // Map the address range to the object name
    objects.insert(addr, size, objectName);
    starts[objectName] = addr;
    
    // Initialize the counts for this object name (if not already present)
    if (reads.find(objectName) == reads.end()) {
        reads[objectName] = {};
        writes[objectName] = {};

        reads[objectName][Language::C] = 0;
        reads[objectName][Language::RUST] = 0;

        writes[objectName][Language::C] = 0;
        writes[objectName][Language::RUST] = 0;
    }

    messages << "[BEFORE BALEEN] Object '" << objectName
            << "' occupies " << size
            << " bytes in range [0x" << std::hex << addr
            << ", 0x" << addr + size
            << ")\n" << std::dec << std::endl;

    messages.flush();
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

			// Determine the routine language
			Language language = RTN_Language(img, rtn);

			routines << "Inspecting " << rtnName << " (" << LanguageToString(language) << ")" << endl;
		}
	}

	routines << endl;

	RTN_InstrumentByName(img, "baleen", IPOINT_BEFORE,
						 (AFUNPTR) BeforeBaleen,
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
	}
}

VOID PrintReport(INT32 code, VOID *v) {
    ofstream report("baleen-messages.log", std::ios::app);
    if (!report) {
        cerr << "Could not open report file." << endl;
        return;
    }

    allocationTracker.Report(report);

    report << "Name, Reads (Rust), Reads (C), Writes (Rust), Writes (C)" << std::endl;

    for (const auto& pair : reads) {
        const string& objName = pair.first;

        report << objName << ", ";

        int rustReads = reads[objName][Language::RUST];
        int cReads = reads[objName][Language::C];

        report << rustReads << ", " << cReads << ", ";

        int rustWrites = writes[objName][Language::RUST];
        int cWrites = writes[objName][Language::C];

        report << rustWrites << ", " << cWrites << std::endl;
    }

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
	allocations.open(".baleen/allocations.log");
	routines.open(".baleen/routines.log");
    messages.open("baleen-messages.log");
    calls.open("baleen-calls.log");
	accesses.open("baleen-accesses.log");

    // Register TRACE instrumentation callback
    TRACE_AddInstrumentFunction(InstrumentTrace, 0);

    // --- NEW ---
    // Register IMAGE instrumentation callback
    IMG_AddInstrumentFunction(InstrumentImage, 0);

	INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini callback
    PIN_AddFiniFunction(PrintReport, 0);
    // --- END NEW ---

    // Start the program
    PIN_StartProgram();

    // Close logging files
	allocations.close();
	routines.close();
    messages.close();
    calls.close();
	accesses.close();

    return 0;
}