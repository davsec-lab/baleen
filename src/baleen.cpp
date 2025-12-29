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

bool IsIndirectCallToC(ADDRINT target, const string& caller_file) {
	RTN target_rtn = RTN_FindByAddress(target);

    if (!RTN_Valid(target_rtn)) {
        return false;
    }

	string rtnName = RTN_Name(target_rtn);

	if (use_fff) {
		if (foreign_functions.count(rtnName) > 0) {
			return true;
		} else {
			return false;
		}
	}

	if (!EndsWith(caller_file, ".rs")) {
        return false; 
    }
    
    ADDRINT rtnAddr = RTN_Address(target_rtn);
    IMG img = IMG_FindByAddress(rtnAddr);
    string timg = ExtractFileName(IMG_Name(img));

    if (IMG_IsRuntime(timg) || RTN_IsRuntime(target_rtn) || RTN_IsPLTStub(target_rtn)) {
        return false;
    }

    bool target_is_rust = RTN_IsRust(target_rtn);
    return !target_is_rust;
}

VOID BeforeIndirectCall(THREADID tid, ADDRINT caller_ip, ADDRINT target, string* file, UINT32 line) {
    PIN_LockClient();
    
    if (IsIndirectCallToC(target, *file)) {
        RTN target_rtn = RTN_FindByAddress(target);
        string target_name = RTN_Name(target_rtn);
        IMG img = IMG_FindByAddress(RTN_Address(target_rtn));
        string timg = ExtractFileName(IMG_Name(img));
        
		PIN_GetLock(&logLock, tid + 1);
        foreigns << "[CALL] '" << target_name 
                 << "' from Rust file '" << *file
                 << " in image '" << timg
                 << "'" << endl;
		PIN_ReleaseLock(&logLock);

        BeforeC(tid, (char*)target_name.c_str());

        PIN_UnlockClient();
        return;
    }
    
    PIN_UnlockClient();
}

VOID AfterIndirectCall(THREADID tid, ADDRINT target, string* file) {
    PIN_LockClient();
    
    if (IsIndirectCallToC(target, *file)) {
        RTN target_rtn = RTN_FindByAddress(target);
        string target_name = RTN_Name(target_rtn);

        PIN_UnlockClient();

        AfterC(tid, (char*)target_name.c_str());
        return;
    }
    
    PIN_UnlockClient();
}


VOID Instruction(INS ins, VOID *v) {
    if (!INS_IsCall(ins)) {
        return; 
    }

    ADDRINT call_addr = INS_Address(ins);
    IMG caller_img = IMG_FindByAddress(call_addr);
    
    if (!IMG_Valid(caller_img)) {
        return;
    }

    INT32 line;
    string file;
    PIN_GetSourceLocation(call_addr, NULL, &line, &file);

    if (INS_IsDirectCall(ins)) {
        ADDRINT target = INS_DirectControlFlowTargetAddress(ins);
        RTN target_rtn = RTN_FindByAddress(target);
        
        if (RTN_Valid(target_rtn)) {
            IMG target_img = IMG_FindByAddress(target);

            string timg = ExtractFileName(IMG_Name(target_img));
            string cimg = ExtractFileName(IMG_Name(caller_img));
            string target_name = RTN_Name(target_rtn);

			// Store string to ensure pointer validity
			const char* safe_name = StoreString(target_name);

			if (use_fff) {
				// All we need to do is check whether 'target_name' is in our set of foreign functions
				if (foreign_functions.count(target_name) > 0) {
					INS_InsertCall(
						ins, IPOINT_BEFORE,
						(AFUNPTR) BeforeC,
						IARG_THREAD_ID,
						IARG_PTR, safe_name, // Pass name
						IARG_END
					);
					
					INS_InsertCall(
						ins, IPOINT_TAKEN_BRANCH,
						(AFUNPTR) AfterC,
						IARG_THREAD_ID,
						IARG_PTR, safe_name, // Pass name
						IARG_END
					);
				}

				return;
			}

			// We need to use some heuristics
            if (IMG_IsRuntime(timg) || RTN_IsRuntime(target_rtn) || RTN_IsPLTStub(target_rtn)) {
                return;
            }
            
            bool caller_is_rust = EndsWith(file, ".rs");
            bool target_is_rust = RTN_IsRust(target_rtn);
            
            // Rust calling C function
            if (caller_is_rust && !target_is_rust) {
                foreigns << "DIR call to '" << target_name 
                         << "' from Rust file '" << file
                         << "' in image '" << cimg
                         << "'" << endl;
                
                INS_InsertCall(
                    ins, IPOINT_BEFORE,
                    (AFUNPTR) BeforeC,
                    IARG_THREAD_ID,
                    IARG_PTR, safe_name, // Pass name
                    IARG_END
                );
                
                INS_InsertCall(
                    ins, IPOINT_TAKEN_BRANCH,
                    (AFUNPTR) AfterC,
                    IARG_THREAD_ID,
                    IARG_PTR, safe_name, // Pass name
                    IARG_END
                );
            }
        }
    } else {
        INS_InsertCall(
            ins, IPOINT_BEFORE,
            (AFUNPTR)BeforeIndirectCall,
            IARG_THREAD_ID,
            IARG_INST_PTR,
            IARG_BRANCH_TARGET_ADDR,
            IARG_PTR, new string(file),
            IARG_UINT32, line,
            IARG_END
        );

        INS_InsertCall(
            ins, IPOINT_TAKEN_BRANCH,
            (AFUNPTR)AfterIndirectCall,
            IARG_THREAD_ID,
            IARG_BRANCH_TARGET_ADDR,
            IARG_PTR, new string(file),
            IARG_END
        );
    }

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

	// Should we use the foreign function finder to collect all foreign functions?
	const char* _use_fff = std::getenv("BALEEN_FFF");

	if (_use_fff != nullptr) {
		use_fff = std::atoi(_use_fff);
	}

	if (use_fff) {
		std::system("mkdir -p .baleen");
		
		const char* command = "/mnt/disk/.cargo/bin/bfff --output .baleen/foreign-functions.txt 2>&1";
		
		std::cout << "Executing: " << command << std::endl;
		
		FILE* pipe = popen(command, "r");
		if (!pipe) {
			std::cerr << "popen() failed: " << strerror(errno) << std::endl;
			use_fff = 0;
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
				use_fff = 0;
			} else if (WIFEXITED(status)) {
				int exit_code = WEXITSTATUS(status);
				std::cout << "Exit code: " << exit_code << std::endl;
				
				if (exit_code == 0) {
					std::cout << "Command executed successfully." << std::endl;
					
					// Open and read the file
					std::ifstream input_file(".baleen/foreign-functions.txt");
					if (!input_file.is_open()) {
						std::cerr << "Error: Could not open file .baleen/foreign-functions.txt" << std::endl;
						use_fff = 0;
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
					use_fff = 0;
				}
			} else {
				std::cerr << "Command terminated abnormally" << std::endl;
				use_fff = 0;
			}
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