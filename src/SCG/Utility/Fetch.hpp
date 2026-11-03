#pragma once
#include <cstddef>
#include <string>

namespace Sagax::SCG {

    // reads the bytes at a location into out. returns the reason of a failure, empty on success
    // out is only written on success
    // "file:///C:/dir/name.json" on windows and "file:///dir/name.json" elsewhere, the percent
    // escapes are decoded
    // "http://address[:port]/target", the target includes the query. the host has to be an ip
    // address
    // any other location is an error
    // the timeout covers the connect, the request and the response together. a file is not timed
    // a status other than 200 is an error
    // a body over bodyLimit is an error and nothing is read past it. a file has no limit
    // it can be called from several threads at once and does not throw
    std::string Fetch(
        const std::string &location,
        double timeout_s,
        std::string &out,
        std::size_t bodyLimit = 64 * 1024 * 1024
    );

}
