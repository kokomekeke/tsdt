#include "Fetch.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace Sagax::SCG {
    namespace {

        namespace net = boost::asio;
        namespace beast = boost::beast;
        namespace http = boost::beast::http;
        using tcp = boost::asio::ip::tcp;

        // the os text of an error is in the code page of the local language, the codes are named
        // here instead. an unlisted code is reported by its number and category
        std::string Reason(const beast::error_code &ec) {
            if(ec == net::error::connection_refused) { return "the connection was refused"; }
            if(ec == net::error::connection_reset) { return "the connection was reset"; }
            if(ec == net::error::connection_aborted) { return "the connection was dropped"; }
            if(ec == net::error::host_unreachable) { return "the host cannot be reached"; }
            if(ec == net::error::network_unreachable) { return "the network cannot be reached"; }
            if(ec == net::error::access_denied) { return "the connection was not allowed"; }
            if(ec == net::error::eof) { return "the other side closed the connection"; }
            if(ec == http::error::partial_message) { return "the answer stopped in the middle"; }
            if(ec == http::error::bad_status) { return "the answer does not begin with a status line"; }
            return std::format("error {} of {}", ec.value(), ec.category().name());
        }

        std::string ReadLocalFile(const std::string &pathStr, std::string &out) {
            std::filesystem::path path;
            try {
                path = std::filesystem::absolute(std::filesystem::path(pathStr));
            }
            catch(const std::exception &e) {
                return std::format("invalid file path: \"{}\": {}", pathStr, e.what());
            }
            std::ifstream file(path, std::ios::binary);
            if(!file.is_open()) {
                return std::format("failed to open file: {}", path.string());
            }
            std::stringstream buf;
            buf << file.rdbuf();
            out = buf.str();
            return {};
        }

        int HexDigit(char c) {
            if(c >= '0' && c <= '9') { return c - '0'; }
            if(c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            if(c >= 'A' && c <= 'F') { return c - 'A' + 10; }
            return -1;
        }

        std::string PercentDecode(const std::string &s, std::string &out) {
            out.clear();
            for(std::size_t i = 0; i < s.size(); ++i) {
                if(s[i] != '%') {
                    out.push_back(s[i]);
                    continue;
                }
                if(i + 2 >= s.size()) {
                    return std::format("truncated percent escape in \"{}\"", s);
                }
                int hi = HexDigit(s[i + 1]);
                int lo = HexDigit(s[i + 2]);
                if(hi < 0 || lo < 0) {
                    return std::format("bad percent escape in \"{}\"", s);
                }
                out.push_back(char(hi * 16 + lo));
                i += 2;
            }
            return {};
        }

        // what follows the two slashes is an empty host and then the path, so it begins
        // with a slash of its own. on windows the drive letter sits behind that slash and
        // the slash is dropped
        std::string FilePathFromUrl(const std::string &rest, std::string &out) {
            if(rest.empty() || rest[0] != '/') {
                return std::format(
                    "a file url is written \"file:///dir/name.json\" with an empty host "
                    "and an absolute path: \"file://{}\"",
                    rest
                );
            }
            std::string decoded;
            std::string error = PercentDecode(rest, decoded);
            if(!error.empty()) {
                return error;
            }
#ifdef _WIN32
            if(decoded.size() >= 3
                && decoded[2] == ':'
                && std::isalpha(static_cast<unsigned char>(decoded[1])) != 0
            ) {
                decoded.erase(0, 1);
            }
#endif
            out = std::move(decoded);
            return {};
        }

        struct HttpUrl {
            std::string host;
            std::string port;
            std::uint16_t portNumber;
            std::string target;
        };

        // the host runs up to the first slash. an ipv6 address is written in brackets and
        // holds colons of its own, so the port separator is looked for behind the closing
        // bracket and the brackets are dropped from the host
        std::string ParseHttpUrl(const std::string &rest, HttpUrl &out) {
            auto slash = rest.find('/');
            std::string authority = rest.substr(0, slash);
            out.target = slash == std::string::npos ? "/" : rest.substr(slash);
            if(authority.empty()) {
                return "an http url must name a host";
            }

            std::size_t colon = std::string::npos;
            if(authority[0] == '[') {
                auto close = authority.find(']');
                if(close == std::string::npos) {
                    return std::format("unterminated address in an http url: \"{}\"", authority);
                }
                out.host = authority.substr(1, close - 1);
                if(close + 1 < authority.size()) {
                    if(authority[close + 1] != ':') {
                        return std::format("bad address in an http url: \"{}\"", authority);
                    }
                    colon = close + 1;
                }
            }
            else {
                colon = authority.find(':');
                out.host = authority.substr(0, colon);
            }
            if(out.host.empty()) {
                return "an http url must name a host";
            }

            out.port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
            if(out.port.empty() || out.port.find_first_not_of("0123456789") != std::string::npos) {
                return std::format("bad port in an http url: \"{}\"", authority);
            }
            unsigned long portValue = 0;
            for(char c : out.port) {
                portValue = portValue * 10 + static_cast<unsigned long>(c - '0');
                if(portValue > 65535) {
                    return std::format("port out of range in an http url: \"{}\"", authority);
                }
            }
            out.portNumber = static_cast<std::uint16_t>(portValue);
            return {};
        }

        std::string HttpGet(const HttpUrl &url, double timeout_s, std::string &out, std::size_t bodyLimit) {
            beast::error_code ec;
            // only an address is accepted, a name lookup runs on a boost thread that the timeout
            // cannot interrupt
            auto address = net::ip::make_address(url.host, ec);
            if(ec) {
                return std::format("the host of an http url has to be an ip address: \"{}\"", url.host);
            }
            std::string authority = url.port == "80"
                ? url.host
                : std::format("{}:{}", url.host, url.port);

            beast::flat_buffer buffer;
            http::request<http::empty_body> req(http::verb::get, url.target, 11);
            http::response_parser<http::string_body> parser;
            req.set(http::field::host, authority);
            req.set(http::field::user_agent, "scg");
            parser.body_limit(bodyLimit);

            net::io_context ioc;
            beast::tcp_stream stream(ioc);

            // the timer covers the connect, the request and the response together. a step
            // that runs past it closes the socket and ends with error::timeout, and a step
            // started after it has run out ends that way at once
            stream.expires_at(
                std::chrono::steady_clock::now()
                + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeout_s)
                )
            );

            // run returns once the step it was handed is over, and leaves the context
            // stopped, so the next step restarts it first
            auto run = [&]() {
                ioc.restart();
                ioc.run();
            };

            stream.async_connect(tcp::endpoint(address, url.portNumber), [&](beast::error_code e) {
                ec = e;
            });
            run();
            if(ec == beast::error::timeout) {
                return std::format("timed out connecting to \"{}\"", authority);
            }
            if(ec) {
                return std::format("failed to connect to \"{}\": {}", authority, Reason(ec));
            }

            http::async_write(stream, req, [&](beast::error_code e, std::size_t) {
                ec = e;
            });
            run();
            if(ec == beast::error::timeout) {
                return std::format("timed out sending the request to \"{}\"", authority);
            }
            if(ec) {
                return std::format("failed to send the request to \"{}\": {}", authority, Reason(ec));
            }

            http::async_read(stream, buffer, parser, [&](beast::error_code e, std::size_t) {
                ec = e;
            });
            run();
            if(ec == beast::error::timeout) {
                return std::format("timed out reading the response from \"{}\"", authority);
            }
            if(ec == http::error::body_limit) {
                return std::format(
                    "the response from \"{}\" is larger than the limit of {} bytes",
                    authority, bodyLimit
                );
            }
            if(ec) {
                return std::format("failed to read the response from \"{}\": {}", authority, Reason(ec));
            }

            beast::error_code ignored;
            stream.socket().shutdown(tcp::socket::shutdown_both, ignored);

            auto &res = parser.get();
            if(res.result() != http::status::ok) {
                return std::format(
                    "\"{}\" answered the request for \"{}\" with {} {}",
                    authority, url.target, res.result_int(),
                    std::string(res.reason().data(), res.reason().size())
                );
            }
            out = std::move(res.body());
            return {};
        }

    }

    std::string Fetch(const std::string &location, double timeout_s, std::string &out, std::size_t bodyLimit) {
        auto sep = location.find("://");
        if(sep == std::string::npos) {
            return std::format(
                "a location has to begin with \"file://\" or \"http://\": \"{}\"",
                location
            );
        }

        std::string scheme = location.substr(0, sep);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::string rest = location.substr(sep + 3);

        if(scheme == "file") {
            std::string path;
            std::string error = FilePathFromUrl(rest, path);
            if(!error.empty()) {
                return error;
            }
            return ReadLocalFile(path, out);
        }
        if(scheme != "http") {
            return std::format("the scheme of a location has to be http or file: \"{}\"", location);
        }
        if(timeout_s <= 0.0) {
            return std::format("the fetch timeout has to be positive, it is {}", timeout_s);
        }

        HttpUrl url;
        std::string error = ParseHttpUrl(rest, url);
        if(!error.empty()) {
            return error;
        }
        // boost reports through the error codes the steps read, an exception from here is
        // the library running out of a resource it needs to start the exchange at all
        try {
            return HttpGet(url, timeout_s, out, bodyLimit);
        }
        catch(const std::exception &e) {
            return std::format("failed to fetch \"{}\": {}", location, e.what());
        }
    }

}
