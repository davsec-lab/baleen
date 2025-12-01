#include "pin.H"
#include <iostream>
#include <fstream>
#include <set>
#include <map> // --- NEW --- Include map

#include "language.h"
#include "registry.h"

using std::map;
using std::pair;
using std::set;
using std::string;
using std::cerr;
using std::endl;
using std::ofstream;

class AllocationTracker {
private:
	PIN_LOCK lock;
	map<Language, UINT64> allocations;
	map<THREADID, map<string, pair<UINT64, USIZE>>> pending;
	map<THREADID, map<string, UINT64>> counter;
	ofstream log;
	
	VOID Allocate(THREADID tid, UINT64 bytes, Language lang) {
		allocations[lang] += bytes;
	}

public:
	AllocationTracker() {}

	VOID BeforeMalloc(THREADID tid, UINT64 bytes, Language lang) {
		PIN_GetLock(&lock, tid + 1);

		auto id = counter[tid]["malloc"]++;
		pending[tid]["malloc"] = { id, bytes };

		PIN_ReleaseLock(&lock);
	}

	VOID AfterMalloc(THREADID tid, ADDRINT returned, Language lang) {
		PIN_GetLock(&lock, tid + 1);

		auto payload = pending[tid]["malloc"];

		if (returned != 0) {
			USIZE size = payload.second;
			Allocate(tid, size, lang);
		} else {
			log << "[AFTER MALLOC] [" << payload.first << "] 'malloc' failed" << endl;
		}

		PIN_ReleaseLock(&lock);
	}

	VOID Report(ofstream& stream) {
		auto rustBytes = allocations[Language::RUST];
		auto cBytes = allocations[Language::C];
		auto sharedBytes = allocations[Language::SHARED];

		stream << endl << "--- Allocation Report ---" << endl;
		stream << "Rust:   " << rustBytes << " bytes" << endl;
		stream << "C:      " << cBytes << " bytes" << endl;
		stream << "Shared: " << sharedBytes << " bytes" << endl;
		stream << "Total:  " << (rustBytes + cBytes + sharedBytes) << " bytes" << endl;
		stream.close();
	}
};

ofstream messages;
ofstream calls;
ofstream routines;
ofstream allocations;

static set<string> rtn_names; 

AllocationTracker allocationTracker;
LanguageTracker languageTracker;

// All helper functions (EndsWith, IsRustModern, IsRustLegacy, IsRuntime, IsStub, IsRust)
// remain exactly the same as in your original code.
BOOL EndsWith(std::string_view s, std::string_view suffix) {
    if (s.length() < suffix.length()) return false;
    size_t start_pos = s.length() - suffix.length();
    return s.substr(start_pos) == suffix;
}

BOOL IsRustModern(const string& name) {
    size_t len = name.length();
    if (len < 19) return false;
    if (name[len - 1] != 'E') return false;
    if (name[len - 20] != '1' || name[len - 19] != '7' || name[len - 18] != 'h') return false;
    for (size_t i = len - 17; i < len - 1; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
    }
    if (name.rfind("_ZN", 0) != 0) return false;
    return true;
}

BOOL IsRustLegacy(const string& name) {
    return name.find("___rust") != string::npos;
}

BOOL IsRuntime(const string& name) {
    static const set<string> names = {
        "_start", "deregister_tm_clones", "register_tm_clones", "__do_global_dtors_aux",
        "frame_dummy", "rust_eh_personality", ".init", "_init", ".fini", "_fini",
        ".plt", ".plt.got", ".plt.sec", ".text", "__rust_try", ""
    };
    return names.count(name) > 0;
}

BOOL IsStub(const string& name) {
    return EndsWith(name, "@plt");
}

BOOL IsRust(const string& name) {
    return IsRustModern(name) || IsRustLegacy(name) || name == "main";
}

BOOL IMG_IsVdso(IMG img) {
    string imgName = IMG_Name(img);
    static const set<string> names = {"[vdso]", "[linux-gate.so.1]", "[linux-vdso.so.1]"};
    return names.count(imgName) > 0;
}

