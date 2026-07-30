#include "net/SoapClient.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace qe {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// XML-escape the five predefined entities so a command can be embedded safely.
std::string XmlEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// Reverse of XmlEscape for parsing server-provided text.
std::string XmlUnescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '&') {
            if (in.compare(i, 5, "&amp;") == 0)       { out += '&';  i += 5; continue; }
            if (in.compare(i, 4, "&lt;") == 0)        { out += '<';  i += 4; continue; }
            if (in.compare(i, 4, "&gt;") == 0)        { out += '>';  i += 4; continue; }
            if (in.compare(i, 6, "&quot;") == 0)      { out += '"';  i += 6; continue; }
            if (in.compare(i, 6, "&apos;") == 0)      { out += '\''; i += 6; continue; }
            // Numeric character references &#NN; / &#xNN;
            if (in.compare(i, 3, "&#x") == 0 || in.compare(i, 3, "&#X") == 0) {
                size_t semi = in.find(';', i + 3);
                if (semi != std::string::npos) {
                    unsigned long code = std::strtoul(in.substr(i + 3, semi - (i + 3)).c_str(), nullptr, 16);
                    if (code < 0x80) { out += static_cast<char>(code); i = semi + 1; continue; }
                }
            } else if (in.compare(i, 2, "&#") == 0) {
                size_t semi = in.find(';', i + 2);
                if (semi != std::string::npos) {
                    unsigned long code = std::strtoul(in.substr(i + 2, semi - (i + 2)).c_str(), nullptr, 10);
                    if (code < 0x80) { out += static_cast<char>(code); i = semi + 1; continue; }
                }
            }
        }
        out += in[i];
        ++i;
    }
    return out;
}

// Standard base64 encoding (used for the Authorization header).
std::string Base64Encode(const std::string& in) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    const size_t n = in.size();
    while (i + 3 <= n) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8) |
                     (static_cast<unsigned char>(in[i + 2]));
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += tbl[v & 0x3F];
        i += 3;
    }
    const size_t rem = n - i;
    if (rem == 1) {
        unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += '=';
        out += '=';
    } else if (rem == 2) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8);
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

// Convert a UTF-8 std::string to a wide string for WinHTTP APIs.
std::wstring Utf8ToWide(const std::string& in) {
    if (in.empty())
        return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, in.c_str(),
                                  static_cast<int>(in.size()), nullptr, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()),
                        &out[0], len);
    return out;
}

// Extract the text between the first <tag> and </tag>, or empty if absent.
// `found` is set to whether the tag pair was present at all.
std::string ExtractTag(const std::string& body, const std::string& tag, bool& found) {
    found = false;
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    size_t start = body.find(open);
    if (start == std::string::npos)
        return std::string();
    start += open.size();
    size_t end = body.find(close, start);
    if (end == std::string::npos)
        return std::string();
    found = true;
    return body.substr(start, end - start);
}

// Map a small set of common WinHTTP error codes to readable strings.
std::string DescribeWinHttpError(DWORD err) {
    switch (err) {
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return "Cannot connect to worldserver (is it running and is the SOAP port open?)";
        case ERROR_WINHTTP_TIMEOUT:
            return "Connection timed out";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "Host name could not be resolved";
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return "Connection error (the connection was reset or aborted)";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "Secure channel failure";
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
            return "Invalid server response";
        default: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "WinHTTP error %lu", static_cast<unsigned long>(err));
            return std::string(buf);
        }
    }
}

// RAII wrapper for WinHTTP HINTERNET handles.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
// SoapClient::ExecuteCommand
// ---------------------------------------------------------------------------

