#pragma once

// Errors: the user sees a short message and a reference; the detail goes to the
// log (stderr and ~/.local/state/afx/afx.log), filed under the same reference.

#include <string>

namespace afx
{
// Logs `detail` under a new reference and returns "<message> (ref AF-XXXXXX)".
std::string reportError (const std::string& message, const std::string& detail);
std::string logPath();
} // namespace afx
