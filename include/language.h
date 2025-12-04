#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "pin.H"

#include <stack>

#include <string>
#include <map>
#include <iostream>
#include <fstream>

using std::string;
using std::map;
using std::ofstream;
using std::endl;
using std::stack;

enum class Language {
    RUST,
    C
};

string LanguageToString(Language lang);

class LanguageTracker {
private:
	PIN_LOCK lock;
	map<THREADID, Language> language;
	map<THREADID, stack<Language>> remembered;
	ofstream& log;

public:
	LanguageTracker(ofstream& l): log(l) {}
	
	VOID CheckState(THREADID tid, Language newLang, const string* rtnName);
	
	Language GetCurrent(THREADID tid);

	VOID Enter(THREADID tid, Language newLang) {
		PIN_GetLock(&lock, tid + 1);

		Language curLang = language[tid];

		remembered[tid].push(curLang);

		language[tid] = newLang;

		log << "[LANGUAGE]" << LanguageToString(curLang)
			<< " → " << LanguageToString(newLang) << endl;

		PIN_ReleaseLock(&lock);
	}

	VOID Exit(THREADID tid) {
		PIN_GetLock(&lock, tid + 1);

		Language curLang = language[tid];
		Language newLang = remembered[tid].top();

		remembered[tid].pop();

		language[tid] = newLang;

		log << "[LANGUAGE]" << LanguageToString(curLang)
			<< " → " << LanguageToString(newLang) << endl;

		PIN_ReleaseLock(&lock);	
	}
};

#endif // LANGUAGE_H