#include "claudebox/vault.hpp"
#include "claudebox/companion.hpp"
#include "claudebox/gateway_ip.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <cstdio>
#include <unistd.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace claudebox::vault {
namespace {

namespace fs = std::filesystem;

using claudebox::config::BoxConfig;
using claudebox::gateway::BoxProcess;
using claudebox::gateway::is_box_claude_process;

constexpr char kArchiveMagic[8] = {'C', 'B', 'O', 'X', 'V', 'L', 'T', '1'};
constexpr char kDirMagic[8] = {'C', 'B', 'O', 'X', 'D', 'I', 'R', '1'};
constexpr int kSaltLen = 16;
constexpr int kNonceLen = 12;
constexpr int kTagLen = 16;
constexpr int kKeyLen = 32;

std::string trim(std::string_view in) {
    std::size_t b = 0;
    while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b]))) {
        ++b;
    }
    std::size_t e = in.size();
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) {
        --e;
    }
    return std::string(in.substr(b, e - b));
}

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string slashify(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return s;
}

std::string getenv_str(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string{};
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, std::string_view body) {
    fs::create_directories(path.parent_path());
    const fs::path tmp = path.parent_path() / (path.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to write " + tmp.string());
        }
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
        if (!out) {
            throw std::runtime_error("failed to flush " + tmp.string());
        }
    }
    fs::rename(tmp, path);
}

bool path_is_under(const fs::path& path, const fs::path& root) {
    if (root.empty() || path.empty()) return false;
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(path, ec);
    const fs::path canon_path = ec ? path.lexically_normal() : a;
    ec.clear();
    const fs::path b = fs::weakly_canonical(root, ec);
    const fs::path canon_root = ec ? root.lexically_normal() : b;
    std::string ps = slashify(to_lower(canon_path.string()));
    std::string rs = slashify(to_lower(canon_root.string()));
    if (ps == rs) return true;
    if (!rs.empty() && rs.back() != '/') rs.push_back('/');
    return ps.size() > rs.size() && ps.compare(0, rs.size(), rs) == 0;
}

void append_u16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>((v >> 8) & 0xff));
    out.push_back(static_cast<char>(v & 0xff));
}

void append_u32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xff));
    out.push_back(static_cast<char>((v >> 16) & 0xff));
    out.push_back(static_cast<char>((v >> 8) & 0xff));
    out.push_back(static_cast<char>(v & 0xff));
}

void append_u64(std::string& out, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
    }
}

bool read_exact(std::string_view in, std::size_t& off, void* dest, std::size_t n) {
    if (off + n > in.size()) return false;
    std::memcpy(dest, in.data() + off, n);
    off += n;
    return true;
}

std::uint16_t take_u16(std::string_view in, std::size_t& off, bool* ok) {
    unsigned char b[2];
    if (!read_exact(in, off, b, 2)) {
        *ok = false;
        return 0;
    }
    return static_cast<std::uint16_t>((b[0] << 8) | b[1]);
}

