#include "extensions.h"

BOOL IMG_IsVdso(IMG img) {
    string imgName = IMG_Name(img);
    static const set<string> names = {"[vdso]", "[linux-gate.so.1]", "[linux-vdso.so.1]"};
    return names.count(imgName) > 0;
}

BOOL RTN_IsRuntime(RTN rtn) {
	string name = RTN_Name(rtn);

    static const set<string> names = {
        "_start", "deregister_tm_clones", "register_tm_clones", "__do_global_dtors_aux",
        "frame_dummy", "rust_eh_personality", ".init", "_init", ".fini", "_fini",
        ".plt", ".plt.got", ".plt.sec", ".text", "__rust_try", ""
    };

    return names.count(name) > 0;
}

BOOL RTN_IsPLTStub(RTN rtn) {
	string name = RTN_Name(rtn);
    return EndsWith(name, "@plt");
}

BOOL RTN_IsMain(RTN rtn) {
	return RTN_Name(rtn) == "main";
}

BOOL RTN_IsRustModern(RTN rtn) {
	string name = RTN_Name(rtn);

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

BOOL RTN_IsRustLegacy(RTN rtn) {
	string name = RTN_Name(rtn);
    return name.find("___rust") != string::npos;
}

BOOL RTN_IsRust(RTN rtn) {
    return RTN_IsRustModern(rtn) || RTN_IsRustLegacy(rtn) || RTN_IsMain(rtn);
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

    string rtnName = RTN_Name(rtn);

    if (RTN_IsPLTStub(rtn) || RTN_IsRuntime(rtn)) {
        return Language::SHARED;
    }

    if (IMG_IsMainExecutable(img)) {
        return RTN_IsRust(rtn) ? Language::RUST : Language::C;
    }
    return Language::C;
}
