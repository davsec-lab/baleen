#include "../include/language.h"

namespace baleen {

LanguageTracker::LanguageTracker() {
  PIN_InitLock(&lock);

  // Initialize skip functions
  skipped.insert("malloc");
  skipped.insert("realloc");
  skipped.insert("free");
  skipped.insert("baleen");
}

LanguageTracker::~LanguageTracker() { delete &lock; }

void LanguageTracker::push(THREADID tid, Language lang) {
  PIN_GetLock(&lock, tid);
  languageStack[tid].push(lang);
  PIN_ReleaseLock(&lock);
}

void LanguageTracker::pop(THREADID tid) {
  PIN_GetLock(&lock, tid);

  if (!languageStack[tid].empty()) {
    languageStack[tid].pop();
  }

  PIN_ReleaseLock(&lock);
}

Language LanguageTracker::get(THREADID tid) const {
  PIN_GetLock(&lock, tid);

  Language lang = Language::C;
  if (!languageStack[tid].empty()) {
    lang = languageStack[tid].top();
  }

  PIN_ReleaseLock(&lock);
  return lang;
}

bool LanguageTracker::isC(const std::string &functionName) const {
  // TODO: Implement by checking the mangling
  return false;
}

bool LanguageTracker::shouldSkip(const std::string &functionName) const {
  return skipped.count(functionName) > 0;
}

} // namespace baleen
