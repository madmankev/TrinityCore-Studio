#include "app/ProjectStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <json.hpp>

namespace we
{
using nlohmann::json;

namespace
{
namespace fs = std::filesystem;

// project.json lives directly inside the project folder.
std::string ProjectFilePath(const std::string& folder)
{
    return (fs::path(folder) / "project.json").string();
}

// Serialize a ProjectConfig into a json object (password only when opted in).
json ToJson(const ProjectConfig& p)
{
    json node;
    node["name"] = p.name;
    node["host"] = p.conn.host;
    node["port"] = p.conn.port;
    node["user"] = p.conn.user;
    node["worldDb"] = p.conn.worldDb;
    node["savePassword"] = p.savePassword;
    if (p.savePassword)
        node["password"] = p.conn.password;
    node["clientDataPath"] = p.clientDataPath;
    node["writeMode"] = (p.writeMode == WriteMode::SqlExport) ? "sqlexport" : "live";
    node["exportPath"] = p.exportPath;
    node["reloadAfterSave"] = p.reloadAfterSave;
    node["soapHost"] = p.soapHost;
    node["soapPort"] = p.soapPort;
    node["soapUser"] = p.soapUser;
    if (p.savePassword)
        node["soapPassword"] = p.soapPassword;
    node["windowMaximized"] = p.windowMaximized;
    node["windowWidth"] = p.windowWidth;
    node["windowHeight"] = p.windowHeight;
    return node;
}

// Read a ProjectConfig from a json object. `location` is supplied by the caller (the
// folder it was read from) so the record's location always matches where it lives.
ProjectConfig FromJson(const json& node, const std::string& location)
{
    ProjectConfig p;
    p.location = location;
    p.name = node.value("name", std::string());
    p.conn.host = node.value("host", std::string("127.0.0.1"));
    p.conn.port = static_cast<uint16_t>(node.value("port", 3306));
    p.conn.user = node.value("user", std::string("root"));
    p.conn.worldDb = node.value("worldDb", std::string("world"));
    p.savePassword = node.value("savePassword", false);
    p.conn.password = node.value("password", std::string());
    p.clientDataPath = node.value("clientDataPath", std::string());
    p.writeMode = (node.value("writeMode", std::string("live")) == "sqlexport")
                      ? WriteMode::SqlExport
                      : WriteMode::Live;
    p.exportPath = node.value("exportPath", std::string());
    p.reloadAfterSave = node.value("reloadAfterSave", false);
    p.soapHost = node.value("soapHost", std::string("127.0.0.1"));
    p.soapPort = static_cast<uint16_t>(node.value("soapPort", 7878));
    p.soapUser = node.value("soapUser", std::string());
    p.soapPassword = node.value("soapPassword", std::string());
    p.windowMaximized = node.value("windowMaximized", true);
    p.windowWidth = node.value("windowWidth", 0);
    p.windowHeight = node.value("windowHeight", 0);
    return p;
}

} // namespace

ProjectStore::ProjectStore(std::string path)
    : filePath(std::move(path))
{
}

bool ProjectStore::LoadProject(const std::string& folder, ProjectConfig& out, std::string& err)
{
    const std::string file = ProjectFilePath(folder);
    std::error_code ec;
    if (!fs::exists(file, ec) || ec)
    {
        err = "project.json not found in '" + folder + "'";
        return false;
    }

    std::ifstream in(file, std::ios::in | std::ios::binary);
    if (!in)
    {
        err = "cannot open '" + file + "' for reading";
        return false;
    }

    json root;
    try
    {
        in >> root;
        out = FromJson(root, folder);
    }
    catch (const std::exception& ex)
    {
        err = std::string("parse error: ") + ex.what();
        return false;
    }
    return true;
}

bool ProjectStore::SaveProject(const ProjectConfig& p, std::string& err)
{
    if (p.location.empty())
    {
        err = "project has no location";
        return false;
    }

    std::error_code ec;
    fs::create_directories(p.location, ec);
    if (ec)
    {
        err = "cannot create project folder '" + p.location + "': " + ec.message();
        return false;
    }

    const std::string file = ProjectFilePath(p.location);
    std::ofstream out(file, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err = "cannot open '" + file + "' for writing";
        return false;
    }
    try
    {
        out << ToJson(p).dump(2) << '\n';
    }
    catch (const std::exception& ex)
    {
        err = std::string("write error: ") + ex.what();
        return false;
    }
    if (!out)
    {
        err = "write failed for '" + file + "'";
        return false;
    }
    return true;
}

void ProjectStore::ReloadEntry(const std::string& folder)
{
    ProjectEntry e;
    e.config.location = folder;
    ProjectConfig loaded;
    std::string err;
    if (LoadProject(folder, loaded, err))
    {
        e.config = loaded;
        e.valid = true;
    }
    else
    {
        e.valid = false;
        e.error = err;
        // Keep the folder's leaf name as a fallback display name.
        e.config.name = fs::path(folder).filename().string();
    }

    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const ProjectEntry& x) { return x.config.location == folder; });
    if (it != entries.end())
        *it = std::move(e);
    else
        entries.push_back(std::move(e));
}

