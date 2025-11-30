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
ofstream calls;

static set<string> rtn_names; 

string LanguageToString(Language lang) {
    switch (lang) {
    case Language::RUST:
        return "RUST";
    case Language::C:
        return "C";
    default:
        return "SHARED";
    }
}

BOOL EndsWith(std::string_view s, std::string_view suffix) {
    if (s.length() < suffix.length()) {
        // String is too short
        return false;
    }

    // Extract the substring starting from the position that would be the beginning of the suffix
    size_t start_pos = s.length() - suffix.length();
    return s.substr(start_pos) == suffix;
}

BOOL IsRustModern(const string& name) {
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

BOOL IsRustLegacy(const string& name) {
    return name.find("___rust") != string::npos;
}

BOOL IsRuntime(const string& name) {
    static const set<string> names = {
        "_start",
        "deregister_tm_clones",
        "register_tm_clones",
        "__do_global_dtors_aux",
        "frame_dummy",
        "main",
        "rust_eh_personality",
        ".init",
        "_init",
        ".fini",
        "_fini",
        ".plt",
        ".plt.got",
        ".plt.sec",
        ".text",
    };

    return names.count(name) > 0;
}

BOOL IsStub(const string& name) {
    return EndsWith(name, "@plt");
}

BOOL IsRust(const string& name) {
    return IsRustModern(name) || IsRustLegacy(name);
}

INT32 Usage() {
    cerr << "Baleen 🐋" << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID EnterC(THREADID tid, const string* rtnName) {
    calls << "Entering " << *rtnName << " (C)" << endl;
    language[tid].push(Language::C);
    calls << "Done" << endl;
}

VOID EnterRust(THREADID tid, const string* rtnName) {
    calls << "Entering " << *rtnName << " (RUST)" << endl;
    language[tid].push(Language::RUST);
    calls << "Done" << endl;
}

VOID LeaveLanguage(THREADID tid, const string* rtnName) {
    calls << "Leaving " << *rtnName << endl;
    language[tid].pop();
    calls << "Done" << endl;
}

BOOL IMG_IsVdso(IMG img) {
    string imgName = IMG_Name(img);

    // Check for common vDSO names
    static const set<string> names = {
        "[vdso]",
        "[linux-gate.so.1]",
        "[linux-vdso.so.1]"
    };

    return names.count(imgName) > 0;
}

Language RTN_Language(IMG img, RTN rtn) {
    if (IMG_IsInterpreter(img) || IMG_IsVdso(img)) {
        return Language::SHARED;
    }

    string rtnName = RTN_Name(rtn);

    if (IsStub(rtnName) || IsRuntime(rtnName)) {
        return Language::SHARED;
    }

    if (IMG_IsMainExecutable(img)) {
        return IsRust(rtnName) ? Language::RUST : Language::C;
    }

    return Language::C;
}

VOID Image(IMG img, VOID *v) {
    messages << "Loading image... " << IMG_Name(img) << endl;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            string rtnName = RTN_Name(rtn);

            // Determine the routine language
            Language language = RTN_Language(img, rtn);

            messages << "Inspecting " << rtnName << " (" << LanguageToString(language) << ")" << endl;

            RTN_Open(rtn);

            if (language != Language::SHARED) {
                auto EnterLanguage = language == Language::RUST ? EnterRust : EnterC;

                // Insert the routine name into our set to get a stable pointer
                auto it = rtn_names.insert(rtnName);
                const string* rtnNamePtr = &(*it.first);

                // Insert call at entry
                RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) EnterLanguage,
                               IARG_THREAD_ID,
                               IARG_PTR, rtnNamePtr,
                               IARG_END);
                
                // Insert call at exit
                RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) LeaveLanguage,
                               IARG_THREAD_ID,
                               IARG_PTR, rtnNamePtr,
                               IARG_END);
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

    // Open logging files
    messages.open("baleen-messages.log");
    calls.open("baleen-calls.log");

    // Register image instrumentation callback
    IMG_AddInstrumentFunction(Image, 0);

    // Start the program
    PIN_StartProgram();

    // Close logging files
    messages.close();
    calls.close();

    return 0;
}