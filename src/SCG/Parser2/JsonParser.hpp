#pragma once
#include <memory>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
#include <variant>
#include <utility>
#include <typeindex>
#include <format>
#include <type_traits>
#include "Common/Assert.hpp"

namespace Sagax::SCG::Json {

    // a wrapper around nlohmann::json that:
    // - hides that header
    // - adds a composable type hierarchy for declarative reuse
    // - does both parsing and serialization
    //
    // should later add GetDescription and/or GetSchema functionality

    struct JsonHandle {
        struct Impl;
        std::unique_ptr<Impl> impl;
        JsonHandle();
        ~JsonHandle();
        JsonHandle(const JsonHandle &other);
        JsonHandle(JsonHandle &&other) noexcept = default;
        JsonHandle &operator=(const JsonHandle &other);
        JsonHandle &operator=(JsonHandle &&other) noexcept = default;
    };

    bool IsObject(const JsonHandle &j);
    bool IsArray(const JsonHandle &j);
    bool IsPrimitive(const JsonHandle &j);

    void SetObject(JsonHandle &j);
    void SetArray(JsonHandle &j);

    bool HasParameter(const JsonHandle &j, const std::string &key);
    std::size_t ArraySize(const JsonHandle &j);

    void GetParameter(const JsonHandle &j, std::string key, JsonHandle &result);
    void SetParameter(JsonHandle &j, std::string key, const JsonHandle &value);
    void GetAt(const JsonHandle &j, std::size_t idx, JsonHandle &result);
    void SetAt(JsonHandle &j, std::size_t idx, const JsonHandle &value);

    JsonHandle ParseJson(const std::string &s);
    std::string DumpJson(const JsonHandle &j);

    template<typename T>
    void GetValue(const JsonHandle &j, T &t);
    template<typename T>
    void SetValue(JsonHandle &j, const T &t);


    template<typename T>
    struct JsonParser {
        virtual ~JsonParser() = default;
        virtual void Parse(const JsonHandle &j, T &t) const = 0;
        virtual void Serialize(const T &t, JsonHandle &j) const = 0;
    };




    template<typename T>
    struct JsonPrimitiveParser : public JsonParser<T> {
        void Parse(const JsonHandle &j, T &t) const override {
            Common::Assert(IsPrimitive(j), "Json value is not a primitive");
            GetValue<T>(j, t);
        }
        void Serialize(const T &t, JsonHandle &j) const override {
            SetValue<T>(j, t);
        }
    };


    template<typename E>
    struct JsonEnumParser : public JsonParser<E> {
        std::vector<std::pair<E, std::string>> entries;

        void AddEnumerator(E value, std::string tag) {
            Common::Assert(!tag.empty(), "enum tag must not be empty");
            for (const auto &p : entries) {
                Common::Assert(p.second != tag, std::format("duplicate enum tag '{}'", tag));
                Common::Assert(p.first != value, std::format("duplicate enum value"));
            }
            entries.emplace_back(value, std::move(tag));
        }

        void Parse(const JsonHandle &j, E &t) const override {
            Common::Assert(IsPrimitive(j), "Json value is not a primitive");
            std::string s;
            GetValue<std::string>(j, s);
            for (const auto &p : entries) {
                if (p.second == s) {
                    t = p.first;
                    return;
                }
            }
            Common::Assert(false, std::format("unknown enum value '{}'", s));
        }

        void Serialize(const E &t, JsonHandle &j) const override {
            for (const auto &p : entries) {
                if (p.first == t) {
                    SetValue<std::string>(j, p.second);
                    return;
                }
            }
            Common::Assert(false, "enum value not registered");
        }
    };





    template<typename T>
    struct JsonObjectParser : public JsonParser<T> {
        template<typename S>
        struct Parameter : public JsonParser<T> {
            std::string key;
            std::unique_ptr<JsonParser<S>> parser;
            S T::*target;
            void Parse(const JsonHandle &j, T &t) const override {
                JsonHandle jp;
                Common::Assert(HasParameter(j, key), std::format("Json key '{}' not found", key));
                GetParameter(j, key, jp);
                parser->Parse(jp, t.*target);
            }
            void Serialize(const T &t, JsonHandle &j) const override {
                JsonHandle jp;
                parser->Serialize(t.*target, jp);
                SetParameter(j, key, jp);
            }
        };

