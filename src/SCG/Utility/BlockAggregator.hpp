#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <optional>
#include <algorithm>

namespace Sagax::SCG {

    // there is an almost identical class in SSG/Resampler, which is kept because it is taken from spire and no one wants to touch that code
    template<class Item>
    struct BlockAggregator {
    private:
        std::vector<Item> buffer;
        std::int32_t i = 0;
        std::int32_t cnt = 0;

        std::span<Item> GetSpan(std::int32_t begin, std::int32_t end) {
            return std::span(buffer.begin() + begin, buffer.begin() + end);
        }

        std::span<Item> Pull(std::int32_t r) {
            cnt = cnt - r;
            i = i + r;
            return GetSpan(i - r, i);
        }


    public:

        std::size_t Cnt() const {
            return cnt;
        }

        std::span<Item> Push(std::int32_t n) {
            if(i != 0 && i + cnt + n > buffer.capacity()) {
                std::copy_n(
                    buffer.begin() + i,
                    cnt,
                    buffer.begin()
                );
                i = 0;
            }
            buffer.resize(i + cnt + n);
            cnt = cnt + n;
            return GetSpan(i + cnt - n, i + cnt);
        }

        std::optional<std::span<Item>> PullIfEnough(std::int32_t n) {
            if(cnt >= n) {
                return Pull(n);
            }
            else {
                return std::nullopt;
            }
        }

        std::span<Item> PullMax(std::int32_t n) {
            return Pull(std::min(cnt, n));
        }

        std::span<Item> PullMultiple(std::int32_t n) {
            return Pull((cnt / n) * n);
        }

        std::span<Item> PullAll() {
            return Pull(i, i + cnt);
        }
    };
}

