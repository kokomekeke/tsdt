#pragma once
#include <cstddef>
#include <string>

namespace Sagax::SCG {

    // reads the bytes at a location into out. returns an empty string when that worked
    // and the reason otherwise, out is only written on success.
    //
    // "file://" reads a file, in the standard spelling with an empty host and an
    // absolute path, "file:///C:/dir/name.json" on windows and "file:///dir/name.json"
    // elsewhere, with the percent escapes decoded. "http://" goes over the network, the
    // spelling is "http://address[:port]/target" and the target carries the query with
    // it. a location written any other way is an error.
    //
    // the host of an http url has to be an ip address, a name is not looked up.
    //
    // the timeout covers the connect, the request and the response together, not each of
    // them on its own. a file is not timed.
    //
    // a status other than 200 is an error and the body is dropped. a response body over
    // bodyLimit is an error and nothing is read past it. the limit is not applied to a
    // file.
    //
    // every object it uses is a local, so it may be called from any thread and by several
    // at once. a failure comes back as the returned string and not as an exception.
    std::string Fetch(
        const std::string &location,
        double timeout_s,
        std::string &out,
        std::size_t bodyLimit = 64 * 1024 * 1024
    );

}
