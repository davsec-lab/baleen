#ifndef ALLOCATION_H
#define ALLOCATION_H

#include "pin.H"

#include "language.h"
#include "object.h"

using std::ofstream;
using std::map;
using std::pair;

class AllocationTracker {
private:
	PIN_LOCK lock;
	ofstream log;

	map<Language, UINT64> allocations;
	map<THREADID, map<string, pair<UINT64, USIZE>>> pending;
	map<THREADID, map<string, UINT64>> counter;
	
	VOID Allocate(THREADID tid, UINT64 bytes, Language lang);

public:
	AllocationTracker();

	VOID BeforeMalloc(THREADID tid, UINT64 bytes, Language lang);
	VOID AfterMalloc(THREADID tid, ADDRINT returned, Language lang);

	VOID BeforeRealloc(THREADID tid, ADDRINT oldAddr, USIZE size, Language lang);
	VOID AfterRealloc(THREADID tid, ADDRINT newAddr, ObjectTracker& objectTracker);

	VOID Report(ofstream& stream);
};

#endif // ALLOCATION_H