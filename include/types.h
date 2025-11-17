#ifndef BALEEN_TYPES_H
#define BALEEN_TYPES_H

#include <string>

namespace baleen {

enum class Language { RUST = 0, C = 1 };

inline std::string to_string(Language lang) {
  return (lang == Language::RUST) ? "Rust" : "C";
}

} // namespace baleen

#endif // BALEEN_TYPES_H
