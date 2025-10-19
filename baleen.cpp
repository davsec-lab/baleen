#include <iostream>
#include <fstream>
#include <ostream>
#include <map>
#include <optional>
#include <types.h>
#include <vector>

#include "registry.h"

#include "pin.H"
#include "types_vmapi.PH"

using std::cerr;
using std::endl;
using std::string;
using std::ofstream;
using std::map;

// Output file for logging.
ofstream outFile;

// Maps every starting address to its object (name and size).
Registry objects;  

// Maps the name of every object to its starting address.
map<string, ADDRINT> starts; 

// Maps the name of every object to its read count.
map<string, int> reads;

VOID RecordMemRead(ADDRINT ip, ADDRINT addr) {
    auto node = objects.find(addr);

    if (node) {
        outFile << "[PIN] Read at 0x" << std::hex << ip 
                << ": address 0x" << addr 
                << " (object: " << node->object << ")" << std::dec << endl;

        // Increment count for this object
        reads[node->object]++;
    }
}

VOID Instruction(INS ins, VOID *v) {
    // Instrument memory reads
    UINT32 memOperands = INS_MemoryOperandCount(ins);
    
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

// Analysis function called after _scampi_register returns
VOID AfterScampiMark(ADDRINT callSite, ADDRINT returnValue, ADDRINT pointerArg, ADDRINT sizeArg, ADDRINT stringArg) {
    // Read the string name
    std::string objectName;
    if (stringArg != 0) {
        char buffer[256];
        PIN_SafeCopy(buffer, (void*)stringArg, sizeof(buffer) - 1);
        buffer[255] = '\0';
        objectName = buffer;
    } else {
        objectName = "unnamed";
    }

    // Map the address range to the object name
    objects.insert(pointerArg, sizeArg, objectName);
    starts[objectName] = pointerArg;
    
    // Initialize the count for this object name (if not already present)
    if (reads.find(objectName) == reads.end()) {
        reads[objectName] = 0;
    }

    std::cout << "Foo" << std::endl;

    outFile << "[PIN] _scampi_register called from: 0x" << std::hex << callSite
            << ", return value: 0x" << returnValue << std::dec << endl;
    
    outFile << "[PIN] Pointer argument: 0x" << std::hex << pointerArg << std::dec << endl;
    outFile << "[PIN] Size argument: 0x" << std::hex << sizeArg << std::dec << endl;
    outFile << "[PIN] Object name: \"" << objectName << "\"" << endl;

    outFile.flush();
}

// Global or thread-local storage for realloc args
struct ReallocArgs {
    ADDRINT addr;
    USIZE size;
};
PIN_LOCK reallocLock;
std::map<THREADID, ReallocArgs> pendingRealloc;

VOID BeforeRealloc(THREADID tid, ADDRINT addr, USIZE size) {
    PIN_GetLock(&reallocLock, tid);
    pendingRealloc[tid] = {addr, size};
    PIN_ReleaseLock(&reallocLock);
}

VOID AfterRealloc(THREADID tid, ADDRINT ret) {
    PIN_GetLock(&reallocLock, tid);
    auto args = pendingRealloc[tid];
    PIN_ReleaseLock(&reallocLock);
    
    std::cout << "realloc(0x" << std::hex << args.addr << std::dec
              << ", " << args.size
              << ") → 0x" << std::hex << ret << std::dec << std::endl;
}

VOID BeforeFree(ADDRINT addr) {
    std::cout << "free(" << std::hex << addr << std::dec
              << ")" << std::endl;
}

template<typename... Args>
void instrument(IMG img, const char* routineName, IPOINT ipoint, AFUNPTR analysisFunc, Args... args) {
    RTN rtn = RTN_FindByName(img, routineName);
    
    if (RTN_Valid(rtn)) {
        RTN_Open(rtn);
        
        RTN_InsertCall(rtn, ipoint, analysisFunc, args..., IARG_END);
        
        RTN_Close(rtn);
    }
}

// Image instrumentation function called when each image is loaded
VOID Image(IMG img, VOID *v) {
    outFile << "[PIN] Loading image: " << IMG_Name(img) << endl;
    outFile << "[PIN] Load offset: 0x" << std::hex << IMG_LoadOffset(img) << std::dec << endl;
    
    // Method 1: Iterate through symbols to find _scampi_register
    outFile << "[PIN] Searching through symbols..." << endl;
    SYM scampiSym = SYM_Invalid();
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
        string name = SYM_Name(sym);
        if (name == "_scampi_register") {
            outFile << "[PIN] Found symbol _scampi_register at 0x" 
                    << std::hex << SYM_Address(sym) << std::dec << endl;
            scampiSym = sym;
            
            // Try to get RTN from the symbol's address
            RTN rtn = RTN_FindByAddress(SYM_Address(sym));
            if (RTN_Valid(rtn)) {
                outFile << "[PIN] Got RTN from symbol: " << RTN_Name(rtn) << endl;
                
                RTN_Open(rtn);
                RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)AfterScampiMark,
                                IARG_RETURN_IP,
                                IARG_FUNCRET_EXITPOINT_VALUE,
                                IARG_REG_VALUE, REG_RDI,  // First argument (pointer)
                                IARG_REG_VALUE, REG_RSI,  // Second argument (size)
                                IARG_REG_VALUE, REG_RDX,  // Third argument (string pointer)
                                IARG_END);
                  
                RTN_Close(rtn);
                
                outFile << "[PIN] *** Successfully instrumented via symbol!" << endl;
            } else {
                outFile << "[PIN] Could not get valid RTN from symbol address" << endl;
            }
            break;
        }
    }

    if (!SYM_Valid(scampiSym)) {
        outFile << "[PIN] Symbol _scampi_register not found in symbol table" << endl;
    }

    instrument(img, "free", IPOINT_BEFORE,
               (AFUNPTR)BeforeFree,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 0);

    instrument(img, "realloc", IPOINT_BEFORE,
               (AFUNPTR)BeforeRealloc,
               IARG_THREAD_ID,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 1);

    instrument(img, "realloc", IPOINT_AFTER,
               (AFUNPTR)AfterRealloc,
               IARG_THREAD_ID,
               IARG_FUNCRET_EXITPOINT_VALUE);
    
    outFile << endl;
}

// Finalization function
VOID Fini(INT32 code, VOID *v) {
    outFile << "\n[PIN] ========== Read Count Summary ==========" << endl;
    
    for (const auto& pair : reads) {
        outFile << "[PIN] Object \"" << pair.first << "\": " 
                << pair.second << " reads" << endl;
    }
    
    outFile << "[PIN] ==========================================" << endl;
    outFile << "[PIN] Instrumentation finished" << endl;
    outFile.close();
}

// Print usage information
INT32 Usage() {
    cerr << "This tool instruments calls to _scampi_register" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

// Main function
int main(int argc, char *argv[]) {
    // Initialize symbol processing - MUST be called before PIN_Init
    PIN_InitSymbols();
    
    // Initialize Pin
    if (PIN_Init(argc, argv)) {
        return Usage();
    }
    
    // Open output file
    outFile.open("scampi_trace.out");
    
    // Register image instrumentation callback
    IMG_AddInstrumentFunction(Image, 0);

    INS_AddInstrumentFunction(Instruction, 0);
    
    // Register finish callback
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program
    PIN_StartProgram();
    
    return 0;
}