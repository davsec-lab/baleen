#include "allocation.h"
#include "logger.h"

AllocationTracker::AllocationTracker(Logger& l) : logger(l) {
}

VOID AllocationTracker::Allocate(THREADID tid, UINT64 bytes, Language lang) {
	allocations[lang] += bytes;
}

VOID AllocationTracker::BeforeMalloc(THREADID tid, UINT64 bytes, Language lang) {
	PIN_GetLock(&lock, tid + 1);

	auto id = counter[tid]["malloc"]++;
	pendingMalloc[tid] = { id, bytes };

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::AfterMalloc(THREADID tid, ADDRINT returned, Language lang, ObjectTracker& objectTracker) {
	PIN_GetLock(&lock, tid + 1);

	auto payload = pendingMalloc[tid];

	if (returned != 0) {
		USIZE size = payload.second;
		Allocate(tid, size, lang);

		// Register an object
		objectTracker.RegisterObject(tid, returned, size, lang, 0);
	} else {
		logger.Stream(LogSubject::MEMORY) << "[AFTER MALLOC] [" << payload.first << "] 'malloc' failed" << endl;
	}

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::BeforePosixMemalign(THREADID tid, ADDRINT memptr_addr, USIZE alignment, USIZE size, Language lang) {
	PIN_GetLock(&lock, tid + 1);

	auto id = counter[tid]["posix_memalign"]++;
	pendingPosixMemalign[tid] = { id, memptr_addr, size };

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::AfterPosixMemalign(THREADID tid, ADDRINT memptr_addr, INT32 result, Language lang, ObjectTracker& objectTracker) {
	PIN_GetLock(&lock, tid + 1);

	auto payload = pendingPosixMemalign[tid];

	if (result == 0) {  // posix_memalign returns 0 on success
		ADDRINT returned;
		PIN_SafeCopy(&returned, (VOID*)memptr_addr, sizeof(ADDRINT));
		
		USIZE size = get<2>(payload);
		Allocate(tid, size, lang);
		objectTracker.RegisterObject(tid, returned, size, lang, 0);
	} else {
		logger.Stream(LogSubject::MEMORY) << "[AFTER POSIX_MEMALIGN] [" << get<0>(payload) << "] 'posix_memalign' failed with code " << result << endl;
	}

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::BeforeRealloc(THREADID tid, ADDRINT addr, USIZE size, Language lang) {
	PIN_GetLock(&lock, tid + 1);

	logger.Stream(LogSubject::MEMORY) << "[BEFORE REALLOC]" << endl;

	auto id = counter[tid]["realloc"]++;
	pendingRealloc[tid] = { id, addr, size };

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::AfterRealloc(THREADID tid, ADDRINT newAddr, ObjectTracker& objectTracker) {
	PIN_GetLock(&lock, tid + 1);

	logger.Stream(LogSubject::MEMORY) << "[AFTER REALLOC]" << endl;

	auto payload = pendingRealloc[tid];

	ADDRINT oldAddr = get<1>(payload);
	USIZE size = get<2>(payload);
	
	objectTracker.MoveObject(tid, oldAddr, newAddr, size);

	PIN_ReleaseLock(&lock);
}

VOID AllocationTracker::BeforeFree(THREADID tid, ADDRINT addr, ObjectTracker& objectTracker) {
	objectTracker.RemoveObject(tid, addr);
}

VOID AllocationTracker::Report(ofstream& stream) {
	auto rustBytes = allocations[Language::RUST];
	auto cBytes = allocations[Language::C];

	stream << endl << "--- Allocation Report ---" << endl;
	stream << "Rust:   " << rustBytes << " bytes" << endl;
	stream << "C:      " << cBytes << " bytes" << endl;
	stream << "Total:  " << (rustBytes + cBytes) << " bytes" << endl;
}