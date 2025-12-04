#include "language.h"

string LanguageToString(Language lang) {
    switch (lang) {
    case Language::RUST:
        return "RUST";
    default:
        return "C";
    }
}

Language LanguageTracker::GetCurrent(THREADID tid) {
	PIN_GetLock(&lock, tid + 1);
	Language lang = language[tid];
	PIN_ReleaseLock(&lock);

	return lang;
}

VOID LanguageTracker::Enter(THREADID tid, Language newLang) {
	PIN_GetLock(&lock, tid + 1);

	// Get current language
	Language curLang = language[tid];

	// Remember the current language for when this function exits
	remembered[tid].push(curLang);

	// Update the new language
	language[tid] = newLang;

	log << "[LANGUAGE] " << LanguageToString(curLang)
		<< " → " << LanguageToString(newLang) << endl;

	PIN_ReleaseLock(&lock);
}

VOID LanguageTracker::Exit(THREADID tid) {
	PIN_GetLock(&lock, tid + 1);

	// Get the current language and the language of our caller
	Language curLang = language[tid];
	Language newLang = remembered[tid].top();

	// Pop the current language since this function is done
	remembered[tid].pop();

	// Set language back to what it was before function call
	language[tid] = newLang;

	log << "[LANGUAGE] " << LanguageToString(curLang)
		<< " → " << LanguageToString(newLang) << endl;

	PIN_ReleaseLock(&lock);	
}