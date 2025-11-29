#include "pin.H"

#include <iostream>
#include <fstream>
#include <set>

using std::map;
using std::stack;
using std::set;
using std::string;
using std::cerr;
using std::endl;
using std::ofstream;

enum class Language { RUST, C, SHARED };

map<THREADID, stack<Language>> language;
ofstream messages;

string languageToString(Language lang) {
    switch (lang) {
    case Language::RUST:
        return "RUST";
    case Language::C:
        return "C";
    default:
        return "SHARED";
    }
}

bool endsWith(std::string_view s, std::string_view suffix) {
    if (s.length() < suffix.length()) {
        // String is too short
        return false;
    }

    // Extract the substring starting from the position that would be the beginning of the suffix
    size_t start_pos = s.length() - suffix.length();
    return s.substr(start_pos) == suffix;
}

bool isRustModern(const string& name) {
    size_t len = name.length();

    // Is the name long enough for the suffix?
    if (len < 19) {
        return false;
    }

    // Is there an "E" at the end?
    if (name[len - 1] != 'E') {
        return false;
    }

    // Does the hash have a "17h" prefix?
    if (name[len - 20] != '1' || name[len - 19] != '7' || name[len - 18] != 'h') {
        return false;
    }
    
    // Are the last 16 characters hexadecimal?
    for (size_t i = len - 17; i < len - 1; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }

    // Does the name use the Itanium ABI prefix?
    if (name.rfind("_ZN", 0) != 0) {
        return false;
    }

    // If all checks passed, it's a Rust v0 function
    return true;
}

bool isRustLegacy(const string& name) {
    return name.find("___rust") != string::npos;
}

bool isRustRuntime(const string& name) {
    if (endsWith(name, "@plt")) {
        return true;
    }

    static const set<string> functions = {
        "_start",
        "deregister_tm_clones",
        "register_tm_clones",
        "__do_global_dtors_aux",
        "frame_dummy",
        "main",
        "rust_eh_personality",
        "_init",
        "_fini",
        ".plt",
    };

    return functions.count(name) > 0;
}

bool isRust(const string& name) {
    return isRustModern(name) || isRustLegacy(name) || isRustRuntime(name);
}

INT32 Usage() {
    cerr << "Baleen 🐋" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID enterRust(THREADID tid) {
    language[tid].push(Language::RUST);
}

VOID enterC(THREADID tid) {
    language[tid].push(Language::C);
}

VOID leaveLanguage(THREADID tid) {
    language[tid].pop();
}

VOID Image(IMG img, VOID *v) {
    messages << "Loading image... " << IMG_Name(img) << endl;

    bool isMain = IMG_IsMainExecutable(img);

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            string rtnName = RTN_Name(rtn);

            Language lang = Language::SHARED;

            if (isMain) {
                lang = isRust(rtnName) ? Language::RUST : Language::C;
            }

            messages << "Inspecting " << rtnName << " (" << languageToString(lang) << ")" << endl;

            // Instrument the routine
            RTN_Open(rtn);

            // RTN_InsertCall(rtn, ipoint, analysisFunc, args..., IARG_END);

            if (lang != Language::SHARED) {
                auto enterLanguage = lang == Language::RUST ? enterRust : enterC;
                
                RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) enterLanguage, IARG_THREAD_ID, IARG_END);
                RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) leaveLanguage, IARG_THREAD_ID, IARG_END);
            }

            RTN_Close(rtn);
        }
    }

    messages << endl;
}

int main(int argc, char *argv[]) {
    // Initialize symbol processing
    PIN_InitSymbols();
    
    // Initialize Pin
    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    // Open logging file
    messages.open("baleen.log");

    // Register image instrumentation callback
    IMG_AddInstrumentFunction(Image, 0);

    // Start the program
    PIN_StartProgram();

    // Close logging file
    messages.close();

    return 0;
}