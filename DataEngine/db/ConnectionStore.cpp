#include "db/ConnectionStore.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <json.hpp>

namespace we
{
using nlohmann::json;

ConnectionStore::ConnectionStore(std::string path)
    : filePath(std::move(path))
{
}

DbError ConnectionStore::Load()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(filePath, ec) || ec)
    {
        // Missing file is not an error: start with an empty list.
        profiles.clear();
        return DbError{};
    }

    std::ifstream in(filePath, std::ios::in | std::ios::binary);
    if (!in)
    {
        DbError err;
        err.ok = false;
        err.message = "connections: cannot open '" + filePath + "' for reading";
        return err;
    }

    json root;
    try
    {
        in >> root;
    }
    catch (const std::exception& ex)
    {
        DbError err;
        err.ok = false;
        err.message = std::string("connections: parse error: ") + ex.what();
        return err;
    }

    std::vector<ConnectionProfile> loaded;
    try
    {
        // Accept either a top-level array or an object with a "profiles" array.
        const json* arr = nullptr;
        if (root.is_array())
            arr = &root;
        else if (root.is_object() && root.contains("profiles") && root["profiles"].is_array())
            arr = &root["profiles"];

        if (arr)
        {
            for (const auto& node : *arr)
            {
                ConnectionProfile p;
                p.name = node.value("name", std::string());
                p.config.host = node.value("host", std::string("127.0.0.1"));
                p.config.port = static_cast<uint16_t>(node.value("port", 3306));
                p.config.user = node.value("user", std::string("root"));
                p.config.worldDb = node.value("worldDb", std::string("world"));
                p.savePassword = node.value("savePassword", false);
                // Password only present when it was opted-in on save.
                p.config.password = node.value("password", std::string());
                loaded.push_back(std::move(p));
            }
        }
    }
    catch (const std::exception& ex)
    {
        DbError err;
        err.ok = false;
        err.message = std::string("connections: malformed profile: ") + ex.what();
        return err;
    }

    profiles = std::move(loaded);
    return DbError{};
}

DbError ConnectionStore::Save() const
{
    namespace fs = std::filesystem;

    // Ensure the parent directory (e.g. config/) exists.
    std::error_code ec;
    const fs::path parent = fs::path(filePath).parent_path();
    if (!parent.empty())
    {
        fs::create_directories(parent, ec);
        if (ec)
        {
            DbError err;
            err.ok = false;
            err.message = "connections: cannot create directory '" + parent.string() +
                          "': " + ec.message();
            return err;
        }
    }

    json arr = json::array();
    try
    {
        for (const auto& p : profiles)
        {
            json node;
            node["name"] = p.name;
            node["host"] = p.config.host;
            node["port"] = p.config.port;
            node["user"] = p.config.user;
            node["worldDb"] = p.config.worldDb;
            node["savePassword"] = p.savePassword;
            // Persist password only when the user opted in.
            if (p.savePassword)
                node["password"] = p.config.password;
            arr.push_back(std::move(node));
        }
    }
    catch (const std::exception& ex)
    {
        DbError err;
        err.ok = false;
        err.message = std::string("connections: serialize error: ") + ex.what();
        return err;
    }

    json root;
    root["profiles"] = std::move(arr);

    std::ofstream out(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
    {
        DbError err;
        err.ok = false;
        err.message = "connections: cannot open '" + filePath + "' for writing";
        return err;
    }

    try
    {
        out << root.dump(2) << '\n';
    }
    catch (const std::exception& ex)
    {
        DbError err;
        err.ok = false;
        err.message = std::string("connections: write error: ") + ex.what();
        return err;
    }

    if (!out)
    {
        DbError err;
        err.ok = false;
        err.message = "connections: write failed for '" + filePath + "'";
        return err;
    }
    return DbError{};
}
} // namespace we
