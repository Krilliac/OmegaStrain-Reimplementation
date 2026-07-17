#pragma once

#include <string>
#include <vector>

namespace omega::asset
{
// An owned, canonical VFS location in the user-supplied game-data tree. Strings use normalized
// uppercase slash-separated paths. Each HOG entry names the next nested container. No pointer or
// byte span crosses the decode boundary.
struct SourceLocator
{
    std::string game_path;
    std::vector<std::string> hog_entries;
};
} // namespace omega::asset
