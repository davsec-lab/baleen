#ifndef ALLOCATION_H
#define ALLOCATION_H

#include "pin.H"

#include "language.h"
#include "object.h"

using std::ofstream;
using std::map;
using std::pair;
using std::get;
using std::tuple;

class AllocationTracker {
private:
	PIN_LOCK lock;
	ofstream log;

	map<Language, UINT64> allocations;

	map<THREADID, pair<UINT64, USIZE>> pendingMalloc;
	map<THREADID, tuple<UINT64, ADDRINT, USIZE>> pendingRealloc;

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