std::uint32_t take_u32(std::string_view in, std::size_t& off, bool* ok) {
    unsigned char b[4];
    if (!read_exact(in, off, b, 4)) {
        *ok = false;
        return 0;
    }
    return (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
           (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
}

std::uint64_t take_u64(std::string_view in, std::size_t& off, bool* ok) {
    unsigned char b[8];
    if (!read_exact(in, off, b, 8)) {
        *ok = false;
        return 0;
    }
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | b[i];
    }
    return v;
}

std::string now_iso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool relative_ok(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos) {
        return false;
    }
    if (rel.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

std::string pack_directory(const fs::path& root) {
    std::string out;
    out.append(kDirMagic, 8);
    std::uint32_t count = 0;
    const auto count_pos = out.size();
    append_u32(out, 0);
    if (fs::exists(root)) {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const fs::path rel = fs::relative(entry.path(), root);
            const std::string rels = slashify(rel.generic_string());
            if (!relative_ok(rels)) {
                throw std::runtime_error("refusing to pack unsafe path: " + rels);
            }
            const auto data = read_file(entry.path());
            if (rels.size() > 0xffff) {
                throw std::runtime_error("path too long: " + rels);
            }
            append_u16(out, static_cast<std::uint16_t>(rels.size()));
            out.append(rels);
            const auto mode = static_cast<std::uint32_t>(
                static_cast<unsigned>(entry.status().permissions()) & 0x1ff);
            append_u32(out, mode);
            append_u64(out, data.size());
            out.append(data);
            ++count;
        }
    }
    // patch count
    out[count_pos + 0] = static_cast<char>((count >> 24) & 0xff);
    out[count_pos + 1] = static_cast<char>((count >> 16) & 0xff);
    out[count_pos + 2] = static_cast<char>((count >> 8) & 0xff);
    out[count_pos + 3] = static_cast<char>(count & 0xff);
    return out;
}

void unpack_directory(std::string_view blob, const fs::path& dest) {
    if (blob.size() < 12 || std::memcmp(blob.data(), kDirMagic, 8) != 0) {
        throw std::runtime_error("identity archive plaintext is not a claude-box directory pack");
    }
    std::size_t off = 8;
    bool ok = true;
    const auto count = take_u32(blob, off, &ok);
    if (!ok) {
        throw std::runtime_error("truncated directory pack");
    }
    fs::create_directories(dest);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto path_len = take_u16(blob, off, &ok);
        if (!ok || off + path_len > blob.size()) {
            throw std::runtime_error("truncated path in directory pack");
        }
        const std::string rel(blob.data() + off, path_len);
        off += path_len;
        if (!relative_ok(rel)) {
            throw std::runtime_error("refusing to unpack unsafe path: " + rel);
        }
        (void)take_u32(blob, off, &ok);
        const auto size = take_u64(blob, off, &ok);
        if (!ok || off + size > blob.size()) {
            throw std::runtime_error("truncated file in directory pack: " + rel);
        }
        const fs::path out = dest / rel;
        if (!path_is_under(out, dest) && fs::weakly_canonical(out.parent_path()) != dest) {
            // path_is_under handles dest/rel; still require dest as root
            if (!path_is_under(out, dest)) {
                throw std::runtime_error("unpack path escaped destination: " + rel);
            }
        }
        write_file(out, std::string_view(blob.data() + off, static_cast<std::size_t>(size)));
        off += static_cast<std::size_t>(size);
    }
}

struct Key {
    unsigned char bytes[kKeyLen]{};
    ~Key() { OPENSSL_cleanse(bytes, sizeof(bytes)); }
};

void derive_key(std::string_view pass, const unsigned char* salt, Key* key) {
    if (PKCS5_PBKDF2_HMAC(pass.data(), static_cast<int>(pass.size()), salt, kSaltLen,
                          kKdfIterations, EVP_sha256(), kKeyLen, key->bytes) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
}

std::string encrypt_blob(std::string_view plaintext, std::string_view passphrase) {
    if (passphrase.empty()) {
        throw std::runtime_error("passphrase is required");
    }
    unsigned char salt[kSaltLen];
    unsigned char nonce[kNonceLen];
    if (RAND_bytes(salt, kSaltLen) != 1 || RAND_bytes(nonce, kNonceLen) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    Key key;
    derive_key(passphrase, salt, &key);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    std::string ciphertext(plaintext.size(), '\0');
    int outlen = 0;
    int tot = 0;
    unsigned char tag[kTagLen];
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
              EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.bytes, nonce) == 1 &&
              EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &outlen,
                                reinterpret_cast<const unsigned char*>(plaintext.data()),
                                static_cast<int>(plaintext.size())) == 1;
    tot = outlen;
    int fin = 0;
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + tot,
                                   &fin) == 1;
    tot += fin;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key.bytes, sizeof(key.bytes));
    if (!ok) {
        throw std::runtime_error("AES-256-GCM encrypt failed");
    }
    ciphertext.resize(static_cast<std::size_t>(tot));
    std::string out;
    out.append(kArchiveMagic, 8);
    append_u32(out, static_cast<std::uint32_t>(kKdfIterations));
    out.append(reinterpret_cast<const char*>(salt), kSaltLen);
    out.append(reinterpret_cast<const char*>(nonce), kNonceLen);
    out.append(ciphertext);
    out.append(reinterpret_cast<const char*>(tag), kTagLen);
    return out;
}

