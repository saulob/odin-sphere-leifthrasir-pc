// The launcher's and the in-game menus' text lookup. Every string is English for now; drawing code keeps calling Tr so a
// translation can be added without touching it.
#pragma once

#include <string>

namespace osl {

inline const char* Tr(const char* english) { return english; }
inline std::string Tr(const std::string& english) { return english; }
inline std::string TrValue(const std::string& english) { return english; }

} // namespace osl
