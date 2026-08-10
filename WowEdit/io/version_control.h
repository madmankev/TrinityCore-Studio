#pragma once
#include <string>
#include <vector>
namespace wowedit
{
struct VersionControlResult { int exitCode = -1; std::string output; bool ok() const { return exitCode == 0; } };
class VersionControl
{
public:
    VersionControlResult status(const std::string& repositoryPath) const;
    VersionControlResult commit(const std::string& repositoryPath, const std::string& message) const;
    VersionControlResult history(const std::string& repositoryPath, int count = 30) const;
private:
    VersionControlResult run(const std::string& repositoryPath, const std::string& command) const;
};
} // namespace wowedit
