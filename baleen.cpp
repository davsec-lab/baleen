#include <iostream>
#include <fstream>
#include <ostream>
#include <map>
#include <optional>
#include <vector>

#include "pin.H"

using std::cerr;
using std::endl;
using std::string;

// Output file for logging
std::ofstream outFile;

template<typename T>
class RangeMap {
private:
    struct RangeValue {
        ADDRINT end;
        T value;
    };
    
    std::map<ADDRINT, RangeValue> ranges;  // key: start of range

public:
    void addRange(ADDRINT start, ADDRINT end, T value) {
        ranges[start] = {end, value};
    }
    
    std::optional<T> get(ADDRINT key) const {
        auto it = ranges.upper_bound(key);
        
        if (it != ranges.begin()) {
            --it;
            if (key <= it->second.end) {
                return it->second.value;
            }
        }
        
        return std::nullopt;
    }

    bool updateByKey(ADDRINT key, T newValue) {
        auto it = ranges.upper_bound(key);
        
        if (it != ranges.begin()) {
            --it;
            if (key <= it->second.end) {
                it->second.value = newValue;
                return true;
            }
        }
        return false;  // Key not in any range
    }

    bool removeRange(ADDRINT start) {
        auto it = ranges.find(start);
        if (it != ranges.end()) {
            ranges.erase(it);
            return true;
        }
        return false;
    }
};

RangeMap<std::string> addressToName;  // Maps address ranges to object names
std::map<std::string, ADDRINT> nameToRanges;  // Maps names to their start address
std::map<std::string, int> nameToCount;  // Maps object names to read counts

VOID RecordMemRead(ADDRINT ip, ADDRINT addr) {
    auto name = addressToName.get(addr);

    if (name) {
        outFile << "[PIN] Read at 0x" << std::hex << ip 
                << ": address 0x" << addr 
                << " (object: " << *name << ")" << std::dec << endl;

        nameToCount[*name]++;  // Increment count for this object name
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
    addressToName.addRange(pointerArg, pointerArg + sizeArg - 1, objectName);
    nameToRanges[objectName] = pointerArg;
    
    // Initialize the count for this object name (if not already present)
    if (nameToCount.find(objectName) == nameToCount.end()) {
        nameToCount[objectName] = 0;
    }

    std::cout << "Foo" << std::endl;

    outFile << "[PIN] _scampi_register called from: 0x" << std::hex << callSite
            << ", return value: 0x" << returnValue << std::dec << endl;
    
    outFile << "[PIN] Pointer argument: 0x" << std::hex << pointerArg << std::dec << endl;
    outFile << "[PIN] Size argument: 0x" << std::hex << sizeArg << std::dec << endl;
    outFile << "[PIN] Object name: \"" << objectName << "\"" << endl;

    outFile.flush();
}

VOID AfterScampiUnregister(ADDRINT callSite, ADDRINT stringArg) {
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

    outFile << "[PIN] _scampi_unregister called from: 0x" << std::hex << callSite << std::dec << endl;
    outFile << "[PIN] Unregistering object: \"" << objectName << "\"" << endl;

    // Look up the start address for this name
    auto it = nameToRanges.find(objectName);
    if (it != nameToRanges.end()) {
        addressToName.removeRange(it->second);
        nameToRanges.erase(it);
    }

    outFile.flush();
}

VOID AfterMemcpy(ADDRINT ret) {
    std::cout << "memcpy called" << std::endl;
}

VOID AfterRealloc(ADDRINT ret) {
    std::cout << "realloc called" << std::endl;
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
    
    outFile << "[PIN] Searching for _scampi_unregister..." << endl;
    SYM unregisterSym = SYM_Invalid();
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
        string name = SYM_Name(sym);
        if (name == "_scampi_unregister") {
            outFile << "[PIN] Found symbol _scampi_unregister at 0x" 
                    << std::hex << SYM_Address(sym) << std::dec << endl;
            unregisterSym = sym;
            
            RTN rtn = RTN_FindByAddress(SYM_Address(sym));
            if (RTN_Valid(rtn)) {
                outFile << "[PIN] Got RTN from symbol: " << RTN_Name(rtn) << endl;
                
                RTN_Open(rtn);
                RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)AfterScampiUnregister,
                                IARG_RETURN_IP,
                                IARG_REG_VALUE, REG_RDI,  // First argument (string pointer)
                                IARG_END);
                RTN_Close(rtn);
                
                outFile << "[PIN] *** Successfully instrumented _scampi_unregister!" << endl;
            } else {
                outFile << "[PIN] Could not get valid RTN from symbol address" << endl;
            }
            break;
        }
    }

    if (!SYM_Valid(unregisterSym)) {
        outFile << "[PIN] Symbol _scampi_unregister not found in symbol table" << endl;
    }

    if (!SYM_Valid(scampiSym)) {
        outFile << "[PIN] Symbol _scampi_register not found in symbol table" << endl;
    }

    // Find memcpy in the image
    RTN memcpyRtn = RTN_FindByName(img, "memcpy");
    
    if (RTN_Valid(memcpyRtn)) {
        RTN_Open(memcpyRtn);
        
        // Insert call after memcpy returns
        RTN_InsertCall(memcpyRtn, IPOINT_AFTER, 
                      (AFUNPTR)AfterMemcpy,
                      IARG_FUNCRET_EXITPOINT_VALUE,
                      IARG_END);
        
        RTN_Close(memcpyRtn);
    }

    // Find realloc in the image
    RTN reallocRtn = RTN_FindByName(img, "realloc");
    
    if (RTN_Valid(reallocRtn)) {
        RTN_Open(reallocRtn);
        
        // Insert call after memcpy returns
        RTN_InsertCall(reallocRtn, IPOINT_AFTER, 
                      (AFUNPTR)AfterRealloc,
                      IARG_FUNCRET_EXITPOINT_VALUE,
                      IARG_END);
        
        RTN_Close(reallocRtn);
    }
    
    outFile << endl;
}

// Finalization function
VOID Fini(INT32 code, VOID *v) {
    outFile << "\n[PIN] ========== Read Count Summary ==========" << endl;
    
    for (const auto& pair : nameToCount) {
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