std::string decrypt_blob(std::string_view blob, std::string_view passphrase) {
    if (blob.size() < 8 + 4 + kSaltLen + kNonceLen + kTagLen ||
        std::memcmp(blob.data(), kArchiveMagic, 8) != 0) {
        throw std::runtime_error("not a claude-box identity vault archive");
    }
    std::size_t off = 8;
    bool ok = true;
    const auto iter = take_u32(blob, off, &ok);
    if (!ok || iter != static_cast<std::uint32_t>(kKdfIterations)) {
        throw std::runtime_error("unsupported vault KDF iteration count");
    }
    unsigned char salt[kSaltLen];
    unsigned char nonce[kNonceLen];
    if (!read_exact(blob, off, salt, kSaltLen) || !read_exact(blob, off, nonce, kNonceLen)) {
        throw std::runtime_error("truncated vault archive header");
    }
    if (blob.size() - off < static_cast<std::size_t>(kTagLen)) {
        throw std::runtime_error("truncated vault archive");
    }
    const std::size_t ct_len = blob.size() - off - kTagLen;
    const auto* ct = reinterpret_cast<const unsigned char*>(blob.data() + off);
    unsigned char tag[kTagLen];
    std::memcpy(tag, blob.data() + off + ct_len, kTagLen);

    Key key;
    derive_key(passphrase, salt, &key);
    std::string plain(ct_len, '\0');
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    int outlen = 0;
    int tot = 0;
    int fin = 0;
    bool dec = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
               EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
               EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.bytes, nonce) == 1 &&
               EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &outlen, ct,
                                 static_cast<int>(ct_len)) == 1;
    tot = outlen;
    dec = dec && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, tag) == 1;
    dec = dec && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + tot,
                                     &fin) == 1;
    tot += fin;
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key.bytes, sizeof(key.bytes));
    if (!dec) {
        throw std::runtime_error("vault archive authentication failed");
    }
    plain.resize(static_cast<std::size_t>(tot));
    return plain;
}

std::string read_pointer(const fs::path& path) {
    if (!fs::exists(path)) return {};
    return trim(read_file(path));
}

void write_pointer(const fs::path& path, const std::string& id) {
    write_file(path, id + "\n");
}

std::string yaml_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string manifest_yaml(const Snapshot& snap) {
    std::ostringstream out;
    out << "kind: ClaudeBoxIdentitySnapshot\n";
    out << "id: " << yaml_quote(snap.id) << "\n";
    out << "label: " << yaml_quote(snap.label) << "\n";
    out << "created_at: " << yaml_quote(snap.created_at) << "\n";
    out << "cipher: " << kCipherName << "\n";
    out << "kdf: " << kKdfName << "\n";
    out << "kdf_iterations: " << kKdfIterations << "\n";
    out << "companion_profile: " << yaml_quote(snap.companion_profile.string()) << "\n";
    return out.str();
}

std::string next_generation_id(const VaultPaths& paths) {
    int max_n = 0;
    if (fs::exists(paths.generations)) {
        for (const auto& entry : fs::directory_iterator(paths.generations)) {
            const auto name = entry.path().filename().string();
            if (name.rfind("gen-", 0) != 0) continue;
            try {
                max_n = std::max(max_n, std::stoi(name.substr(4)));
            } catch (...) {
            }
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "gen-%04d", max_n + 1);
    return buf;
}

void copy_dir(const fs::path& from, const fs::path& to) {
    if (fs::exists(to)) {
        fs::remove_all(to);
    }
    if (!fs::exists(from)) {
        fs::create_directories(to);
        return;
    }
    fs::create_directories(to);
    for (const auto& entry : fs::recursive_directory_iterator(from)) {
        const auto rel = fs::relative(entry.path(), from);
        const auto dest = to / rel;
        if (entry.is_directory()) {
            fs::create_directories(dest);
        } else if (entry.is_regular_file()) {
            fs::create_directories(dest.parent_path());
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
        }
    }
}

bool dir_has_files(const fs::path& dir) {
    if (!fs::exists(dir)) return false;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) return true;
    }
    return false;
}

std::string yaml_field(const std::string& text, const std::string& key) {
    const std::string prefix = key + ":";
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto t = trim(line);
        if (t.rfind(prefix, 0) == 0) {
            auto v = trim(t.substr(prefix.size()));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            return v;
        }
    }
    return {};
}

fs::path identity_companion_profile(const VaultPaths& paths, const std::string& id) {
    return paths.companion_root / id / "profile";
}

void record_written(ActionResult& result, const fs::path& path) {
    result.written.push_back(path);
}

bool all_writes_inside_prefix(const ActionResult& result, const fs::path& prefix,
                              const VaultPaths& paths) {
    (void)paths;
    for (const auto& p : result.written) {
        if (is_forbidden_host_path(p)) {
            return false;
        }
        if (!path_is_under(p, prefix) && p != prefix) {
            return false;
        }
    }
    return true;
}

void ensure_layout(const VaultPaths& paths) {
    fs::create_directories(paths.vault_dir);
    fs::create_directories(paths.workspace);
    fs::create_directories(paths.generations);
    fs::create_directories(paths.tmp);
    fs::create_directories(paths.companion_root);
}

std::string pack_workspace(const VaultPaths& paths, const fs::path& companion_src) {
    const auto staging = paths.tmp / ("pack-" + std::to_string(::getpid()));
    fs::remove_all(staging);
    fs::create_directories(staging / "workspace");
    if (fs::exists(paths.workspace)) {
        copy_dir(paths.workspace, staging / "workspace");
    }
    if (!companion_src.empty() && fs::exists(companion_src) &&
        path_is_under(companion_src, paths.prefix) &&
        !claudebox::companion::is_forbidden_profile_path(companion_src, paths.prefix)) {
        copy_dir(companion_src, staging / "companion-profile");
    }
    auto blob = pack_directory(staging);
    fs::remove_all(staging);
    return blob;
}

