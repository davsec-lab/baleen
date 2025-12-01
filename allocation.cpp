#include "allocation.h"

AllocationTracker::AllocationTracker() {
	log.open(".baleen/allocation.log");
}

VOID AllocationTracker::Allocate(THREADID tid, UINT64 bytes, Language lang) {
	allocations[lang] += bytes;
}

VOID AllocationTracker::BeforeMalloc(THREADID tid, UINT64 bytes, Language lang) {
	PIN_GetLock(&lock, tid + 1);

	auto id = counter[tid]["malloc"]++;
	pending[tid]["malloc"] = { id, bytes };

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::AfterMalloc(THREADID tid, ADDRINT returned, Language lang) {
	PIN_GetLock(&lock, tid + 1);

	auto payload = pending[tid]["malloc"];

	if (returned != 0) {
		USIZE size = payload.second;
		Allocate(tid, size, lang);
	} else {
		log << "[AFTER MALLOC] [" << payload.first << "] 'malloc' failed" << endl;
	}

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::BeforeRealloc(THREADID tid, ADDRINT oldAddr, USIZE size, Language lang) {
	log << "[BEFORE REALLOC]" << endl;
}

VOID AllocationTracker::AfterRealloc(THREADID tid, ADDRINT newAddr, ObjectTracker& objectTracker) {
	log << "[AFTER REALLOC]" << endl;
}

VOID AllocationTracker::Report(ofstream& stream) {
	auto rustBytes = allocations[Language::RUST];
	auto cBytes = allocations[Language::C];
	auto sharedBytes = allocations[Language::SHARED];

	stream << endl << "--- Allocation Report ---" << endl;
	stream << "Rust:   " << rustBytes << " bytes" << endl;
	stream << "C:      " << cBytes << " bytes" << endl;
	stream << "Shared: " << sharedBytes << " bytes" << endl;
	stream << "Total:  " << (rustBytes + cBytes + sharedBytes) << " bytes" << endl;
}