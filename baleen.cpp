#include <iostream>
#include <fstream>
#include <ostream>
#include <map>

#include "pin.H"
#include "registry.h"

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
    
    // Initialize the count for this object name (if not already present)
    if (reads.find(objectName) == reads.end()) {
        reads[objectName] = 0;
    }

    outFile << "Object '" << objectName
            << "' occupies " << size
            << " bytes in range [0x" << std::hex << addr
            << ", 0x" << addr + size
            << ")" << std::dec << std::endl;

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
    
    Node *node = objects.remove(args.addr);

    if (node) {
        outFile << "Object '" << node->object
                << "' was reallocated!" << std::endl;
        
        outFile << "+ " << node->size
                << " → " << args.size
                << " bytes" << std::endl;
        
        outFile << "+ [0x" << std::hex << node->start
                << ", 0x" << node->start + node->size
                << ") → [0x" << ret
                << ", 0x" << ret + args.size
                << ")" << std::dec << std::endl;
        
        objects.insert(ret, args.size, node->object);
        starts[node->object] = ret;
    }
}

VOID BeforeFree(ADDRINT addr) {
    Node *removed = objects.remove(addr);

    if (removed) {
        outFile << "Object '" << removed->object
                << "' is no longer mapped to range [0x" << std::hex << removed->start
                << ", 0x" << removed->start + removed->size
                << ")" << std::dec << std::endl;
        
        // TODO: Is this a good way to handle the start address mapping?
        starts[removed->object] = 0;
    }

    std::cout << "free(0x" << std::hex << addr << std::dec
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

    instrument(img, "baleen", IPOINT_BEFORE,
               (AFUNPTR)BeforeBaleen,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // Address
               IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size
               IARG_FUNCARG_ENTRYPOINT_VALUE, 2); // Name

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
    outFile.open("trace.baleen");
    
    // Register image instrumentation callback
    IMG_AddInstrumentFunction(Image, 0);

    INS_AddInstrumentFunction(Instruction, 0);
    
    // Register finish callback
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program
    PIN_StartProgram();
    
    return 0;
}