void unpack_workspace(const std::string& plain, const VaultPaths& paths,
                      const fs::path& companion_dest) {
    const auto staging = paths.tmp / ("unpack-" + std::to_string(::getpid()));
    fs::remove_all(staging);
    unpack_directory(plain, staging);
    fs::create_directories(paths.workspace.parent_path());
    copy_dir(staging / "workspace", paths.workspace);
    if (fs::exists(staging / "companion-profile")) {
        if (companion_dest.empty() ||
            claudebox::companion::is_forbidden_profile_path(companion_dest, paths.prefix) ||
            !path_is_under(companion_dest, paths.prefix)) {
            fs::remove_all(staging);
            throw std::runtime_error(
                "refusing to restore companion profile outside the box prefix");
        }
        copy_dir(staging / "companion-profile", companion_dest);
    }
    fs::remove_all(staging);
}

void write_empty_workspace(const VaultPaths& paths, const std::string& id) {
    fs::remove_all(paths.workspace);
    fs::create_directories(paths.workspace);
    const auto profile = identity_companion_profile(paths, id);
    fs::create_directories(profile);
    write_file(paths.workspace / "identity.json",
               std::string("{\n  \"kind\": \"claude-box-identity\",\n  \"name\": \"empty\",\n  "
                           "\"bound_companion_profile\": \"") +
                   profile.string() + "\"\n}\n");
    write_file(paths.workspace / "companion-bind.txt", profile.string() + "\n");
}

void bind_companion_env(const BoxConfig& config, const VaultPaths& paths,
                        const fs::path& profile) {
    if (profile.empty()) return;
    // Rewrite companion.env profile pointer if intercept is already installed.
    const auto env = claudebox::companion::companion_env_path(paths.prefix);
    if (!fs::exists(env)) {
        // Installing intercept also creates companion.env; engine may be missing.
        claudebox::companion::install_intercept(config, paths.prefix);
    }
    if (!fs::exists(env)) return;
    auto text = read_file(env);
    const std::string key = "export CLAUDE_BOX_COMPANION_PROFILE=";
    const auto pos = text.find(key);
    const std::string line = key + "'" + profile.string() + "'\n";
    if (pos == std::string::npos) {
        text += line;
    } else {
        auto end = text.find('\n', pos);
        if (end == std::string::npos) {
            text.replace(pos, text.size() - pos, line);
        } else {
            text.replace(pos, end + 1 - pos, line);
        }
    }
    write_file(env, text);
}

ActionResult fail_result(VaultPlan plan, std::string error) {
    ActionResult r;
    r.plan = std::move(plan);
    r.error = std::move(error);
    r.ok = false;
    return r;
}

}  // namespace

std::filesystem::path default_vault_dir(const std::filesystem::path& prefix) {
    return prefix / "var" / "identity-vault";
}

std::filesystem::path default_workspace(const std::filesystem::path& vault_dir) {
    return vault_dir / "workspace";
}

std::filesystem::path default_companion_root(const std::filesystem::path& prefix) {
    return prefix / "var" / "companion-browser" / "identities";
}

bool is_forbidden_host_path(const std::filesystem::path& path) {
    if (path.empty()) return true;
    const std::string s = slashify(to_lower(path.string()));
    const char* needles[] = {
        "/appdata/roaming/claude",
        "/appdata/local/claude",
        "/.config/claude",
        "/.config/claude-desktop",
        "/google/chrome",
        "/microsoft/edge",
        "/.config/google-chrome",
        "/.config/chromium",
        "/appdata/local/google/chrome",
        "/appdata/local/microsoft/edge",
        "/etc/localtime",
        "/etc/timezone",
    };
    for (const char* n : needles) {
        if (s.find(n) != std::string::npos) return true;
    }
    return false;
}

