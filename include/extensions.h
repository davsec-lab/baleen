#ifndef EXTENSIONS_H
#define EXTENSIONS_H

#include "pin.H"

#include "language.h"
#include "utilities.h"

#include <set>

using std::string;
using std::set;

BOOL IMG_IsVdso(IMG img);

BOOL RTN_IsRuntime(RTN rtn);

BOOL RTN_IsPLTStub(RTN rtn);

BOOL RTN_IsMain(RTN rtn);

BOOL RTN_IsRustModern(RTN rtn);

BOOL RTN_IsRustLegacy(RTN rtn);

BOOL RTN_IsRust(RTN rtn);

Language RTN_Language(IMG img, RTN rtn);

template<typename... Args>
VOID RTN_InstrumentByName(IMG img, const char* name, IPOINT ipoint, AFUNPTR fun, Args... args) {
	RTN rtn = RTN_FindByName(img, name);

	if (RTN_Valid(rtn)) {
        RTN_Open(rtn);
        
        RTN_InsertCall(rtn, ipoint, fun, args..., IARG_END);
        
        RTN_Close(rtn);
    }
}

#endif // EXTENSIONS_H