        template<typename S>
        struct ParameterOpt : public JsonParser<T> {
            std::string key;
            std::unique_ptr<JsonParser<S>> parser;
            std::optional<S> T::*target;
            void Parse(const JsonHandle &j, T &t) const override {
                if(HasParameter(j, key)) {
                    JsonHandle jp;
                    GetParameter(j, key, jp);
                    parser->Parse(jp, t.*target);
                }
                else {
                    t.*target = std::nullopt;
                }
            }
            void Serialize(const T &t, JsonHandle &j) const override {
                if((t.*target).has_value()) {
                    JsonHandle jp;
                    parser->Serialize((t.*target).value(), jp);
                    SetParameter(j, key, jp);
                }
            }
        };

        std::vector<std::unique_ptr<JsonParser<T>>> params;

        template<typename S, typename P>
        void AddParam(S T::*target, P &&parser, const std::string &key) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto param = std::make_unique<Parameter<S>>();
            param->key = key;
            param->parser = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            param->target = target;
            params.push_back(std::move(param));
        }

        template<typename S, typename P>
        void AddParamOpt(std::optional<S> T::*target, P &&parser, const std::string &key) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto param = std::make_unique<ParameterOpt<S>>();
            param->key = key;
            param->parser = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            param->target = target;
            params.push_back(std::move(param));
        }

        void Parse(const JsonHandle &j, T &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            for(const auto &p : params) {
                p->Parse(j, t);
            }
        }

        void Serialize(const T &t, JsonHandle &j) const override {
            SetObject(j);
            for(const auto &p : params) {
                p->Serialize(t, j);
            }
        }
    };



    template<typename T>
    struct JsonVariantParser : public JsonParser<T> {
        struct VariantBase : public JsonParser<T> {
            std::string typeTag;
            std::type_index typeIndex{typeid(void)};
            virtual ~VariantBase() = default;
            virtual bool HasVariant(const T &t) const = 0;
        };

        template<typename S>
        struct Variant : public VariantBase {
            std::unique_ptr<JsonParser<S>> parser;

            bool HasVariant(const T &t) const {
                return std::holds_alternative<S>(t);
            }

            void Parse(const JsonHandle &j, T &t) const override {
                S value;
                parser->Parse(j, value);
                t = std::move(value);
            }

            void Serialize(const T &t, JsonHandle &j) const override {
                const S *value = std::get_if<S>(&t);
                parser->Serialize(*value, j);
            }
        };

        std::string typeTagKey;
        std::optional<std::string> subTypeKey;
        std::vector<std::unique_ptr<VariantBase>> variants;

        template<typename S, typename P>
        void AddVariant(const std::string &typeTag, P &&parser) {
            Common::Assert(!typeTag.empty(), "variant type tag must not be empty");
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");

            std::type_index newTypeIndex = std::type_index(typeid(S));
            for(const auto &variant : variants) {
                Common::Assert(variant->typeTag != typeTag, std::format("duplicate variant type tag '{}'", typeTag));
                Common::Assert(variant->typeIndex != newTypeIndex, std::format("duplicate variant type '{}'", typeTag));
            }

            auto variant = std::make_unique<Variant<S>>();
            variant->typeTag = typeTag;
            variant->typeIndex = newTypeIndex;
            variant->parser = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            variants.push_back(std::move(variant));
        }

        void Parse(const JsonHandle &j, T &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            Common::Assert(HasParameter(j, typeTagKey), std::format("Json key '{}' not found", typeTagKey));

            JsonHandle typeTagH;
            GetParameter(j, typeTagKey, typeTagH);
            Common::Assert(IsPrimitive(typeTagH), "Json type tag is not a primitive");
            std::string typeTag;
            GetValue<std::string>(typeTagH, typeTag);


            bool found = false;

            if(subTypeKey.has_value()) {
                JsonHandle subTypeH;
                Common::Assert(HasParameter(j, subTypeKey.value()), std::format("Json key '{}' not found", subTypeKey.value()));
                GetParameter(j, subTypeKey.value(), subTypeH);

                for(const auto &variant : variants) {
                    if(variant->typeTag == typeTag) {
                        found = true;
                        variant->Parse(subTypeH, t);
                    }
                }
            }
            else {
                for(const auto &variant : variants) {
                    if(variant->typeTag == typeTag) {
                        found = true;
                        variant->Parse(j, t);
                    }
                }
            }

            Common::Assert(found, std::format("unknown variant type '{}'", typeTag));
        }

        void Serialize(const T &t, JsonHandle &j) const override {
            JsonHandle typeTagH;

            bool found = false;

            if(subTypeKey.has_value()) {
                JsonHandle subTypeH;
                for(const auto &variant : variants) {
                    if(variant->HasVariant(t)) {
                        found = true;
                        SetValue<std::string>(typeTagH, variant->typeTag);
                        variant->Serialize(t, subTypeH);
                    }
                }
                SetObject(j);
                SetParameter(j, subTypeKey.value(), subTypeH);
            }
            else {
                for(const auto &variant : variants) {
                    if(variant->HasVariant(t)) {
                        found = true;
                        SetValue<std::string>(typeTagH, variant->typeTag);
                        variant->Serialize(t, j);
                    }
                }
            }

            Common::Assert(found, "variant value does not match any registered type");

            Common::Assert(IsObject(j), "variant parser with no subtype key can only contain objects");
            Common::Assert(!HasParameter(j, typeTagKey), "variant alternative parser set the type tag attribute");
            SetParameter(j, typeTagKey, typeTagH);
        }
    };


    template<typename Base>
    struct JsonSubtypeParser : public JsonParser<std::unique_ptr<Base>> {
        struct SubtypeBase : public JsonParser<std::unique_ptr<Base>> {
            std::string typeTag;
            std::type_index typeIndex{typeid(void)};
            virtual ~SubtypeBase() = default;
            virtual bool IsInstance(const Base *b) const = 0;
        };

        template<typename S>
        struct Subtype : public SubtypeBase {
            std::unique_ptr<JsonParser<S>> parser;

            bool IsInstance(const Base *b) const override {
                return dynamic_cast<const S*>(b) != nullptr;
            }

            void Parse(const JsonHandle &j, std::unique_ptr<Base> &t) const override {
                S value;
                parser->Parse(j, value);
                t = std::make_unique<S>(std::move(value));
            }

            void Serialize(const std::unique_ptr<Base> &t, JsonHandle &j) const override {
                const S *s = dynamic_cast<const S*>(t.get());
                Common::Assert(s != nullptr, "subtype value type mismatch");
                parser->Serialize(*s, j);
            }
        };

        std::string typeTagKey;
        std::optional<std::string> subTypeKey;
        std::vector<std::unique_ptr<SubtypeBase>> subtypes;

        template<typename S, typename P>
        void AddSubtype(const std::string &typeTag, P &&parser) {
            Common::Assert(!typeTag.empty(), "subtype type tag must not be empty");
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");

            std::type_index newTypeIndex = std::type_index(typeid(S));
            for(const auto &st : subtypes) {
                Common::Assert(st->typeTag != typeTag, std::format("duplicate subtype type tag '{}'", typeTag));
                Common::Assert(st->typeIndex != newTypeIndex, std::format("duplicate subtype type '{}'", typeTag));
            }

            auto st = std::make_unique<Subtype<S>>();
            st->typeTag = typeTag;
            st->typeIndex = newTypeIndex;
            st->parser = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            subtypes.push_back(std::move(st));
        }

        void Parse(const JsonHandle &j, std::unique_ptr<Base> &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            Common::Assert(HasParameter(j, typeTagKey), std::format("Json key '{}' not found", typeTagKey));

            JsonHandle typeTagH;
            GetParameter(j, typeTagKey, typeTagH);
            Common::Assert(IsPrimitive(typeTagH), "Json type tag is not a primitive");
            std::string typeTag;
            GetValue<std::string>(typeTagH, typeTag);

            bool found = false;

            if(subTypeKey.has_value()) {
                JsonHandle subTypeH;
                Common::Assert(HasParameter(j, subTypeKey.value()), std::format("Json key '{}' not found", subTypeKey.value()));
                GetParameter(j, subTypeKey.value(), subTypeH);

                for(const auto &st : subtypes) {
                    if(st->typeTag == typeTag) {
                        found = true;
                        st->Parse(subTypeH, t);
                    }
                }
            }
            else {
                for(const auto &st : subtypes) {
                    if(st->typeTag == typeTag) {
                        found = true;
                        st->Parse(j, t);
                    }
                }
            }

            Common::Assert(found, std::format("unknown subtype type '{}'", typeTag));
        }

        void Serialize(const std::unique_ptr<Base> &t, JsonHandle &j) const override {
            JsonHandle typeTagH;

            bool found = false;

            if(subTypeKey.has_value()) {
                JsonHandle subTypeH;
                for(const auto &st : subtypes) {
                    if(st->IsInstance(t.get())) {
                        found = true;
                        SetValue<std::string>(typeTagH, st->typeTag);
                        st->Serialize(t, subTypeH);
                    }
                }
                SetObject(j);
                SetParameter(j, subTypeKey.value(), subTypeH);
            }
            else {
                for(const auto &st : subtypes) {
                    if(st->IsInstance(t.get())) {
                        found = true;
                        SetValue<std::string>(typeTagH, st->typeTag);
                        st->Serialize(t, j);
                    }
                }
            }

            Common::Assert(found, "subtype value does not match any registered type");

            Common::Assert(IsObject(j), "subtype parser with no subtype key can only contain objects");
            Common::Assert(!HasParameter(j, typeTagKey), "subtype alternative parser set the type tag attribute");
            SetParameter(j, typeTagKey, typeTagH);
        }
    };



    template<typename T>
    struct JsonArrayParser : public JsonParser<std::vector<T>> {
        std::unique_ptr<JsonParser<T>> itemParser;
        std::optional<std::int32_t> minSize;
        std::optional<std::int32_t> maxSize;

        template<typename P>
        void SetItemParser(P &&parser) {
            static_assert(std::is_base_of_v<JsonParser<T>, std::remove_reference_t<P>>, "parser must derive from JsonParser<T>");
            itemParser = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
        }

        void SetSizeRange(std::optional<std::int32_t> min, std::optional<std::int32_t> max) {
            Common::Assert(!min.has_value() || !max.has_value() || min.value() <= max.value(), "expected min <= max");
            minSize = min;
            maxSize = max;
        }

        void Parse(const JsonHandle &j, std::vector<T> &t) const override {
            Common::Assert(IsArray(j), "Json value is not an array");
            Common::Assert(itemParser, "item parser not set");

            std::size_t size = ArraySize(j);
            Common::Assert(
                ((!minSize.has_value()) || size >= minSize.value()) &&
                ((!maxSize.has_value()) || size <= maxSize.value()),
                std::format("Json array size {} out of expected range", size)
            );

            t.clear();
            t.reserve(size);
            for(std::size_t i = 0; i < size; ++i) {
                JsonHandle ji;
                GetAt(j, i, ji);
                T item;
                itemParser->Parse(ji, item);
                t.push_back(std::move(item));
            }
        }

        void Serialize(const std::vector<T> &t, JsonHandle &j) const override {
            Common::Assert(itemParser, "item parser not set");
            Common::Assert(
                ((!minSize.has_value()) || t.size() >= minSize.value()) &&
                ((!maxSize.has_value()) || t.size() <= maxSize.value()),
                std::format("array size {} out of expected range", t.size())
            );

            SetArray(j);
            for(std::size_t i = 0; i < t.size(); ++i) {
                JsonHandle ji;
                itemParser->Serialize(t[i], ji);
                SetAt(j, i, ji);
            }
        }
    };


}


