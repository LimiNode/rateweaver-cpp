#include <curl/curl.h>

#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::size_t discard_body(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}

CURLcode request(const char* url, const char* ca_file, bool wrong_host = false) {
    CURL* easy = curl_easy_init();
    if (!easy) return CURLE_FAILED_INIT;
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_PROXY, "");
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_CAINFO, ca_file);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, discard_body);
    // Resolve a deliberately wrong name to loopback without changing the peer.
    curl_slist* resolve = nullptr;
    if (wrong_host) {
        const std::string address = std::string(url);
        const auto port_start = address.find(':', 8) + 1;
        const auto port_end = address.find('/', port_start);
        const auto entry = "wrong.invalid:" + address.substr(port_start, port_end - port_start) + ":127.0.0.1";
        resolve = curl_slist_append(nullptr, entry.c_str());
        curl_easy_setopt(easy, CURLOPT_RESOLVE, resolve);
    }
    const auto result = curl_easy_perform(easy);
    curl_easy_cleanup(easy);
    curl_slist_free_all(resolve);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    const auto* version = curl_version_info(CURLVERSION_NOW);
    std::cout << "headers=" << LIBCURL_VERSION << " runtime=" << version->version
              << " TLS=" << (version->ssl_version ? version->ssl_version : "none") << '\n';
    bool valid = (version->features & CURL_VERSION_SSL) != 0;
#ifdef RATEWEAVER_TEST_PREBUILT_CURL
    valid = valid && version->version_num == LIBCURL_VERSION_NUM && LIBCURL_VERSION_NUM == 0x081600;
    valid = valid && version->ssl_version && std::string(version->ssl_version) == "LibreSSL/4.3.2";
    wchar_t executable[MAX_PATH]{};
    wchar_t module[MAX_PATH]{};
    const auto curl_module = GetModuleHandleW(L"libcurl-x64.dll");
    valid = valid && curl_module && GetModuleFileNameW(nullptr, executable, MAX_PATH)
        && GetModuleFileNameW(curl_module, module, MAX_PATH);
    if (curl_module) {
        const auto expected = std::filesystem::path(executable).parent_path() / L"libcurl-x64.dll";
        valid = valid && std::filesystem::equivalent(expected, module);
        std::cout << "curl_module=" << std::filesystem::path(module).string() << '\n';
    }
#endif
    const auto trusted = request(argv[1], argv[2]);
    const auto untrusted = request(argv[1], nullptr);
    std::string wrong_url = argv[1];
    wrong_url.replace(wrong_url.find("localhost"), 9, "wrong.invalid");
    const auto rejected = request(wrong_url.c_str(), argv[2], true);
    std::cout << "trusted_https=" << trusted << " untrusted=" << untrusted
              << " wrong_host=" << rejected << '\n';
    valid = valid && trusted == CURLE_OK && untrusted == CURLE_PEER_FAILED_VERIFICATION
        && rejected == CURLE_PEER_FAILED_VERIFICATION;
    curl_global_cleanup();
    return valid ? 0 : 2;
}
