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