Language RTN_Language(IMG img, RTN rtn) {
    if (IMG_IsInterpreter(img) || IMG_IsVdso(img)) {
        return Language::SHARED;
    }

    string imgName = IMG_Name(img);
    if (imgName.find("libc") != string::npos) {
        return Language::SHARED;
    }

	if (imgName.find("libgcc") != string::npos) {
        return Language::SHARED;
    }

    if (imgName.find("libblkid") != string::npos) {
        return Language::SHARED;
    }

    // --- END NEW CHECK ---

    string rtnName = RTN_Name(rtn);
    if (IsStub(rtnName) || IsRuntime(rtnName)) {
        return Language::SHARED;
    }
    if (IMG_IsMainExecutable(img)) {
        return IsRust(rtnName) ? Language::RUST : Language::C;
    }
    return Language::C;
}


INT32 Usage() {
    cerr << "Baleen 🐋 (State-Based Model)" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}


VOID CheckLanguageState(THREADID tid, ADDRINT new_lang_int, const string* rtnName) {
    Language new_lang = (Language)new_lang_int;

	languageTracker.CheckState(tid, new_lang, rtnName);
}

// Maps every starting address to its object (name and size).
Registry objects;

// Maps the name of every object to its starting address.
map<string, ADDRINT> starts;

// Maps the name of every object to its read count.
map<string, map<Language, int>> reads;

// Maps the name of every object to its write count.
map<string, map<Language, int>> writes;

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

std::map<THREADID, std::pair<unsigned long long, USIZE>> pendingMalloc;
PIN_LOCK mallocLock;

unsigned long long total_malloc_calls = 0;

VOID BeforeMalloc(THREADID tid, USIZE size) {
    Language l = languageTracker.GetCurrent(tid);
	allocationTracker.BeforeMalloc(tid, size, l);
}

VOID AfterMalloc(THREADID tid, ADDRINT ret) {
	Language l = languageTracker.GetCurrent(tid);
	allocationTracker.AfterMalloc(tid, ret, l);
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

	RTN baleen_rtn = RTN_FindByName(img, "baleen");
	if (RTN_Valid(baleen_rtn)) {
		messages << "  Found baleen" << endl;
		RTN_Open(baleen_rtn);

		RTN_InsertCall(baleen_rtn, IPOINT_BEFORE, (AFUNPTR)BeforeBaleen,
					IARG_THREAD_ID,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // Address
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,  // Name
					IARG_END);

		RTN_Close(baleen_rtn);
	}

	if (IMG_Name(img).find("libc") != string::npos) {

		// Find the allocation routines
		RTN malloc_rtn = RTN_FindByName(img, "malloc");
		if (RTN_Valid(malloc_rtn)) {
			messages << "  Found malloc" << endl;
			RTN_Open(malloc_rtn);

			RTN_InsertCall(malloc_rtn, IPOINT_BEFORE, (AFUNPTR)BeforeMalloc,
						IARG_THREAD_ID,
						IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
						IARG_END);

			RTN_InsertCall(malloc_rtn, IPOINT_AFTER, (AFUNPTR)AfterMalloc,
						IARG_THREAD_ID,
						IARG_FUNCRET_EXITPOINT_VALUE, 
						IARG_END);

			RTN_Close(malloc_rtn);
		}
	}
}

VOID PrintReport(INT32 code, VOID *v) {
    // We re-open the messages file in append mode to add our report
    ofstream report("baleen-messages.log", std::ios::app);
    if (!report) {
        cerr << "Could not open report file." << endl;
        return;
    }

    allocationTracker.Report(report);
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

    // Register TRACE instrumentation callback
    TRACE_AddInstrumentFunction(InstrumentTrace, 0);

    // --- NEW ---
    // Register IMAGE instrumentation callback
    IMG_AddInstrumentFunction(InstrumentImage, 0);

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

    return 0;
}