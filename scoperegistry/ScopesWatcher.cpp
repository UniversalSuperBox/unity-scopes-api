/*
 * Copyright (C) 2014 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Marcus Tomlinson <marcus.tomlinson@canonical.com>
 */

#include "ScopesWatcher.h"

#include "FindFiles.h"

#include <unity/UnityExceptions.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <sys/stat.h>

using namespace unity::scopes::internal;
using namespace boost;

namespace scoperegistry
{

ScopesWatcher::ScopesWatcher(RegistryObject::SPtr registry,
                             std::function<void(std::pair<std::string, std::string> const&)> ini_added_callback,
                             Logger& logger)
    : DirWatcher(logger)
    , registry_(registry)
    , ini_added_callback_(ini_added_callback)
    , logger_(logger)
{
}

ScopesWatcher::~ScopesWatcher()
{
    cleanup();
}

void ScopesWatcher::add_install_dir(std::string const& dir)
{
    try
    {
        try
        {
            add_watch(parent_dir(dir));
        }
        catch (unity::FileException const&) {}  // Ignore does not exist exception
        catch (unity::LogicException const&) {} // Ignore already exists exception
        catch (unity::SyscallException const& e)
        {
            logger_() << "ScopesWatcher::add_install_dir(): parent dir watch: " << e.what();
        }

        // Create a new entry for this install dir into idir_to_sdirs_map_
        if (dir.back() == '/')
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idir_to_sdirs_map_[dir.substr(0, dir.length() - 1)] = std::set<std::string>();
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idir_to_sdirs_map_[dir] = std::set<std::string>();
        }

        // Add watch for root directory
        try
        {
            add_watch(dir);

            // Add watches for each sub directory in root
            auto subdirs = find_entries(dir, EntryType::Directory);
            for (auto const& subdir : subdirs)
            {
                try
                {
                    add_scope_dir(subdir);
                }
                catch (unity::FileException const&)
                {
                    // Ignore does not exist exception
                }
            }
        }
        catch (unity::FileException const&)
        {
            // Ignore does not exist exception
        }
    }
    catch (unity::ResourceException const& e)
    {
        logger_() << "ScopesWatcher::add_install_dir(): install dir watch: " << e.what();
    }
    catch (unity::SyscallException const& e)
    {
        logger_() << "ScopesWatcher::add_install_dir(): install dir watch: " << e.what();
    }
}

std::string ScopesWatcher::parent_dir(std::string const& child_dir)
{
    std::string parent;
    if (child_dir.back() == '/')
    {
        parent = filesystem::path(child_dir.substr(0, child_dir.length() - 1)).parent_path().native();
    }
    else
    {
        parent = filesystem::path(child_dir).parent_path().native();
    }
    return parent;
}

void ScopesWatcher::remove_install_dir(std::string const& dir)
{
    std::set<std::string> scope_dirs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (idir_to_sdirs_map_.find(dir) != idir_to_sdirs_map_.end())
        {
            scope_dirs = idir_to_sdirs_map_.at(dir);
        }
    }

    for (auto const& scope_dir : scope_dirs)
    {
        remove_scope_dir(scope_dir);
    }

    remove_watch(dir);
}

namespace
{

bool file_is_empty(std::string const& path)
{
    struct stat buf;
    if (stat(path.c_str(), &buf) == -1)
    {
        // We ignore errors because, by the time we get to look,
        // the file may no longer be there.
        return true;
    }
    return buf.st_size == 0;
}

}

void ScopesWatcher::add_scope_dir(std::string const& dir)
{
    try
    {
        // Add a watch for this directory (ignore exception if already exists)
        try
        {
            if (filesystem::is_directory(dir))
            {
                add_watch(dir);  // Avoid noise if someone drops a file in here
            }
        }
        catch (unity::LogicException const&) {}

        auto configs = find_scope_dir_configs(dir, ".ini");
        if (!configs.empty())
        {
            auto config = *configs.cbegin();
            if (file_is_empty(config.second))
            {
                return;  // Wait for event indicating non-empty file, so we don't try parsing it too early.
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);

                // Associate this scope with its install directory
                if (idir_to_sdirs_map_.find(parent_dir(dir)) != idir_to_sdirs_map_.end())
                {
                    idir_to_sdirs_map_.at(parent_dir(dir)).insert(dir);
                }

                // Associate this directory with the contained config file
                sdir_to_ini_map_[dir] = config.second;
            }

            // New config found, execute callback
            ini_added_callback_(config);
            logger_(LoggerSeverity::Info) << "ScopesWatcher: scope: \"" << config.first
                                          << "\" installed to: \"" << dir << "\"";
        }
    }
    catch (std::exception const& e)
    {
        logger_() << "scoperegistry: add_scope_dir(): " << e.what();
    }
}