SoapResult SoapClient::ExecuteCommand(const SoapConfig& cfg, const std::string& command) const {
    SoapResult result;

    // Build the SOAP 1.1 envelope.
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "xmlns:ns1=\"urn:TC\">"
        "<SOAP-ENV:Body>"
        "<ns1:executeCommand>"
        "<command>" + XmlEscape(command) + "</command>"
        "</ns1:executeCommand>"
        "</SOAP-ENV:Body>"
        "</SOAP-ENV:Envelope>";

    // Open a WinHTTP session.
    WinHttpHandle session(WinHttpOpen(L"TrinityCoreStudio-SoapClient/1.0",
                                      WINHTTP_ACCESS_TYPE_NO_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0));
    if (!session) {
        result.error = "WinHttpOpen failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    const std::wstring whost = Utf8ToWide(cfg.host);
    WinHttpHandle connect(WinHttpConnect(session.h, whost.c_str(),
                                         static_cast<INTERNET_PORT>(cfg.port), 0));
    if (!connect) {
        result.error = "WinHttpConnect failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    WinHttpHandle request(WinHttpOpenRequest(connect.h, L"POST", L"/",
                                             nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             0));  // plain HTTP (no WINHTTP_FLAG_SECURE)
    if (!request) {
        result.error = "WinHttpOpenRequest failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    // Basic auth via an explicit Authorization header (portable and predictable).
    const std::string credentials = cfg.user + ":" + cfg.password;
    const std::wstring authHeader = L"Authorization: Basic " + Utf8ToWide(Base64Encode(credentials));
    if (!WinHttpAddRequestHeaders(request.h, authHeader.c_str(),
                                  static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        result.error = "WinHttpAddRequestHeaders (auth) failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    // Also register credentials with WinHTTP so it can answer a 401 challenge.
    const std::wstring wuser = Utf8ToWide(cfg.user);
    const std::wstring wpass = Utf8ToWide(cfg.password);
    WinHttpSetCredentials(request.h, WINHTTP_AUTH_TARGET_SERVER,
                          WINHTTP_AUTH_SCHEME_BASIC,
                          wuser.c_str(), wpass.c_str(), nullptr);

    // Content-Type and SOAPAction headers.
    static const wchar_t kOtherHeaders[] =
        L"Content-Type: text/xml; charset=utf-8\r\n"
        L"SOAPAction: urn:TC#executeCommand\r\n";
    if (!WinHttpAddRequestHeaders(request.h, kOtherHeaders,
                                  static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        result.error = "WinHttpAddRequestHeaders failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    // Send the request with the body bytes.
    if (!WinHttpSendRequest(request.h,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            body.empty() ? nullptr : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()),
                            0)) {
        result.error = "WinHttpSendRequest failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        result.error = "WinHttpReceiveResponse failed: " + DescribeWinHttpError(GetLastError());
        return result;
    }

    // Query the HTTP status code.
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.h,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        // Not fatal: continue and rely on body parsing, but note the failure.
        statusCode = 0;
    }

    // Read the full response body.
    std::string responseBody;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            result.error = "WinHttpQueryDataAvailable failed: " + DescribeWinHttpError(GetLastError());
            return result;
        }
        if (available == 0)
            break;

        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buffer.data(), available, &read)) {
            result.error = "WinHttpReadData failed: " + DescribeWinHttpError(GetLastError());
            return result;
        }
        if (read == 0)
            break;
        responseBody.append(buffer.data(), read);
    }

    // HTTP 401 -> authentication error.
    if (statusCode == 401) {
        result.ok = false;
        result.error = "Authentication failed (HTTP 401): check the GM account name and password.";
        return result;
    }

    // A SOAP fault takes precedence.
    bool haveFault = false;
    std::string fault = ExtractTag(responseBody, "faultstring", haveFault);
    if (haveFault) {
        result.ok = false;
        result.error = XmlUnescape(fault);
        return result;
    }

    // Extract the <result> element on success.
    bool haveResult = false;
    std::string res = ExtractTag(responseBody, "result", haveResult);
    if (haveResult) {
        result.ok = (statusCode == 0 || statusCode == 200);
        result.output = XmlUnescape(res);
        if (!result.ok && result.error.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "HTTP status %lu", static_cast<unsigned long>(statusCode));
            result.error = buf;
        }
        return result;
    }

    // Neither <result> nor <faultstring>: fall back to the raw body.
    result.output = responseBody;
    result.ok = (statusCode == 200);
    if (!result.ok) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "HTTP status %lu", static_cast<unsigned long>(statusCode));
        result.error = buf;
    }
    return result;
}

} // namespace qe
