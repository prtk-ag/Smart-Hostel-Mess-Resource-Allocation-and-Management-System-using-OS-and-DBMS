#pragma once

#include <filesystem>
#include <string>

// Where schema.sql lives. CMake bakes the absolute source directory in as
// SHRM_SOURCE_DIR, so the programs work from ANY folder -- including
// build\Release\ under Visual Studio, where the old relative path
// "server/db/schema.sql" failed. Falls back to the relative path (the old
// behaviour) if the baked-in one no longer exists, e.g. the project folder
// was moved after building.
inline std::string schemaPath() {
#ifdef SHRM_SOURCE_DIR
    const std::filesystem::path baked = std::filesystem::path(SHRM_SOURCE_DIR) / "server" / "db" / "schema.sql";
    if (std::filesystem::exists(baked)) return baked.string();
#endif
    return "server/db/schema.sql";
}
