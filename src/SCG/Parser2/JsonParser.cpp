#include <nlohmann/json.hpp>
#include "Common/Assert.hpp"
#include "JsonParser.hpp"

namespace Sagax::SCG::Json {

    struct JsonHandle::Impl {
        nlohmann::json j;
    };

    JsonHandle::JsonHandle() : impl(std::make_unique<Impl>()) {
    }

    JsonHandle::~JsonHandle() = default;

    JsonHandle::JsonHandle(const JsonHandle &other) 
        : impl(std::make_unique<Impl>()) {
        if (other.impl) {
            impl->j = other.impl->j;
        }
    }

    JsonHandle &JsonHandle::operator=(const JsonHandle &other) {
        if (this != &other) {
            if (!impl) {
                impl = std::make_unique<Impl>();
            }
            if (other.impl) {
                impl->j = other.impl->j;
            }
        }
        return *this;
    }

    bool IsObject(const JsonHandle &j) {
        return j.impl->j.is_object();
    }

    bool IsArray(const JsonHandle &j) {
        return j.impl->j.is_array();
    }

    bool IsPrimitive(const JsonHandle &j) {
        return j.impl->j.is_primitive();
    }

    void SetObject(JsonHandle &j) {
        j.impl->j = nlohmann::json::object();
    }

    void SetArray(JsonHandle &j) {
        j.impl->j = nlohmann::json::array();
    }

    bool HasParameter(const JsonHandle &j, const std::string &key) {
        return IsObject(j) && j.impl->j.contains(key);
    }

    std::size_t ArraySize(const JsonHandle &j) {
        return j.impl->j.size();
    }

    void GetParameter(const JsonHandle &j, std::string key, JsonHandle &result) {
        auto it = j.impl->j.find(key);
        if (it != j.impl->j.end()) {
            result.impl->j = it.value();
        }
    }

    void SetParameter(JsonHandle &j, std::string key, const JsonHandle &value) {
        if (!j.impl->j.is_object()) {
            j.impl->j = nlohmann::json::object();
        }
        j.impl->j[key] = value.impl->j;
    }

    void GetAt(const JsonHandle &j, std::size_t idx, JsonHandle &result) {
        if (idx < j.impl->j.size()) {
            result.impl->j = j.impl->j[idx];
        }
    }

    void SetAt(JsonHandle &j, std::size_t idx, const JsonHandle &value) {
        if (!j.impl->j.is_array()) {
            j.impl->j = nlohmann::json::array();
        }
        j.impl->j[idx] = value.impl->j;
    }

    JsonHandle ParseJson(const std::string &s) {
        JsonHandle j;
        j.impl->j = nlohmann::json::parse(s);
        return std::move(j);
    }

    std::string DumpJson(const JsonHandle &j) {
        return j.impl->j.dump();
    }




    template<typename T>
    void GetValue(const JsonHandle &j, T &t) {
        try {
            t = j.impl->j.get<T>();
        }
        catch (const std::exception &e) {
            throw std::runtime_error(
                std::format("could not convert json to type: {}", e.what())
            );
        }
    }

    template<typename T>
    void SetValue(JsonHandle &j, const T &t) {
        j.impl->j = t;
    }



    #define SGX_EXPLICIT_INST(T) \
        template void GetValue<T>(const JsonHandle &j, T &t); \
        template void SetValue<T>(JsonHandle &j, const T &t);
    SGX_EXPLICIT_INST(float)
    SGX_EXPLICIT_INST(double)
    SGX_EXPLICIT_INST(std::string)
    SGX_EXPLICIT_INST(std::int64_t)
    SGX_EXPLICIT_INST(std::int32_t)
    #undef SGX_EXPLICIT_INST

}