void ScopesWatcher::remove_scope_dir(std::string const& dir)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if this directory is associate with a config file
    if (sdir_to_ini_map_.find(dir) != sdir_to_ini_map_.end())
    {
        // Unassociate this scope with its install directory
        if (idir_to_sdirs_map_.find(parent_dir(dir)) != idir_to_sdirs_map_.end())
        {
            idir_to_sdirs_map_.at(parent_dir(dir)).erase(dir);
        }

        // Unassociate this config file with its scope directory
        std::string ini_path = sdir_to_ini_map_.at(dir);
        sdir_to_ini_map_.erase(dir);

        // Inform the registry that this scope has been removed
        filesystem::path p(ini_path);
        std::string scope_id = p.stem().native();
        registry_->remove_local_scope(scope_id);
        logger_(LoggerSeverity::Info) << "ScopesWatcher: scope: \"" << scope_id
                                      << "\" uninstalled from: \"" << dir << "\"";
    }

    // Remove the watch for this directory
    remove_watch(dir);
}

void ScopesWatcher::watch_event(DirWatcher::EventType event_type,
                                DirWatcher::FileType file_type,
                                std::string const& path)
{
    filesystem::path fs_path(path);

    if (file_type == DirWatcher::File
        && fs_path.extension() == ".ini"
        && !boost::algorithm::ends_with(path, "-settings.ini"))
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string parent_path = fs_path.parent_path().native();
        std::string scope_id = fs_path.stem().native();

        // A .ini has been added / modified
        if (event_type == DirWatcher::Added || event_type == DirWatcher::Modified)
        {
            // We notify only if the file is non-empty.
            // This avoids notifying twice if things are slow,
            // because we may get an event for the file creation,
            // followed by an event for the file modification.
            // This is not completely free of races because,
            // by the time we get the create event, the file may
            // have been *partially* written, in which case
            // we'll still notify a second time when the file is closed.
            // But because .ini files are small, we get away with it. (We
            // rely on the file writer to not write, say, one byte
            // at a time.)
            bool non_empty = true;
            if (event_type == DirWatcher::Added)
            {
                non_empty = !file_is_empty(path);
            }
            if (non_empty)
            {
                sdir_to_ini_map_[parent_path] = path;
                ini_added_callback_(std::make_pair(scope_id, path));
                logger_(LoggerSeverity::Info) << "scopeswatcher: scope: \"" << scope_id
                                              << "\" .ini installed: \"" << path << "\"";
            }
        }
        // a .ini has been removed
        else if (event_type == DirWatcher::Removed)
        {
            sdir_to_ini_map_.erase(parent_path);
            registry_->remove_local_scope(scope_id);
            logger_(LoggerSeverity::Info) << "scopeswatcher: scope: \"" << scope_id
                                          << "\" .ini uninstalled: \"" << path << "\"";
        }
    }
    else
    {
        bool is_install_dir = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (idir_to_sdirs_map_.find(path) != idir_to_sdirs_map_.end())
            {
                is_install_dir = true;
            }
        }

        // If this path is an install dir:
        if (is_install_dir)
        {
            // An install directory has been added
            if (event_type == DirWatcher::Added)
            {
                add_install_dir(path);
            }
            // An install directory has been removed
            else if (event_type == DirWatcher::Removed)
            {
                remove_install_dir(path);
            }
        }
        // Else if this path is within an install dir:
        else
        {
            bool is_inside_install_dir;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                is_inside_install_dir = idir_to_sdirs_map_.find(parent_dir(path)) != idir_to_sdirs_map_.end();
            }

            if (is_inside_install_dir)
            {
                // A new sub-directory (or symlink to sub-directory) has been added
                if (event_type == DirWatcher::Added)
                {
                    // try add this path as a scope folder (ignore failures to add this path as scope dir)
                    // (we need to do this with both files and folders added, as the file added may be a symlink)
                    try
                    {
                        add_scope_dir(path);
                    }
                    catch (unity::FileException const& e) {}
                }
                // A sub directory has been removed
                else if (event_type == DirWatcher::Removed)
                {
                    remove_scope_dir(path);
                }
            }
        }
    }
}

} // namespace scoperegistry