DbError ProjectStore::LoadRegistry()
{
    std::error_code ec;
    entries.clear();

    if (!fs::exists(filePath, ec) || ec)
        return DbError{};   // missing registry => empty list

    std::ifstream in(filePath, std::ios::in | std::ios::binary);
    if (!in)
        return DbError{false, "projects: cannot open '" + filePath + "' for reading"};

    json root;
    try
    {
        in >> root;
    }
    catch (const std::exception& ex)
    {
        return DbError{false, std::string("projects: parse error: ") + ex.what()};
    }

    try
    {
        const json* arr = nullptr;
        if (root.is_array())
            arr = &root;
        else if (root.is_object() && root.contains("projects") && root["projects"].is_array())
            arr = &root["projects"];

        if (arr)
        {
            for (const auto& node : *arr)
            {
                // Accept a bare string path or an object { path, name }.
                std::string folder;
                std::string cachedName;
                if (node.is_string())
                    folder = node.get<std::string>();
                else if (node.is_object())
                {
                    folder = node.value("path", std::string());
                    cachedName = node.value("name", std::string());
                }
                if (folder.empty())
                    continue;

                ReloadEntry(folder);
                // If project.json was unreadable, fall back to the registry's cached name.
                if (!entries.empty() && !entries.back().valid && !cachedName.empty())
                    entries.back().config.name = cachedName;
            }
        }
    }
    catch (const std::exception& ex)
    {
        return DbError{false, std::string("projects: malformed registry: ") + ex.what()};
    }

    return DbError{};
}

DbError ProjectStore::SaveRegistry() const
{
    std::error_code ec;
    const fs::path parent = fs::path(filePath).parent_path();
    if (!parent.empty())
    {
        fs::create_directories(parent, ec);
        if (ec)
            return DbError{false, "projects: cannot create directory '" + parent.string() +
                                      "': " + ec.message()};
    }

    json arr = json::array();
    try
    {
        for (const auto& e : entries)
        {
            json node;
            node["path"] = e.config.location;
            node["name"] = e.config.name;   // cache for display when project.json is missing
            arr.push_back(std::move(node));
        }
    }
    catch (const std::exception& ex)
    {
        return DbError{false, std::string("projects: serialize error: ") + ex.what()};
    }

    json root;
    root["projects"] = std::move(arr);

    std::ofstream out(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
        return DbError{false, "projects: cannot open '" + filePath + "' for writing"};
    try
    {
        out << root.dump(2) << '\n';
    }
    catch (const std::exception& ex)
    {
        return DbError{false, std::string("projects: write error: ") + ex.what()};
    }
    if (!out)
        return DbError{false, "projects: write failed for '" + filePath + "'"};
    return DbError{};
}

void ProjectStore::AddOrPromote(const std::string& folder)
{
    if (folder.empty())
        return;   // never register an empty/placeholder path
    // Drop any existing entry for this folder, reload it fresh, and move to the front.
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const ProjectEntry& x) { return x.config.location == folder; }),
                  entries.end());
    ReloadEntry(folder);   // appends at the back
    if (entries.size() > 1)
        std::rotate(entries.begin(), entries.end() - 1, entries.end());
    SaveRegistry();
}

void ProjectStore::Remove(const std::string& folder)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const ProjectEntry& x) { return x.config.location == folder; }),
                  entries.end());
    SaveRegistry();
}

} // namespace we
