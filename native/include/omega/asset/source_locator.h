#pragma once

#include <string>
#include <vector>

namespace omega::asset
{
// An owned, canonical VFS location in the user-supplied game-data tree. Strings use normalized
// uppercase slash-separated paths. Each HOG component selects one member of the current archive;
// the final selected member may be either another container or a leaf. An empty component chain
// selects the VFS file itself. No pointer or byte span crosses the decode boundary.
struct SourceLocator
{
    std::string game_path;
    std::vector<std::string> hog_entries;
};
} // namespace omega::asset