bool is_allowed_vault_dir(const std::filesystem::path& path, const std::filesystem::path& prefix,
                          std::string* error) {
    if (path.empty()) {
        if (error) *error = "vault dir is empty";
        return false;
    }
    if (is_forbidden_host_path(path)) {
        if (error) {
            *error =
                "vault dir must not be a host Claude or browser profile path: " + path.string();
        }
        return false;
    }
    if (!prefix.empty() && path_is_under(path, prefix)) {
        return true;
    }
    const std::string s = slashify(to_lower(path.string()));
    if (s == "/var/lib/claude-box/identity-vault" ||
        s.rfind("/var/lib/claude-box/identity-vault/", 0) == 0) {
        return true;
    }
    if (s.find("/localappdata/claudebox/identity-vault") != std::string::npos ||
        s.find("/claudebox/identity-vault") != std::string::npos) {
        // Allow the documented %LOCALAPPDATA%\ClaudeBox\identity-vault convention
        // only when it is clearly under ClaudeBox, not under %APPDATA%\Claude.
        if (s.find("/appdata/roaming/claude") == std::string::npos) {
            return true;
        }
    }
    if (error) {
        *error = "vault dir is not a box path or documented ClaudeBox convention: " + path.string();
    }
    return false;
}

VaultPaths resolve_paths(const claudebox::config::BoxConfig& config,
                         const std::filesystem::path& prefix) {
    VaultPaths p;
    p.prefix = prefix;
    auto configured = trim(config.features.vault_dir);
    const auto from_env = getenv_str("CLAUDE_BOX_IDENTITY_VAULT_DIR");
    if (!from_env.empty()) {
        configured = from_env;
    }
    if (configured.empty()) {
        p.vault_dir = default_vault_dir(prefix);
    } else {
        fs::path cand(configured);
        // Expand a bare relative path against the prefix.
        if (cand.is_relative() && !prefix.empty()) {
            cand = prefix / cand;
        }
        p.vault_dir = cand;
    }
    p.workspace = default_workspace(p.vault_dir);
    p.generations = p.vault_dir / "generations";
    p.tmp = p.vault_dir / "tmp";
    p.current_pointer = p.vault_dir / "current";
    p.previous_pointer = p.vault_dir / "previous";
    p.companion_root = default_companion_root(prefix);
    return p;
}

bool load_passphrase(std::string* out, std::string* error) {
    if (!out) return false;
    const auto file = getenv_str("CLAUDE_BOX_VAULT_PASSPHRASE_FILE");
    if (!file.empty()) {
        if (!fs::exists(file)) {
            if (error) *error = "passphrase file does not exist";
            return false;
        }
        *out = trim(read_file(file));
        if (out->empty()) {
            if (error) *error = "passphrase file is empty";
            return false;
        }
        return true;
    }
    const auto env = getenv_str("CLAUDE_BOX_VAULT_PASSPHRASE");
    if (!env.empty()) {
        *out = env;
        return true;
    }
    if (error) {
        *error =
            "passphrase is required (set CLAUDE_BOX_VAULT_PASSPHRASE_FILE or "
            "CLAUDE_BOX_VAULT_PASSPHRASE)";
    }
    return false;
}

bool is_vault_close_target(std::string_view name) {
    if (is_box_claude_process(name)) return true;
    const auto n = to_lower(std::string(name));
    if (n == "companion-browser" || n == "fingerprint-chromium" ||
        n == "claude-box-companion" || n == "fingerprintchromium") {
        return true;
    }
    return false;
}

ClosePlan plan_close(const std::vector<BoxProcess>& processes, int grace_seconds) {
    ClosePlan p;
    p.grace_seconds = grace_seconds < 0 ? 0 : grace_seconds;
    p.dry_run = true;
    for (const auto& proc : processes) {
        if (is_vault_close_target(proc.name) && proc.alive) {
            p.targets.push_back(proc);
        } else {
            p.skipped.push_back(proc);
        }
    }
    p.starts_polite_close = !p.targets.empty();
    p.force_after_grace = !p.targets.empty();
    p.ok = true;
    return p;
}

