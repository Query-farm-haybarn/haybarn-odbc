#pragma once
#include "duckdb.hpp"
namespace duckdb {
// Resolve the current user's Cupola registry and attach through the VGI extension.
// Throws fixed, credential-free errors; never returns ATTACH SQL to ODBC diagnostics.
void AttachCupolaConnection(Connection &connection, const std::string &name);
} // namespace duckdb
