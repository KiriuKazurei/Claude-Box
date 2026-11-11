#pragma once

#include "claudebox/identity.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace claudebox::identity {

struct CollectResult {
    bool ok = false;
    std::string error;
    Inventory inventory;
    std::filesystem::path output;
};

// Collect a complete raw inventory from a source tree that mirrors host
// paths (sys/class/dmi, sys/class/net, sys/block, proc, and optional
// claudebox/ overlays for Windows-only fields). Never writes the
// presentation layer.
CollectResult collect_from_tree(const std::filesystem::path& source_root);

// Normalize a pre-made inventory file (test fixture or host export) into
// canonical field ids. Does not drop identifiers — this is the raw layer.
CollectResult collect_from_inventory_file(const std::filesystem::path& path);

void write_raw_inventory(const std::filesystem::path& path, const Inventory& inventory);

// Default raw-inventory path under a box prefix.
std::filesystem::path raw_inventory_path(const std::filesystem::path& prefix);

}  // namespace claudebox::identity
