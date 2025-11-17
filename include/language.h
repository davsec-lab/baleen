#ifndef BALEEN_LANGUAGE_TRACKER_H
#define BALEEN_LANGUAGE_TRACKER_H

#include "pin.H"
#include "types.h"

#include <map>
#include <set>
#include <stack>
#include <string>

namespace baleen {

class LanguageTracker {
public:
  LanguageTracker();
  ~LanguageTracker();

  // Push the language context for a thread
  void push(THREADID tid, Language lang);

  // Pop the language context for a thread
  void pop(THREADID tid);

  // Get the current language for a thread
  Language get(THREADID tid) const;

  // Determine if a function is a C function
  bool isC(const std::string &functionName) const;

  // Check if function should be skipped for instrumentation
  bool shouldSkip(const std::string &functionName) const;

private:
  mutable PIN_LOCK lock;
  std::map<THREADID, std::stack<Language>> languageStack;
  std::set<std::string> skipped;
};

} // namespace baleen

#endif // BALEEN_LANGUAGE_TRACKER_H