std::vector<Snapshot> list_snapshots(const VaultPaths& paths) {
    std::vector<Snapshot> out;
    const auto current = read_pointer(paths.current_pointer);
    const auto previous = read_pointer(paths.previous_pointer);
    if (!fs::exists(paths.generations)) return out;
    for (const auto& entry : fs::directory_iterator(paths.generations)) {
        if (!entry.is_directory()) continue;
        const auto name = entry.path().filename().string();
        if (name.find(".partial") != std::string::npos) continue;
        if (name.rfind("gen-", 0) != 0) continue;
        Snapshot s;
        s.id = name;
        s.directory = entry.path();
        s.archive = entry.path() / "archive.enc";
        const auto man = entry.path() / "manifest.yaml";
        if (fs::exists(man)) {
            const auto text = read_file(man);
            s.label = yaml_field(text, "label");
            s.created_at = yaml_field(text, "created_at");
            const auto comp = yaml_field(text, "companion_profile");
            if (!comp.empty()) s.companion_profile = comp;
        }
        if (s.label.empty()) s.label = s.id;
        s.current = (s.id == current);
        s.previous = (s.id == previous);
        if (s.companion_profile.empty()) {
            s.companion_profile = identity_companion_profile(paths, s.id);
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(),
              [](const Snapshot& a, const Snapshot& b) { return a.id < b.id; });
    return out;
}

VaultPlan plan(const claudebox::config::BoxConfig& config, const std::filesystem::path& prefix,
               const ActionOptions& options) {
    VaultPlan p;
    p.feature_enabled = config.features.identity_vault;
    p.paths = resolve_paths(config, prefix);
    std::string path_err;
    if (prefix.empty()) {
        p.errors.emplace_back("prefix is required");
    }
    if (!is_allowed_vault_dir(p.paths.vault_dir, prefix, &path_err)) {
        p.errors.push_back(path_err);
    }
    if (!p.feature_enabled) {
        p.errors.emplace_back(
            "features.identity_vault is disabled; refusing to archive or switch "
            "(will not write host Claude or browser directories)");
    }
    p.close = plan_close(options.processes, config.features.kill_grace_seconds);
    if (fs::exists(p.paths.vault_dir)) {
        p.snapshots = list_snapshots(p.paths);
        p.current_id = read_pointer(p.paths.current_pointer);
        p.previous_id = read_pointer(p.paths.previous_pointer);
    }
    p.ok = p.errors.empty();
    return p;
}

static ActionResult commit_generation(const BoxConfig& config, const fs::path& prefix,
                                      std::string_view passphrase, const ActionOptions& options,
                                      const std::string& packed, const std::string& label,
                                      const fs::path& companion_profile, bool replace_workspace,
                                      const std::string& workspace_mode) {
    ActionResult result;
    result.plan = plan(config, prefix, options);
    result.close = result.plan.close;
    if (!result.plan.ok) {
        result.error = result.plan.errors.empty() ? "vault plan rejected" : result.plan.errors.front();
        return result;
    }
    if (passphrase.empty()) {
        result.error = "passphrase is required";
        return result;
    }
    try {
        auto paths = result.plan.paths;
        ensure_layout(paths);
        const auto previous_id = read_pointer(paths.current_pointer);
        const auto id = next_generation_id(paths);
        const auto partial = paths.generations / (id + ".partial");
        const auto final_dir = paths.generations / id;
        fs::remove_all(partial);
        fs::create_directories(partial);
        record_written(result, partial);

        Snapshot snap;
        snap.id = id;
        snap.label = label.empty() ? id : label;
        snap.created_at = now_iso();
        snap.directory = final_dir;
        snap.archive = final_dir / "archive.enc";
        snap.companion_profile =
            companion_profile.empty() ? identity_companion_profile(paths, id) : companion_profile;

        const auto cipher = encrypt_blob(packed, passphrase);
        write_file(partial / "archive.enc", cipher);
        write_file(partial / "manifest.yaml", manifest_yaml(snap));
        record_written(result, partial / "archive.enc");
        record_written(result, partial / "manifest.yaml");

        if (options.force_fail_after == "after_write") {
            throw std::runtime_error("forced failure after_write");
        }

        const auto check = decrypt_blob(read_file(partial / "archive.enc"), passphrase);
        if (check != packed) {
            throw std::runtime_error("vault archive failed verification");
        }
        if (options.force_fail_after == "after_verify") {
            throw std::runtime_error("forced failure after_verify");
        }

        if (fs::exists(final_dir)) {
            throw std::runtime_error("generation already exists: " + id);
        }
        fs::rename(partial, final_dir);
        record_written(result, final_dir);

        if (replace_workspace) {
            if (workspace_mode == "empty") {
                write_empty_workspace(paths, id);
            } else if (workspace_mode == "unpack") {
                unpack_workspace(check, paths, snap.companion_profile);
            }
            record_written(result, paths.workspace);
            if (!snap.companion_profile.empty()) {
                fs::create_directories(snap.companion_profile);
                record_written(result, snap.companion_profile);
                bind_companion_env(config, paths, snap.companion_profile);
                record_written(result, claudebox::companion::companion_env_path(paths.prefix));
            }
        } else if (!snap.companion_profile.empty()) {
            // Archiving the current identity: keep workspace, bind recorded profile.
            fs::create_directories(snap.companion_profile);
            record_written(result, snap.companion_profile);
        }

        if (!previous_id.empty()) {
            write_pointer(paths.previous_pointer, previous_id);
            record_written(result, paths.previous_pointer);
        }
        write_pointer(paths.current_pointer, id);
        record_written(result, paths.current_pointer);

        if (!all_writes_inside_prefix(result, prefix, paths)) {
            result.wrote_outside_box = true;
            throw std::runtime_error("refusing writes outside the box vault or companion dirs");
        }
        result.wrote_outside_box = false;
        result.current_id = id;
        result.previous_id = previous_id;
        result.plan = plan(config, prefix, options);
        result.plan.close = result.close;
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.ok = false;
        result.error = ex.what();
        // Pointer is only flipped after verify; a mid-way failure leaves
        // current unchanged. Incomplete .partial dirs are recoverable (ignored
        // by list_snapshots). Workspace is not replaced until after verify
        // except when the exception occurs during replace — recover from previous.
        try {
            const auto paths = resolve_paths(config, prefix);
            const auto current = read_pointer(paths.current_pointer);
            if (current.empty() || current == result.current_id) {
                // nothing flipped, or we somehow wrote the id; if workspace
                // was mid-replaced, restore from previous archive.
            }
            if (!read_pointer(paths.current_pointer).empty() &&
                !result.current_id.empty() &&
                read_pointer(paths.current_pointer) == result.current_id && !result.ok) {
                // should not happen because we set current_id after pointer write
            }
        } catch (...) {
        }
        result.current_id = read_pointer(resolve_paths(config, prefix).current_pointer);
        result.previous_id = read_pointer(resolve_paths(config, prefix).previous_pointer);
        result.plan = plan(config, prefix, options);
        return result;
    }
}

ActionResult archive(const claudebox::config::BoxConfig& config, const std::filesystem::path& prefix,
                     std::string_view passphrase, const ActionOptions& options) {
    auto planned = plan(config, prefix, options);
    if (!planned.ok) {
        return fail_result(std::move(planned),
                           planned.errors.empty() ? "vault plan rejected" : planned.errors.front());
    }
    try {
        ensure_layout(planned.paths);
        fs::path companion;
        const auto bind = planned.paths.workspace / "companion-bind.txt";
        if (fs::exists(bind)) {
            companion = trim(read_file(bind));
        }
        if (companion.empty()) {
            companion = identity_companion_profile(planned.paths, "live");
            fs::create_directories(companion);
            write_file(bind, companion.string() + "\n");
        }
        const auto packed = pack_workspace(planned.paths, companion);
        auto label = options.label;
        if (label.empty()) label = "archive";
        return commit_generation(config, prefix, passphrase, options, packed, label, companion,
                                 /*replace_workspace=*/false, "");
    } catch (const std::exception& ex) {
        return fail_result(std::move(planned), ex.what());
    }
}

ActionResult switch_to(const claudebox::config::BoxConfig& config,
                       const std::filesystem::path& prefix, std::string_view target_id,
                       std::string_view passphrase, const ActionOptions& options) {
    auto planned = plan(config, prefix, options);
    if (!planned.ok) {
        return fail_result(std::move(planned),
                           planned.errors.empty() ? "vault plan rejected" : planned.errors.front());
    }
    const std::string target = trim(target_id);
    if (target.empty()) {
        return fail_result(std::move(planned), "switch target is required");
    }
    try {
        ensure_layout(planned.paths);
        // Safety archive of the current workspace when it has files and is not
        // already the latest current snapshot's exact image — always snapshot
        // first so failure can roll back to a known generation.
        ActionOptions archive_opts = options;
        archive_opts.label = "pre-switch";
        archive_opts.force_fail_after.clear();  // safety archive must complete
        if (dir_has_files(planned.paths.workspace)) {
            const auto saved = archive(config, prefix, passphrase, archive_opts);
            if (!saved.ok) {
                return saved;
            }
            planned = plan(config, prefix, options);
        }

        if (target == "empty") {
            ActionOptions empty_opts = options;
            if (empty_opts.label.empty()) empty_opts.label = "empty";
            // Pack the empty workspace we are about to install, commit as a
            // new generation, then replace the live workspace.
            const auto staging_paths = planned.paths;
            const auto backup_ws = staging_paths.tmp / "ws-backup";
            copy_dir(staging_paths.workspace, backup_ws);
            write_empty_workspace(staging_paths, "empty-staging");
            const auto packed =
                pack_workspace(staging_paths, identity_companion_profile(staging_paths, "empty-staging"));
            copy_dir(backup_ws, staging_paths.workspace);
            fs::remove_all(backup_ws);
            auto result =
                commit_generation(config, prefix, passphrase, empty_opts, packed, empty_opts.label,
                                  fs::path{}, true, "empty");
            if (!result.ok && fs::exists(backup_ws)) {
                copy_dir(backup_ws, staging_paths.workspace);
            }
            return result;
        }

        // Restore an existing generation.
        const auto dest = planned.paths.generations / target;
        const auto archive_path = dest / "archive.enc";
        if (!fs::exists(archive_path)) {
            return fail_result(std::move(planned), "snapshot not found: " + target);
        }
        const auto cipher = read_file(archive_path);
        const auto plain = decrypt_blob(cipher, passphrase);
        if (options.force_fail_after == "after_verify" ||
            options.force_fail_after == "after_write") {
            ActionResult failed;
            failed.plan = planned;
            failed.error = "forced failure " + options.force_fail_after;
            failed.current_id = planned.current_id;
            failed.previous_id = planned.previous_id;
            failed.close = planned.close;
            failed.rolled_back = false;
            return failed;
        }
        fs::path companion = identity_companion_profile(planned.paths, target);
        const auto man = dest / "manifest.yaml";
        if (fs::exists(man)) {
            const auto listed = yaml_field(read_file(man), "companion_profile");
            if (!listed.empty()) companion = listed;
        }
        unpack_workspace(plain, planned.paths, companion);
        bind_companion_env(config, planned.paths, companion);
        const auto previous_id = read_pointer(planned.paths.current_pointer);
        if (!previous_id.empty()) {
            write_pointer(planned.paths.previous_pointer, previous_id);
        }
        write_pointer(planned.paths.current_pointer, target);

        ActionResult result;
        result.ok = true;
        result.current_id = target;
        result.previous_id = previous_id;
        result.plan = plan(config, prefix, options);
        result.close = result.plan.close;
        result.written = {planned.paths.workspace, companion, planned.paths.current_pointer};
        result.wrote_outside_box = !all_writes_inside_prefix(result, prefix, planned.paths);
        if (result.wrote_outside_box) {
            result.ok = false;
            result.error = "refusing writes outside the box vault or companion dirs";
        }
        return result;
    } catch (const std::exception& ex) {
        ActionResult failed = fail_result(std::move(planned), ex.what());
        return failed;
    }
}

ActionResult rollback(const claudebox::config::BoxConfig& config,
                      const std::filesystem::path& prefix, std::string_view passphrase,
                      const ActionOptions& options) {
    auto planned = plan(config, prefix, options);
    if (!planned.ok) {
        return fail_result(std::move(planned),
                           planned.errors.empty() ? "vault plan rejected" : planned.errors.front());
    }
    const auto previous = read_pointer(planned.paths.previous_pointer);
    if (previous.empty()) {
        return fail_result(std::move(planned), "no previous snapshot to roll back to");
    }
    auto result = switch_to(config, prefix, previous, passphrase, options);
    if (result.ok) {
        result.rolled_back = true;
    }
    return result;
}

std::string format_status(const VaultPlan& plan) {
    std::ostringstream out;
    out << "身份保险库状态（盒子内，dry-run 进程结束）\n";
    out << "只写盒子前缀 / 文档约定的 ClaudeBox 保险库目录，不会改宿主时区、语言、"
        << "默认浏览器或系统代理，也不会把宿主默认 Claude 目录当作工作副本。\n";
    out << "功能开关 features.identity_vault：" << (plan.feature_enabled ? "开启（默认）" : "关闭")
        << "\n";
    out << "保险库目录：\n  " << plan.paths.vault_dir.string() << "\n";
    out << "工作副本：\n  " << plan.paths.workspace.string() << "\n";
    out << "加密：AES-256-GCM + PBKDF2-HMAC-SHA256（" << kKdfIterations
        << " 次），与 CCBaga .baga 不必二进制兼容。口令不会出现在本页或日志中。\n";
    if (plan.current_id.empty()) {
        out << "当前身份：无\n";
    } else {
        out << "当前身份：" << plan.current_id << "\n";
    }
    if (!plan.previous_id.empty()) {
        out << "上一份身份（可回滚）：" << plan.previous_id << "\n";
    }
    out << "快照数：" << plan.snapshots.size() << "\n";
    for (const auto& s : plan.snapshots) {
        out << "  - " << s.id;
        if (!s.label.empty() && s.label != s.id) out << "（" << s.label << "）";
        if (s.current) out << " [当前]";
        if (s.previous) out << " [上一份]";
        out << "\n";
    }
    out << "切换 / 归档前会按现有节奏规划礼貌关闭盒子内 Claude 和已绑定的伴生浏览器"
        << "（grace " << plan.close.grace_seconds << " 秒后再强杀）。这是 dry-run，不会结束真实进程。\n";
    if (plan.close.starts_polite_close) {
        out << "将结束的进程：";
        for (const auto& proc : plan.close.targets) {
            out << " " << proc.name << "#" << proc.pid;
        }
        out << "\n";
    }
    if (!plan.errors.empty()) {
        out << "问题：\n";
        for (const auto& e : plan.errors) {
            out << "  - " << e << "\n";
        }
    }
    return out.str();
}

}  // namespace claudebox::vault
