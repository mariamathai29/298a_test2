#include "Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <random>

namespace afx
{
std::string logPath()
{
    namespace fs = std::filesystem;
    fs::path dir;
    if (const char* s = std::getenv ("XDG_STATE_HOME"); s != nullptr && *s != 0) dir = fs::path (s) / "afx";
    else if (const char* h = std::getenv ("HOME"); h != nullptr && *h != 0) dir = fs::path (h) / ".local" / "state" / "afx";
    else dir = fs::temp_directory_path() / "afx";
    std::error_code ec;
    fs::create_directories (dir, ec);
    return (dir / "afx.log").string();
}

std::string reportError (const std::string& message, const std::string& detail)
{
    static std::mutex mutex;
    static std::mt19937 rng ((unsigned) std::chrono::steady_clock::now().time_since_epoch().count());
    std::lock_guard<std::mutex> l (mutex);
    char ref[16];
    std::snprintf (ref, sizeof ref, "AF-%06X", (unsigned) (rng() & 0xffffff));

    const auto now = std::time (nullptr);
    char when[32];
    std::strftime (when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime (&now));
    std::fprintf (stderr, "[%s] %s: %s: %s\n", when, ref, message.c_str(), detail.c_str());
    if (std::FILE* f = std::fopen (logPath().c_str(), "a"))
    {
        std::fprintf (f, "[%s] %s: %s: %s\n", when, ref, message.c_str(), detail.c_str());
        std::fclose (f);
    }
    return message + " (ref " + ref + ")";
}
} // namespace afx
