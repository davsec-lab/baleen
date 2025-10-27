#include <iostream>
#include <fstream>
#include <ostream>
#include <map>
#include <set>
#include <stack>

#include "pin.H"
#include "registry.h"

using std::cerr;
using std::endl;
using std::string;
using std::ofstream;
using std::map;
using std::stack;
using std::set;

enum Language {
    RUST,
    C
};

PIN_LOCK languageLock;

set<string> c_functions;

map<THREADID, stack<Language>> languageStack;

// Output file for logging.
ofstream memFile;

ofstream outFile;

// Maps every starting address to its object (name and size).
Registry objects;

// Maps the name of every object to its starting address.
map<string, ADDRINT> starts;

// Maps the name of every object to its read count.
map<string, map<Language, int>> reads;

// Maps the name of every object to its write count.
map<string, map<Language, int>> writes;

set<string> skipFunctions = {"malloc", "realloc", "free", "baleen"};

VOID RecordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr) {
    auto node = objects.find(addr);

    if (node) {
        PIN_GetLock(&languageLock, tid);
        Language currentLang = languageStack[tid].empty() ? Language::C : languageStack[tid].top();
        PIN_ReleaseLock(&languageLock);
        
        memFile << "[READ] Read from " << std::hex << addr 
                << " ('" << node->object
                << "')\n" << std::dec << endl;

        // Increment count for this object
        reads[node->object][currentLang]++;
    }
}

VOID RecordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr) {
    auto node = objects.find(addr);

    if (node) {
        PIN_GetLock(&languageLock, tid);
        Language currentLang = languageStack[tid].empty() ? Language::C : languageStack[tid].top();
        PIN_ReleaseLock(&languageLock);
        
        memFile << "[WRITE] Write to " << std::hex << addr 
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

    memFile << "[BEFORE BALEEN] Object '" << objectName
            << "' occupies " << size
            << " bytes in range [0x" << std::hex << addr
            << ", 0x" << addr + size
            << ")\n" << std::dec << std::endl;

    memFile.flush();
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
        memFile << "[AFTER REALLOC] Object '" << node->object
                << "' was reallocated!" << std::endl;
        
        memFile << "[AFTER REALLOC] - [0x" << std::hex << node->start
                << ", 0x" << node->start + node->size
                << ") → [0x" << ret
                << ", 0x" << ret + args.size
                << ")" << std::dec << std::endl;
        
        memFile << "[AFTER REALLOC] - " << node->size
                << " → " << args.size
                << " bytes\n" << std::endl;
        
        objects.insert(ret, args.size, node->object);
        starts[node->object] = ret;
    }
}

