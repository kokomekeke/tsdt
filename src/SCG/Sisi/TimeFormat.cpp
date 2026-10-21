#include <cstdint>
#include <format>
#include "TimeFormat.hpp"

namespace Sagax::SCG {

    namespace {

        // reads n digits starting at i into out and steps i past them
        bool ReadDigits(const std::string &s, std::size_t &i, std::size_t n, int &out) {
            if(i + n > s.size()) {
                return false;
            }
            int value = 0;
            for(std::size_t k = 0; k < n; ++k) {
                char c = s[i + k];
                if(c < '0' || c > '9') {
                    return false;
                }
                value = value * 10 + (c - '0');
            }
            i += n;
            out = value;
            return true;
        }

        bool ReadLiteral(const std::string &s, std::size_t &i, char c) {
            if(i >= s.size() || s[i] != c) {
                return false;
            }
            ++i;
            return true;
        }

    }

    CommandError ParseStartTime(const std::string &s, std::chrono::system_clock::time_point &out) {
        std::size_t i = 0;
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;

        bool fields =
            ReadDigits(s, i, 4, year) && ReadLiteral(s, i, '-') &&
            ReadDigits(s, i, 2, month) && ReadLiteral(s, i, '-') &&
            ReadDigits(s, i, 2, day) && ReadLiteral(s, i, 'T') &&
            ReadDigits(s, i, 2, hour) && ReadLiteral(s, i, ':') &&
            ReadDigits(s, i, 2, minute) && ReadLiteral(s, i, ':') &&
            ReadDigits(s, i, 2, second);
        if(!fields) {
            return std::format("could not parse starting time: \"{}\"", s);
        }

        // digits past nanoseconds are read and dropped
        std::chrono::nanoseconds fraction{0};
        if(ReadLiteral(s, i, '.')) {
            std::size_t iFraction = i;
            std::int64_t scale = 100000000;
            while(i < s.size() && s[i] >= '0' && s[i] <= '9') {
                fraction += std::chrono::nanoseconds((s[i] - '0') * scale);
                scale /= 10;
                ++i;
            }
            if(i == iFraction) {
                return std::format("could not parse starting time: \"{}\"", s);
            }
        }

        std::chrono::minutes offset{0};
        if(i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
            ++i;
        }
        else if(i < s.size() && (s[i] == '+' || s[i] == '-')) {
            int sign = s[i] == '-' ? -1 : 1;
            ++i;
            int offsetHour = 0;
            int offsetMinute = 0;
            if(!ReadDigits(s, i, 2, offsetHour)) {
                return std::format("could not parse starting time: \"{}\"", s);
            }
            ReadLiteral(s, i, ':');
            if(!ReadDigits(s, i, 2, offsetMinute)) {
                return std::format("could not parse starting time: \"{}\"", s);
            }
            offset = sign * (std::chrono::hours(offsetHour) + std::chrono::minutes(offsetMinute));
        }
        else {
            return std::format("starting time has no utc offset: \"{}\"", s);
        }
        if(i != s.size()) {
            return std::format("could not parse starting time: \"{}\"", s);
        }

        std::chrono::year_month_day date{
            std::chrono::year(year),
            std::chrono::month(unsigned(month)),
            std::chrono::day(unsigned(day))
        };
        if(!date.ok()) {
            return std::format("starting time is not a valid date: \"{}\"", s);
        }
        if(hour > 23 || minute > 59 || second > 59) {
            return std::format("starting time is not a valid time of day: \"{}\"", s);
        }

        out = std::chrono::sys_days(date)
            + std::chrono::hours(hour)
            + std::chrono::minutes(minute)
            + std::chrono::seconds(second)
            + std::chrono::duration_cast<std::chrono::system_clock::duration>(fraction)
            - offset;
        return {};
    }

    std::string FormatIsoTime(std::chrono::system_clock::time_point t) {
        auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(t);
        auto days = std::chrono::floor<std::chrono::days>(milliseconds);
        std::chrono::year_month_day date{days};
        std::chrono::hh_mm_ss<std::chrono::milliseconds> timeOfDay{milliseconds - days};

        return std::format(
            "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
            int(date.year()),
            unsigned(date.month()),
            unsigned(date.day()),
            timeOfDay.hours().count(),
            timeOfDay.minutes().count(),
            timeOfDay.seconds().count(),
            timeOfDay.subseconds().count()
        );
    }

}
