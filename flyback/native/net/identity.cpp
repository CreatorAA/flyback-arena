#include "identity.hpp"
#include <openssl/asn1.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace flyback::net {
template <class T, auto D>
using Ptr = std::unique_ptr<T, decltype(D)>; // OpenSSL objects stay in the library that allocated them
static void need(bool x, const char *s) {
    if (!x)
        throw std::runtime_error(s);
}
static std::string spki(X509 *c) {
    unsigned char *der = nullptr;
    auto n = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(c), &der);
    need(n > 0, "public key encode failed");
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned len = 0;
    int ok = EVP_Digest(der, n, hash, &len, EVP_sha256(), nullptr);
    OPENSSL_free(der);
    need(ok == 1 && len == 32, "public key fingerprint failed");
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string s;
    for (unsigned i = 0; i < len; i++) {
        s += hex[hash[i] >> 4];
        s += hex[hash[i] & 15];
    }
    return s;
}
Cert inspect_cert(const std::uint8_t *p, size_t n, const std::string &host) {
    need(p && n && n < 65536, "missing/oversized TLS leaf certificate");
    const unsigned char *it = p;
    Ptr<X509, X509_free> c(d2i_X509(nullptr, &it, long(n)), X509_free);
    need(c && it == p + n, "invalid TLS leaf certificate");
    auto x = c.get();
    bool now = X509_cmp_current_time(X509_get0_notBefore(x)) < 0 &&
               X509_cmp_current_time(X509_get0_notAfter(x)) > 0;
    bool self = X509_NAME_cmp(X509_get_issuer_name(x), X509_get_subject_name(x)) == 0 &&
                X509_verify(x, X509_get0_pubkey(x)) == 1;
    // An iPAddress SAN is required for literals (a numeric DNS SAN is insufficient).
    auto *ip = a2i_IPADDRESS(host.c_str());
    bool name = ip ? X509_check_ip_asc(x, host.c_str(), 0) == 1
                   : X509_check_host(x, host.c_str(), host.size(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS,
                                     nullptr) == 1;
    ASN1_OCTET_STRING_free(ip);
    return {spki(x), now, self, name};
}
static std::string pem(BIO *b) {
    char *s = nullptr;
    auto n = BIO_get_mem_data(b, &s);
    need(n > 0, "PEM encoding failed");
    return {s, size_t(n)};
}
static std::string read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    auto n = f.tellg();
    need(n > 0 && n <= 1024 * 1024, "identity unreadable or oversized");
    std::string s(size_t(n), '\0');
    f.seekg(0);
    f.read(s.data(), n);
    need(f.good(), "identity read failed");
    return s;
}
static void put_new(const fs::path &file, const std::string &bytes, bool secret) {
#ifdef _WIN32
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
    if (secret) {
        need(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                 L"D:P(A;;FA;;;OW)(A;;FA;;;SY)", SDDL_REVISION_1, &sd, nullptr) == TRUE,
             "identity key ACL creation failed");
        sa.lpSecurityDescriptor = sd;
    }
    auto h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, secret ? &sa : nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sd)
        LocalFree(sd);
    need(h != INVALID_HANDLE_VALUE, "identity file already exists or is not writable");
    DWORD wrote = 0;
    bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) &&
              wrote == bytes.size() && FlushFileBuffers(h);
    CloseHandle(h);
#else
    auto fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, secret ? 0600 : 0644);
    need(fd >= 0, "identity file already exists or is not writable");
    size_t at = 0;
    while (at < bytes.size()) {
        auto n = ::write(fd, bytes.data() + at, bytes.size() - at);
        if (n <= 0)
            break;
        at += n;
    }
    bool ok = at == bytes.size() && fsync(fd) == 0;
    close(fd);
