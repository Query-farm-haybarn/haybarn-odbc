# haybarn-odbc

The ODBC driver for **Haybarn** — an independent derived distribution of
DuckDB ("Haybarn, powered by DuckDB"), published by Query Farm LLC. It is a
rebrand of the upstream [`duckdb-odbc`](https://github.com/duckdb/duckdb-odbc)
driver and is intentionally **ABI-compatible** with DuckDB: the C API, the
`duckdb::` C++ namespace, headers, and the `DUCKDB_VERSION` macro are all
preserved. The differences are the driver identity (registered driver name,
default DSN, Windows file metadata) and the embedded extension trust root.

Haybarn is independent of and not endorsed by the DuckDB Foundation. DuckDB is
a trademark of the DuckDB Foundation
(https://duckdb.org/trademark_guidelines).

## Driver identity

| | Upstream | Haybarn |
|---|---|---|
| Registered driver name | `DuckDB Driver` | `Haybarn Driver` |
| Default DSN | `DuckDB` | `Haybarn` |
| Shared library | `libduckdb_odbc` | `libhaybarn_odbc` |
| `SQL_DBMS_NAME` / `SQL_DRIVER_NAME` | `DuckDB` | `Haybarn` |

#### Build the ODBC client

###### Debug (for development)

```bash
make debug
```
###### Release (for usage)

```bash
make
```

For a build whose `SELECT version()` reports the release version (not a `git
describe` dev string), regenerate the vendored source with
`OVERRIDE_GIT_DESCRIBE` — see [CLAUDE.md](CLAUDE.md).

#### Configure a DSN

Linux/macOS via unixODBC:

```bash
./linux_setup/unixodbc_setup.sh -u -D $(pwd)/build/release/libhaybarn_odbc.so
```

This registers the `Haybarn Driver` and a default `[Haybarn]` DSN. Connect
with `DSN=Haybarn` (isql, pyodbc, etc.) or `Driver={Haybarn Driver}`.

#### Run the ODBC Unit Tests

The ODBC tests are written with the catch framework. To run the tests, run
the following command from the repository root:

```bash
build/debug/test/test_odbc
```

You can also individually run the tests by specifying the test name as an
argument to the test executable:

```bash
build/debug/test/test_odbc 'Test ALTER TABLE statement'
```

## Source

Source for the underlying Haybarn engine (DuckDB as modified by Haybarn) is at
https://github.com/Query-farm-haybarn/haybarn. The original `duckdb-odbc`
project lives at https://github.com/duckdb/duckdb-odbc.

### Cupola for Excel connections (Windows)

`Driver={Cupola for Excel};CupolaConnection={friendly name};` is a DSN-less
mode for the Excel integration. Register the built Haybarn DLL under that
name using Cupola's updater or MSI. Existing Haybarn driver registrations and
DSNs are not changed. The driver reads the current user's Cupola registry,
validates the exact identity and HTTPS settings, decrypts the shared DPAPI
OAuth session in memory, and attaches through the signed VGI extension.
Ship `vgi.duckdb_extension` alongside the DLL. No credentials are accepted in
the connection string for this mode. Normal non-Cupola ODBC behavior remains
available through the existing Haybarn registration.

The connection-scoped `cupola_connection_info()` table macro reports protocol
version 1 and the resolved name, catalog, endpoint, authentication mode, and
ATTACH options, without OAuth material. The Excel client compares this with its
selected connection and confirms a VGI catalog is attached before creating M.
Errors during ATTACH are fixed messages rather than engine errors that might
include credentials. `VGI_EXCEL_CONFIG_HOME` selects an isolated store for tests.

Tests: build `test_odbc`, run its `[cupola]` and `Test SQLConnect and
SQLDriverConnect` filters, then run Cupola's
`tests/odbc/cupola-connection.ps1` on Windows for registry, DPAPI failure cases,
and a live HTTPS catalog query. Excel first-use permissions require
**Default or Custom** authentication with no extra credentials; the driver
uses Cupola's encrypted session instead of Power Query credentials.
