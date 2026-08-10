#include "io/version_control.h"
#include <cstdio>
#include <filesystem>
namespace wowedit
{
namespace
{
#if defined(_WIN32)
FILE* OpenPipe(const char* command) { return _popen(command, "r"); }
int ClosePipe(FILE* pipe) { return _pclose(pipe); }
#else
FILE* OpenPipe(const char* command) { return popen(command, "r"); }
int ClosePipe(FILE* pipe) { return pclose(pipe); }
#endif
} // namespace
VersionControlResult VersionControl::status(const std::string& path) const { return run(path, "git status --short"); }
VersionControlResult VersionControl::commit(const std::string& path, const std::string& message) const { return run(path, "git add -A && git commit -m \"" + message + "\""); }
VersionControlResult VersionControl::history(const std::string& path, int count) const { return run(path, "git log --oneline -" + std::to_string(count)); }
VersionControlResult VersionControl::run(const std::string& path, const std::string& command) const
{
    // A UI host should expose a reviewed command preview before invoking this adapter.
    const std::string full = "cd \"" + path + "\" && " + command + " 2>&1";
    FILE* pipe = OpenPipe(full.c_str()); if (!pipe) return {-1, "Could not launch git"};
    VersionControlResult result; char buffer[512]; while (fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
    result.exitCode = ClosePipe(pipe); return result;
}
} // namespace wowedit