#endif
    need(ok, "identity save failed");
}
static void generate(const fs::path &dir) {
    Ptr<EVP_PKEY, EVP_PKEY_free> k(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"),
                                   EVP_PKEY_free);
    need(bool(k), "EC identity generation failed");
    Ptr<X509, X509_free> c(X509_new(), X509_free);
    need(bool(c), "X509 allocation failed");
    unsigned char serial[16];
    need(RAND_bytes(serial, sizeof(serial)) == 1, "certificate serial generation failed");
    serial[0] &= 0x7f;
    serial[0] |= 1;
    Ptr<BIGNUM, BN_free> bn(BN_bin2bn(serial, sizeof(serial), nullptr), BN_free);
    need(bn && BN_to_ASN1_INTEGER(bn.get(), X509_get_serialNumber(c.get())), "invalid serial");
    need(X509_set_version(c.get(), 2L) == 1 && X509_set_pubkey(c.get(), k.get()) == 1,
         "certificate init failed");
    auto *n = X509_get_subject_name(c.get());
    need(X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (const unsigned char *)"Flyback Arena", -1,
                                    -1, 0) == 1 &&
             X509_set_issuer_name(c.get(), n) == 1,
         "certificate subject failed");
    need(X509_gmtime_adj(X509_getm_notBefore(c.get()), -300) &&
             X509_gmtime_adj(X509_getm_notAfter(c.get()), 86400L * 365 * 5),
         "certificate validity failed");
    for (auto e : {std::pair<int, const char *>{NID_basic_constraints, "critical,CA:FALSE"},
                   {NID_key_usage, "critical,digitalSignature"},
                   {NID_ext_key_usage, "serverAuth"}}) {
        Ptr<X509_EXTENSION, X509_EXTENSION_free> ext(
            X509V3_EXT_conf_nid(nullptr, nullptr, e.first, e.second), X509_EXTENSION_free);
        need(ext && X509_add_ext(c.get(), ext.get(), -1) == 1, "certificate extension failed");
    }
    need(X509_sign(c.get(), k.get(), EVP_sha256()) > 0, "certificate signing failed");
    Ptr<BIO, BIO_free> kb(BIO_new(BIO_s_mem()), BIO_free), cb(BIO_new(BIO_s_mem()), BIO_free);
    need(kb && cb &&
             PEM_write_bio_PrivateKey(kb.get(), k.get(), nullptr, nullptr, 0, nullptr, nullptr) &&
             PEM_write_bio_X509(cb.get(), c.get()),
         "PEM encoding failed");
    put_new(dir / "server.key", pem(kb.get()), true);
    put_new(dir / "server.crt", pem(cb.get()), false);
}
Identity load_identity(const fs::path &dir) {
    fs::create_directories(dir);
    auto cp = dir / "server.crt", kp = dir / "server.key";
    if (!fs::exists(cp) && !fs::exists(kp))
        generate(dir);
    need(fs::exists(cp) && fs::exists(kp),
         "incomplete identity; restore server.crt and server.key together");
    auto ctext = read(cp), ktext = read(kp);
    Ptr<BIO, BIO_free> cb(BIO_new_mem_buf(ctext.data(), int(ctext.size())), BIO_free),
        kb(BIO_new_mem_buf(ktext.data(), int(ktext.size())), BIO_free);
    Ptr<X509, X509_free> c(PEM_read_bio_X509(cb.get(), nullptr, nullptr, nullptr), X509_free);
    Ptr<EVP_PKEY, EVP_PKEY_free> k(
        PEM_read_bio_PrivateKey(
            kb.get(), nullptr, [](char *, int, int, void *) { return 0; }, nullptr),
        EVP_PKEY_free);
    OPENSSL_cleanse(ktext.data(), ktext.size());
    need(c && k && X509_check_private_key(c.get(), k.get()) == 1, "certificate/private key mismatch");
    STACK_OF(X509) *chain = sk_X509_new_null();
    need(chain != nullptr, "certificate chain allocation failed");
    while (auto extra = PEM_read_bio_X509(cb.get(), nullptr, nullptr, nullptr)) {
        if (!sk_X509_push(chain, extra)) {
            X509_free(extra);
            sk_X509_pop_free(chain, X509_free);
            throw std::runtime_error("certificate chain allocation failed");
        }
    }
    Ptr<PKCS12, PKCS12_free> bundle(
        PKCS12_create("", "Flyback", k.get(), c.get(), chain, 0, 0, 0, 0, 0), PKCS12_free);
    sk_X509_pop_free(chain, X509_free);
    need(bool(bundle), "PKCS12 encoding failed");
    auto n = i2d_PKCS12(bundle.get(), nullptr);
    need(n > 0, "PKCS12 export failed");
    Identity result{spki(c.get()), std::vector<std::uint8_t>(n)};
    auto *into = result.p12.data();
    need(i2d_PKCS12(bundle.get(), &into) == n, "PKCS12 export failed");
    return result;
}
} // namespace flyback::net
