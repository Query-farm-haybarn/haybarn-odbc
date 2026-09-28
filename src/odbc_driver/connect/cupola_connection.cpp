#include "cupola_connection.hpp"

#include "duckdb/common/string_util.hpp"
#include "yyjson.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <shlobj.h>
#include <wincrypt.h>
#include <windows.h>
#include <winhttp.h>
#endif

namespace duckdb {
#ifdef _WIN32
using namespace duckdb_yyjson;
namespace {
struct Json {
	yyjson_doc *doc;
	explicit Json(const std::string &text)
	    : doc(yyjson_read(text.data() + (text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0),
	                      text.size() - (text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0), 0)) {
		if (!doc)
			throw std::runtime_error("Cupola store contains invalid JSON.");
	}
	~Json() {
		yyjson_doc_free(doc);
	}
	yyjson_val *Root() {
		return yyjson_doc_get_root(doc);
	}
};
std::wstring Wide(const std::string &s) {
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
	if (s.empty())
		return L"";
	if (!n && !s.empty())
		throw std::runtime_error("Invalid Cupola text encoding.");
	std::wstring out(n, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &out[0], n);
	return out;
}
std::string Utf8(const std::wstring &s) {
	if (s.empty())
		return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	std::string out(n, 0);
	WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
	return out;
}
std::wstring Env(const wchar_t *key) {
	DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
	if (!n)
		return L"";
	std::wstring v(n, 0);
	GetEnvironmentVariableW(key, &v[0], n);
	v.resize(n - 1);
	return v;
}
std::wstring Root() {
	auto override_path = Env(L"VGI_EXCEL_CONFIG_HOME");
	if (!override_path.empty())
		return override_path;
	PWSTR path = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
		throw std::runtime_error("Cupola user store is unavailable.");
	std::wstring root(path);
	CoTaskMemFree(path);
	return root + L"\\QueryFarm\\VgiExcel";
}
std::string Read(const std::wstring &path, size_t limit) {
	std::ifstream in(path.c_str(), std::ios::binary);
	if (!in)
		throw std::runtime_error(
		    "Cupola connection or OAuth session was not found. Open Cupola Connections and sign in if required.");
	in.seekg(0, std::ios::end);
	auto size = in.tellg();
	if (size < 0 || (uint64_t)size > limit)
		throw std::runtime_error("Cupola store exceeds its size limit.");
	in.seekg(0);
	std::string data((size_t)size, 0);
	if (!data.empty() && !in.read(&data[0], data.size()))
		throw std::runtime_error("Cupola store could not be read.");
	return data;
}
std::string Field(yyjson_val *o, const char *key) {
	auto v = yyjson_obj_get(o, key);
	if (!yyjson_is_str(v))
		throw std::runtime_error("Cupola connection fields are invalid.");
	return std::string(yyjson_get_str(v), yyjson_get_len(v));
}
std::string Optional(yyjson_val *o, const char *key) {
	auto v = yyjson_obj_get(o, key);
	return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : "";
}
std::string Quote(const std::string &s, char mark = '\'') {
	std::string v(1, mark);
	for (char c : s) {
		if (!c)
			throw std::runtime_error("Cupola fields must not contain NUL.");
		v += c;
		if (c == mark)
			v += c;
	}
	return v + mark;
}
std::string Dump(yyjson_val *v) {
	size_t len;
	char *p = yyjson_val_write(v, 0, &len);
	if (!p)
		throw std::runtime_error("Invalid Cupola option.");
	std::string s(p, len);
	free(p);
	return s;
}
std::string Hash(const std::string &s) {
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	BYTE value[32];
	DWORD n = sizeof(value);
	if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		throw std::runtime_error("Cupola hash provider unavailable.");
	bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
	          CryptHashData(hash, (const BYTE *)s.data(), (DWORD)s.size(), 0) &&
	          CryptGetHashParam(hash, HP_HASHVAL, value, &n, 0);
	if (hash)
		CryptDestroyHash(hash);
	CryptReleaseContext(provider, 0);
	if (!ok)
		throw std::runtime_error("Cupola hash failed.");
	const char *hex = "0123456789ABCDEF";
	std::string out;
	for (auto b : value) {
		out += hex[b >> 4];
		out += hex[b & 15];
	}
	return out;
}
// Match OAuthClient.Key: normalized HTTPS origin/path, excluding query/fragment and trailing slash.
std::string OAuthKey(const std::string &location) {
	auto url = Wide(location);
	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwUrlPathLength =
	    parts.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
	    !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
		throw std::runtime_error("Cupola requires an HTTPS endpoint without embedded credentials.");
	auto host = Utf8(std::wstring(parts.lpszHostName, parts.dwHostNameLength));
	host = StringUtil::Lower(host);
	std::string key = "https://" + host;
	if (parts.nPort != 443)
		key += ":" + std::to_string(parts.nPort);
	if (parts.dwUrlPathLength)
		key += Utf8(std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength));
	while (!key.empty() && key.back() == '/')
		key.pop_back();
	return key;
}
struct Secret {
	std::string value;
	~Secret() {
		if (!value.empty())
			SecureZeroMemory(&value[0], value.size());
	}
};
std::string Credential(const std::wstring &root, const std::string &key) {
	auto encrypted =
	    Read(root + L"\\oauth-sessions\\" + Wide(Hash("QueryFarm/VgiExcel/OAuth/" + Hash(key))) + L".bin", 1024 * 1024);
	std::string entropy = "QueryFarm.CupolaForExcel.OAuth.v1";
	DATA_BLOB input = {(DWORD)encrypted.size(), (BYTE *)&encrypted[0]},
	          extra = {(DWORD)entropy.size(), (BYTE *)&entropy[0]}, output = {};
	if (!CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
		throw std::runtime_error(
		    "Cupola OAuth session cannot be decrypted for this Windows user. Sign in again in Cupola.");
	Secret clear;
	clear.value.assign((char *)output.pbData, output.cbData);
	SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	Json tokens(clear.value);
	Secret token;
	token.value = Optional(tokens.Root(), "refresh_token");
	if (!token.value.empty())
		return "oauth_refresh_token " + Quote(token.value);
	// Persistent access-only sessions are not refreshed by this driver; require a valid expiration.
	auto expiry = Optional(tokens.Root(), "ExpiresAtUtc");
	SYSTEMTIME now;
	GetSystemTime(&now);
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
	         now.wMinute, now.wSecond);
	if (expiry.size() < 19 || expiry.substr(0, 19) <= stamp)
		throw std::runtime_error("Cupola OAuth session has expired. Sign in again in Cupola.");
	bool id = yyjson_is_true(yyjson_obj_get(tokens.Root(), "UseIdToken"));
	token.value = Optional(tokens.Root(), id ? "id_token" : "access_token");
	if (token.value.empty())
		throw std::runtime_error("Cupola OAuth session has no usable credential. Sign in again in Cupola.");
	return "bearer_token " + Quote(token.value);
}
void Run(Connection &c, const std::string &sql, const char *error) {
	try {
		auto result = c.Query(sql);
		if (result && !result->HasError())
			return;
	} catch (...) {
	}
	throw std::runtime_error(error);
}
} // namespace
#endif
void AttachCupolaConnection(Connection &c, const std::string &requested) {
#ifndef _WIN32
	throw std::runtime_error("Cupola connections require the Windows per-user connection store.");
#else
	auto root = Root();
	Json registry(Read(root + L"\\desktop-connections.json", 8 * 1024 * 1024));
	if (!yyjson_is_arr(registry.Root()) || requested.empty())
		throw std::runtime_error("A valid Cupola connection name is required.");
	yyjson_val *selected = nullptr, *row;
	size_t i, max;
	auto wanted = Wide(requested);
	yyjson_arr_foreach(registry.Root(), i, max, row) {
		auto candidate = Wide(Field(row, "Name"));
		if (CompareStringOrdinal(candidate.c_str(), (int)candidate.size(), wanted.c_str(), (int)wanted.size(), TRUE) ==
		    CSTR_EQUAL) {
			if (selected)
				throw std::runtime_error("Cupola connection name is ambiguous.");
			selected = row;
		}
	}
	if (!selected)
		throw std::runtime_error(
		    "The named Cupola connection does not exist. Select an existing connection in Cupola.");
	auto name = Field(selected, "Name"), catalog = Field(selected, "Catalog"), location = Field(selected, "Location"),
	     auth = Field(selected, "Authentication");
	if (catalog.empty())
		throw std::runtime_error("Cupola catalog alias is missing.");
	auto oauth_key = OAuthKey(location);
	if (auth != "anonymous" && auth != "oauth")
		throw std::runtime_error("Cupola authentication mode is invalid.");
	auto options = yyjson_obj_get(selected, "AttachOptions");
	if (options && !yyjson_is_null(options) && !yyjson_is_obj(options))
		throw std::runtime_error("Cupola ATTACH options must be an object.");
	Secret attach;
	attach.value =
	    "ATTACH " + Quote(catalog) + " AS " + Quote(catalog, '"') + " (TYPE vgi, LOCATION " + Quote(location);
	if (auth == "oauth") {
		Secret credential;
		credential.value = Credential(root, oauth_key);
		attach.value += ", " + credential.value;
	}
	static const std::set<std::string> forbidden = {
	    "type",          "location", "access_token",        "api_key",  "authorization", "bearer_token",
	    "client_secret", "id_token", "oauth_refresh_token", "password", "refresh_token", "secret"};
	if (yyjson_is_obj(options)) {
		yyjson_val *k, *v;
		yyjson_obj_foreach(options, i, max, k, v) {
			std::string key = yyjson_get_str(k);
			if (!std::regex_match(key, std::regex("[A-Za-z_][A-Za-z0-9_]*")) || forbidden.count(StringUtil::Lower(key)))
				throw std::runtime_error("Cupola ATTACH options contain an invalid or reserved name.");
			if (!yyjson_is_str(v) && !yyjson_is_num(v) && !yyjson_is_bool(v) && !yyjson_is_null(v))
				throw std::runtime_error("Cupola ATTACH options must be scalar values.");
			attach.value += ", " + key + " " +
			                (yyjson_is_str(v) ? Quote(std::string(yyjson_get_str(v), yyjson_get_len(v))) : Dump(v));
		}
	}
	attach.value += ")";
	// Load the signed extension shipped alongside this driver, with the normal Haybarn repository as fallback.
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCWSTR)&AttachCupolaConnection, &module);
	wchar_t path[32768];
	DWORD n = GetModuleFileNameW(module, path, 32768);
	if (!n || n >= 32768)
		throw std::runtime_error("Cupola driver directory is unavailable.");
	std::wstring extension(path, n);
	extension = extension.substr(0, extension.find_last_of(L"\\/")) + L"\\vgi.duckdb_extension";
	if (GetFileAttributesW(extension.c_str()) != INVALID_FILE_ATTRIBUTES)
		Run(c, "LOAD " + Quote(Utf8(extension)), "Cupola VGI extension could not be loaded.");
	else {
		Run(c, "INSTALL vgi FROM community", "Cupola VGI extension could not be installed.");
		Run(c, "LOAD vgi", "Cupola VGI extension could not be loaded.");
	}
	Run(c, attach.value, "Cupola could not attach the HTTPS catalog. Check the connection and sign-in in Cupola.");
	Run(c, "USE " + Quote(catalog, '"'), "Cupola could not select its attached catalog.");
	// Connection-scoped identity contract; never includes OAuth material. Read only by local preflight.
	auto option_json = yyjson_is_obj(options) ? Dump(options) : "{}";
	Run(c,
	    "CREATE TEMP MACRO cupola_connection_info() AS TABLE SELECT 1 AS contract_version, " + Quote(name) +
	        " AS connection_name, " + Quote(catalog) + " AS catalog_alias, " + Quote(location) + " AS location, " +
	        Quote(auth) + " AS authentication, " + Quote(option_json) + " AS attach_options",
	    "Cupola connection identity could not be initialized.");
#endif
}
} // namespace duckdb