VOID BeforeFree(ADDRINT addr) {
    Node *removed = objects.remove(addr);

    if (removed) {
        memFile << "[BEFORE FREE] Object '" << removed->object
                << "' is no longer mapped to range [0x" << std::hex << removed->start
                << ", 0x" << removed->start + removed->size
                << ")\n" << std::dec << std::endl;
        
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

VOID PushLanguage(THREADID tid, Language lang) {
    PIN_GetLock(&languageLock, tid);
    languageStack[tid].push(lang);
    PIN_ReleaseLock(&languageLock);
}

VOID PopLanguage(THREADID tid) {
    PIN_GetLock(&languageLock, tid);

    if (!languageStack[tid].empty()) {
        languageStack[tid].pop();
    }

    PIN_ReleaseLock(&languageLock);
}

VOID BeforeMalloc(THREADID tid, USIZE size) {
    memFile << "[BEFORE MALLOC] " << size
            << " bytes requested by " << (languageStack[tid].top() == RUST ? "Rust" : "C")
            << "\n" << std::endl;
}

string exportCSV() {
    string sheet = "";

    return sheet;
}

// Image instrumentation function called when each image is loaded
VOID Image(IMG img, VOID *v) {
    memFile << "[PIN] Loading image: " << IMG_Name(img) << endl;
    memFile << "[PIN] Load offset: 0x" << std::hex << IMG_LoadOffset(img) << std::dec << endl;

    outFile << "[PIN] Loading image: " << IMG_Name(img) << endl;
    outFile << "[PIN] Load offset: 0x" << std::hex << IMG_LoadOffset(img) << std::dec << endl;

    Language lang = Language::C;

    if (IMG_IsMainExecutable(img)) {
        // This is the Rust binary being executed
        lang = Language::RUST;
    }

    // Instrument all routines in the main executable
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            string rtnName = RTN_Name(rtn);

            outFile << "Instrumenting " << rtnName << std::endl;

            if (skipFunctions.find(rtnName) != skipFunctions.end()) {
                continue;
            }

            RTN_Open(rtn);

            Language l = c_functions.count(rtnName) == 1 ? Language::C : lang;
            
            RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)PushLanguage,
              IARG_THREAD_ID,
              IARG_UINT32, l,
              IARG_END);
            
            RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)PopLanguage,
                          IARG_THREAD_ID,
                          IARG_END);
            
            RTN_Close(rtn);
        }
    }

    instrument(img, "baleen", IPOINT_BEFORE,
               (AFUNPTR)BeforeBaleen,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // Address
               IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size
               IARG_FUNCARG_ENTRYPOINT_VALUE, 2); // Name

    instrument(img, "free", IPOINT_BEFORE,
               (AFUNPTR)BeforeFree,
               IARG_FUNCARG_ENTRYPOINT_VALUE, 0);

    instrument(img, "malloc", IPOINT_BEFORE,
               (AFUNPTR)BeforeMalloc,
               IARG_THREAD_ID,
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
    
    memFile << endl;
}

// Finalization function
VOID Fini(INT32 code, VOID *v) {
    memFile << "========== Access Summary ==========" << endl;
    
    // Print statistics for each object
    for (const auto& pair : reads) {
        const string& objName = pair.first;
        
        memFile << "Object \"" << objName << "\":" << endl;
        
        // Read counts
        int rustReads = reads[objName][Language::RUST];
        int cReads = reads[objName][Language::C];
        
        memFile << "  Reads  - Rust: " << rustReads 
                << ", C: " << cReads 
                << ", Total: " << (rustReads + cReads) << endl;
        
        // Write counts
        int rustWrites = writes[objName][Language::RUST];
        int cWrites = writes[objName][Language::C];
        
        memFile << "  Writes - Rust: " << rustWrites 
                << ", C: " << cWrites 
                << ", Total: " << (rustWrites + cWrites) << endl;
        
        memFile << endl;
    }
    
    memFile << "==========================================" << endl;
    memFile << "Instrumentation finished" << endl;
    memFile.close();

    // Export CSV
    ofstream csv;

    csv.open("baleen.csv");

    csv << "Name, Reads (Rust), Reads (C), Writes (Rust), Writes (C)" << std::endl;

    for (const auto& pair : reads) {
        const string& objName = pair.first;

        csv << objName << ", ";

        int rustReads = reads[objName][Language::RUST];
        int cReads = reads[objName][Language::C];

        csv << rustReads << ", " << cReads << ", ";

        int rustWrites = writes[objName][Language::RUST];
        int cWrites = writes[objName][Language::C];

        csv << rustWrites << ", " << cWrites << std::endl;
    }

    csv.close();
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
    
    // Open output files
    memFile.open("memory.baleen");
    outFile.open("functions.baleen");

    // Get C functions
    std::ifstream file("c-functions.txt");
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file c-functions.txt" << std::endl;
        return 1;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        c_functions.insert(line);
    }
    
    file.close();
    
    // Register image instrumentation callback
    IMG_AddInstrumentFunction(Image, 0);

    INS_AddInstrumentFunction(Instruction, 0);
    
    // Register finish callback
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program
    PIN_StartProgram();
    
    return 0;
}