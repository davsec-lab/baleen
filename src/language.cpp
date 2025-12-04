#include "language.h"

string LanguageToString(Language lang) {
    switch (lang) {
    case Language::RUST:
        return "RUST";
    default:
        return "C";
    }
}

LanguageTracker::LanguageTracker() {
	log.open(".baleen/foreigns.log");
}

VOID LanguageTracker::CheckState(THREADID tid, Language newLang, const string* rtnName) {
	// Obtain the lock
	PIN_GetLock(&lock, tid + 1);

	// Get the current language
	Language curLang = language[tid];

	// If the language hasn't changed, do nothing
	if (newLang == curLang) {
		// We must release the lock before returning!
		PIN_ReleaseLock(&lock);
		return;
	}

	// A transition has occurred!
	log << LanguageToString(curLang) << " → " << LanguageToString(newLang) << " (at " << *rtnName << ")" << endl;

	// Update the thread's current language
	language[tid] = newLang;
	
	// Release the lock
	PIN_ReleaseLock(&lock);
}

Language LanguageTracker::GetCurrent(THREADID tid) {
	// Obtain the lock
	PIN_GetLock(&lock, tid + 1);

	Language lang = language[tid];

	// Release the lock
	PIN_ReleaseLock(&lock);

	return lang;
}