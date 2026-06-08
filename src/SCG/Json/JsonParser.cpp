#include <nlohmann/json.hpp>
#include "Common/Assert.hpp"
#include "JsonParser.hpp"

namespace Sagax::SCG::Json {

    struct JsonHandle::Impl {
        nlohmann::ordered_json j;
    };

    JsonHandle::JsonHandle() : impl(std::make_unique<Impl>()) {
    }

    JsonHandle::~JsonHandle() = default;

    JsonHandle::JsonHandle(const JsonHandle &other) : impl(std::make_unique<Impl>()) {
        impl->j = other.impl->j;
    }


    struct JsonView::Impl {
        nlohmann::ordered_json *j;
    };

    JsonView::JsonView() : impl(std::make_unique<Impl>()) {
    }

    JsonView::~JsonView() = default;

    JsonView::JsonView(const JsonHandle &h) : impl(std::make_unique<Impl>()) {
        impl->j = &h.impl->j;
    }

    JsonView::JsonView(const JsonView &other) : impl(std::make_unique<Impl>()) {
        *impl = *other.impl;
    }

    JsonView &JsonView::operator=(const JsonView &other) {
        *impl = *other.impl;
        return *this;
    }



    JsonHandle ParseJson(const std::string &s) {
        JsonHandle j;
        j.impl->j = nlohmann::json::parse(
            s,
            nullptr,
            true, // allow exceptions
            true, // ignore comments
            true // allow trailing commas
        );
        return j;
    }

    std::string DumpJson(const JsonHandle &j, bool indent) {
        if(indent) {
            return j.impl->j.dump(4);
        }
        else {
            return j.impl->j.dump(-1);
        }
    }




    namespace {
        nlohmann::ordered_json &Get(const JsonView &j) {
            return *j.impl->j;
        }
    }


    bool IsObject(const JsonView &j) {
        return Get(j).is_object();
    }

    bool IsArray(const JsonView &j) {
        return Get(j).is_array();
    }

    bool IsPrimitive(const JsonView &j) {
        return Get(j).is_primitive();
    }

    void SetObject(const JsonView &j) {
        Get(j) = nlohmann::json::object();
    }

    void SetArray(const JsonView &j) {
        Get(j) = nlohmann::json::array();
    }

    bool HasParameter(const JsonView &j, const std::string &key) {
        return IsObject(j) && Get(j).contains(key);
    }

    std::size_t ArraySize(const JsonView &j) {
        return Get(j).size();
    }

    JsonView GetParameter(const JsonView &j, std::string key) {
        JsonView result;
        result.impl->j = &Get(j).find(key).value();
        return result;
    }

    JsonView AddParameter(const JsonView &j, std::string key) {
        JsonView result;
        result.impl->j = &Get(j)[key];
        return result;
    }

    JsonView GetAt(const JsonView &j, std::size_t idx) {
        JsonView result;
        result.impl->j = &Get(j)[idx];
        return result;
    }

    JsonView AddAt(const JsonView &j, std::size_t idx) {
        JsonView result;
        result.impl->j = &Get(j)[idx];
        return result;
    }


    template<typename T>
    void GetValue(const JsonView &j, T &t) {
        try {
            t = Get(j).get<T>();
        }
        catch (const std::exception &e) {
            throw std::runtime_error(
                std::format("could not convert json to type: {}", e.what())
            );
        }
    }

    template<typename T>
    void SetValue(const JsonView &j, const T &t) {
        Get(j) = t;
    }




    #define SGX_EXPLICIT_INST(T) \
        template void GetValue<T>(const JsonView &j, T &t); \
        template void SetValue<T>(const JsonView &j, const T &t);
    SGX_EXPLICIT_INST(float)
    SGX_EXPLICIT_INST(double)
    SGX_EXPLICIT_INST(std::string)
    SGX_EXPLICIT_INST(std::int64_t)
    SGX_EXPLICIT_INST(std::int32_t)
    SGX_EXPLICIT_INST(std::uint32_t)
    SGX_EXPLICIT_INST(std::uint64_t)
    // won't compile without this... on linux it causes duplicate explicit inst :))
    #if defined(__APPLE__)
    SGX_EXPLICIT_INST(std::size_t)
    #endif
    #undef SGX_EXPLICIT_INST

}
