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
    // - provides traced exception messages for parsing errors
    //
    // should maybe add description and/or schema building functionality

    struct JsonHandle {
        struct Impl;
        std::unique_ptr<Impl> impl;
        JsonHandle();
        ~JsonHandle();
        JsonHandle(const JsonHandle &other);
    };

    struct JsonView {
        struct Impl;
        std::unique_ptr<Impl> impl;
        JsonView();
        ~JsonView();
        JsonView(const JsonHandle &h);
        JsonView(const JsonView &other);
        JsonView &operator=(const JsonView &other);
    };



    JsonHandle ParseJson(const std::string &s);
    std::string DumpJson(const JsonHandle &j, bool indent);


    bool IsObject(const JsonView &j);
    bool IsArray(const JsonView &j);
    bool IsPrimitive(const JsonView &j);

    void SetObject(const JsonView &j);
    void SetArray(const JsonView &j);

    bool HasParameter(const JsonView &j, const std::string &key);
    std::size_t ArraySize(const JsonView &j);

    JsonView GetParameter(const JsonView &j, std::string key);
    JsonView AddParameter(const JsonView &j, std::string key);
    JsonView GetAt(const JsonView &j, std::size_t idx);
    JsonView AddAt(const JsonView &j, std::size_t idx);

    template<typename T>
    void GetValue(const JsonView &j, T &t);
    template<typename T>
    void SetValue(const JsonView &j, const T &t);





    template<typename T>
    struct JsonParser {
        virtual ~JsonParser() = default;
        virtual void Parse(const JsonView &j, T &t) const = 0;
        virtual void Serialize(const T &t, const JsonView &j) const = 0;
    };


    template<typename T>
    struct JsonPrimitiveParser : public JsonParser<T> {
        void Parse(const JsonView &j, T &t) const override {
            Common::Assert(IsPrimitive(j), "Json value is not a primitive");
            GetValue<T>(j, t);
        }
        void Serialize(const T &t, const JsonView &j) const override {
            SetValue<T>(j, t);
        }
    };


    template<typename E>
    struct JsonEnumParser : public JsonParser<E> {
        std::vector<std::pair<E, std::string>> entries;

        void AddEnumerator(E value, std::string tag) {
            Common::Assert(!tag.empty(), "enum tag must not be empty");
            for (const auto &p : entries) {
                Common::Assert(p.second != tag, std::format("duplicate enum tag \"{}\"", tag));
                Common::Assert(p.first != value, std::format("duplicate enum value"));
            }
            entries.emplace_back(value, std::move(tag));
        }

        void Parse(const JsonView &j, E &t) const override {
            Common::Assert(IsPrimitive(j), "Json value is not a primitive");
            std::string s;
            GetValue<std::string>(j, s);
            for (const auto &p : entries) {
                if (p.second == s) {
                    t = p.first;
                    return;
                }
            }
            Common::Assert(false, std::format("unknown enum value \"{}\"", s));
        }

        void Serialize(const E &t, const JsonView &j) const override {
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
            void Parse(const JsonView &j, T &t) const override {
                Common::Assert(HasParameter(j, key), std::format("missing object parameter: \"{}\"", key));
                JsonView jp = GetParameter(j, key);
                try {
                    parser->Parse(jp, t.*target);
                }
                catch(const std::exception &e) {
                    throw std::runtime_error(std::format(
                        "failed to parse object parameter: \"{}\"\n{}",
                        key, e.what()
                    ));
                }
            }
            void Serialize(const T &t, const JsonView &j) const override {
                JsonView jp = AddParameter(j, key);
                parser->Serialize(t.*target, jp);
            }
        };

        template<typename S>
        struct ParameterOpt : public JsonParser<T> {
            std::string key;
            std::unique_ptr<JsonParser<S>> parser;
            std::optional<S> T::*target;
            void Parse(const JsonView &j, T &t) const override {
                if(HasParameter(j, key)) {
                    JsonView jp = GetParameter(j, key);
                    try {
                        parser->Parse(jp, (t.*target).value());
                    }
                    catch(const std::exception &e) {
                        throw std::runtime_error(std::format(
                            "failed to parse object parameter: \"{}\"\n{}",
                            key, e.what()
                        ));
                    }
                }
                else {
                    t.*target = std::nullopt;
                }
            }
            void Serialize(const T &t, const JsonView &j) const override {
                if((t.*target).has_value()) {
                    JsonView jp = AddParameter(j, key);
                    parser->Serialize((t.*target).value(), jp);
                }
            }
        };

        template<typename S>
        struct ParameterFlat : public JsonParser<T> {
            std::unique_ptr<JsonParser<S>> parser;
            S T::*target;
            void Parse(const JsonView &j, T &t) const override {
                try {
                    parser->Parse(j, t.*target);
                }
                catch(const std::exception &e) {
                    throw std::runtime_error(std::format(
                        "failed to parse object flat parameter:\n{}",
                        e.what()
                    ));
                }
            }
            void Serialize(const T &t, const JsonView &j) const override {
                parser->Serialize(t.*target, j);
            }
        };

        template<typename S>
        struct ParameterDef : public JsonParser<T> {
            std::string key;
            std::unique_ptr<JsonParser<S>> parser;
            S T::*target;
            S def;
            void Parse(const JsonView &j, T &t) const override {
                if(HasParameter(j, key)) {
                    JsonView jp = GetParameter(j, key);
                    try {
                        parser->Parse(jp, t.*target);
                    }
                    catch(const std::exception &e) {
                        throw std::runtime_error(std::format(
                            "failed to parse object parameter: {}\n{}",
                            key, e.what()
                        ));
                    }
                }
                else {
                    t.*target = def;
                }
            }
            void Serialize(const T &t, const JsonView &j) const override {
                JsonView jp = AddParameter(j, key);
                parser->Serialize(t.*target, jp);
            }
        };

        std::vector<std::unique_ptr<JsonParser<T>>> params;

        template<typename S, typename P>
        P *AddParam(S T::*target, P &&parser, const std::string &key) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto param = std::make_unique<Parameter<S>>();
            param->key = key;
            param->parser = std::move(parserU);
            param->target = target;
            params.push_back(std::move(param));
            return parserP;
        }

        template<typename S, typename P>
        P *AddParamOpt(std::optional<S> T::*target, P &&parser, const std::string &key) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto param = std::make_unique<ParameterOpt<S>>();
            param->key = key;
            param->parser = std::move(parserU);
            param->target = target;
            params.push_back(std::move(param));
            return parserP;
        }

        template<typename S, typename P>
        P *AddParamDef(S T::*target, P &&parser, const S &def, const std::string &key) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto param = std::make_unique<ParameterDef<S>>();
            param->key = key;
            param->parser = std::move(parserU);
            param->def = def;
            param->target = target;
            params.push_back(std::move(param));
            return parserP;
        }

        template<typename S, typename P>
        P *AddParamFlat(S T::*target, P &&parser) {
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");
            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto param = std::make_unique<ParameterFlat<S>>();
            param->parser = std::move(parserU);
            param->target = target;
            params.push_back(std::move(param));
            return parserP;
        }

        void Parse(const JsonView &j, T &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            for(const auto &p : params) {
                p->Parse(j, t);
            }
        }

        void Serialize(const T &t, const JsonView &j) const override {
            if(!IsObject(j)) {
                SetObject(j);
            }
            for(const auto &p : params) {
                p->Serialize(t, j);
            }
        }
    };



    template<typename T>
    struct JsonVariantParser : public JsonParser<T> {
        struct AltBase : public JsonParser<T> {
            std::optional<std::string> subKey;
            std::string typeTag;
            std::type_index typeIndex{typeid(void)};
            virtual ~AltBase() = default;
            virtual bool HasAlt(const T &t) const = 0;
        };

        template<typename S>
        struct Alt : public AltBase {
            std::unique_ptr<JsonParser<S>> parser;

            bool HasAlt(const T &t) const {
                return std::holds_alternative<S>(t);
            }

            void Parse(const JsonView &j, T &t) const override {
                S value;
                try {
                    parser->Parse(j, value);
                }
                catch(const std::exception &e) {
                    throw std::runtime_error(std::format(
                        "failed to parse variant with alternative: \"{}\"\n{}",
                        this->typeTag, e.what()
                    ));
                }
                t = std::move(value);
            }

            void Serialize(const T &t, const JsonView &j) const override {
                const S *value = std::get_if<S>(&t);
                parser->Serialize(*value, j);
            }
        };

        std::string typeTagKey;
        std::vector<std::unique_ptr<AltBase>> alts;

        template<typename S, typename P>
        P *AddAlt(const std::string &typeTag, std::optional<std::string> subKey, P &&parser) {
            Common::Assert(!typeTag.empty(), "variant type tag must not be empty");
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");

            std::type_index newTypeIndex = std::type_index(typeid(S));
            for(const auto &alt : alts) {
                Common::Assert(alt->typeTag != typeTag, std::format("duplicate alternative type tag \"{}\"", typeTag));
                Common::Assert(alt->typeIndex != newTypeIndex, std::format("duplicate alternative type \"{}\"", typeTag));
            }

            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto variant = std::make_unique<Alt<S>>();
            variant->typeTag = typeTag;
            variant->subKey = subKey;
            variant->typeIndex = newTypeIndex;
            variant->parser = std::move(parserU);
            alts.push_back(std::move(variant));
            return parserP;
        }

        void Parse(const JsonView &j, T &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            Common::Assert(HasParameter(j, typeTagKey), std::format("missing type tag: \"{}\"", typeTagKey));

            JsonView typeTagH = GetParameter(j, typeTagKey);
            Common::Assert(IsPrimitive(typeTagH), "Json type tag is not a primitive");
            std::string typeTag;
            GetValue<std::string>(typeTagH, typeTag);

            bool found = false;

            for(const auto &alt : alts) {
                if(alt->typeTag == typeTag) {
                    found = true;
                    if(alt->subKey.has_value()) {
                        Common::Assert(HasParameter(j, alt->subKey.value()), std::format("missing subparameter key: \"{}\"", alt->subKey.value()));
                        JsonView subH = GetParameter(j, alt->subKey.value());
                        alt->Parse(subH, t);
                    }
                    else {
                        alt->Parse(j, t);
                    }
                }
            }

            Common::Assert(found, std::format("unknown variant type \"{}\"", typeTag));
        }

        void Serialize(const T &t, const JsonView &j) const override {
            bool found = false;
            if(!IsObject(j)) {
                SetObject(j);
            }

            for(const auto &alt : alts) {
                if(alt->HasAlt(t)) {
                    found = true;
                    if(alt->subKey.has_value()) {
                        JsonView typeTagH = AddParameter(j, typeTagKey);
                        SetValue<std::string>(typeTagH, alt->typeTag);
                        JsonView subH = AddParameter(j, alt->subKey.value());
                        alt->Serialize(t, subH);
                    }
                    else {
                        JsonView typeTagH = AddParameter(j, typeTagKey);
                        SetValue<std::string>(typeTagH, alt->typeTag);
                        alt->Serialize(t, j);
                    }
                }
            }

            Common::Assert(found, "variant value does not match any registered type");
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

            void Parse(const JsonView &j, std::unique_ptr<Base> &t) const override {
                S value;
                try {
                    parser->Parse(j, value);
                }
                catch(const std::exception &e) {
                    throw std::runtime_error(
                        "failed to parse subtype: \"{}\"\n{}",
                        this->typeTag, e.what()
                    );
                }
                t = std::make_unique<S>(std::move(value));
            }

            void Serialize(const std::unique_ptr<Base> &t, const JsonView &j) const override {
                const S *s = dynamic_cast<const S*>(t.get());
                Common::Assert(s != nullptr, "subtype value type mismatch");
                parser->Serialize(*s, j);
            }
        };

        std::string typeTagKey;
        std::optional<std::string> subKey;
        std::vector<std::unique_ptr<SubtypeBase>> subtypes;

        template<typename S, typename P>
        P *AddSubtype(const std::string &typeTag, P &&parser) {
            Common::Assert(!typeTag.empty(), "subtype type tag must not be empty");
            static_assert(std::is_base_of_v<JsonParser<S>, std::remove_reference_t<P>>, "parser must derive from JsonParser<S>");

            std::type_index newTypeIndex = std::type_index(typeid(S));
            for(const auto &st : subtypes) {
                Common::Assert(st->typeTag != typeTag, std::format("duplicate subtype type tag \"{}\"", typeTag));
                Common::Assert(st->typeIndex != newTypeIndex, std::format("duplicate subtype type \"{}\"", typeTag));
            }

            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            auto st = std::make_unique<Subtype<S>>();
            st->typeTag = typeTag;
            st->typeIndex = newTypeIndex;
            st->parser = std::move(parserU);
            subtypes.push_back(std::move(st));
            return parserP;
        }

        void Parse(const JsonView &j, std::unique_ptr<Base> &t) const override {
            Common::Assert(IsObject(j), "Json value is not an object");
            Common::Assert(HasParameter(j, typeTagKey), std::format("missing type tag: \"{}\"", typeTagKey));

            JsonView typeTagH = GetParameter(j, typeTagKey);
            Common::Assert(IsPrimitive(typeTagH), "Json type tag is not a primitive");
            std::string typeTag;
            GetValue<std::string>(typeTagH, typeTag);

            bool found = false;

            if(subKey.has_value()) {
                Common::Assert(HasParameter(j, subKey.value()), std::format("missing subtype: \"{}\"", subKey.value()));
                JsonView subH = GetParameter(j, subKey.value());

                for(const auto &st : subtypes) {
                    if(st->typeTag == typeTag) {
                        found = true;
                        st->Parse(subH, t);
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

            Common::Assert(found, std::format("unknown subtype type \"{}\"", typeTag));
        }

        void Serialize(const std::unique_ptr<Base> &t, const JsonView &j) const override {
            bool found = false;
            if(!IsObject(j)) {
                SetObject(j);
            }

            if(subKey.has_value()) {
                JsonView subH = AddParameter(j, subKey.value());
                for(const auto &st : subtypes) {
                    if(st->IsInstance(t.get())) {
                        found = true;
                        st->Serialize(t, subH);
                        JsonView typeTagH = AddParameter(j, typeTagKey);
                        SetValue<std::string>(typeTagH, st->typeTag);
                    }
                }
            }
            else {
                for(const auto &st : subtypes) {
                    if(st->IsInstance(t.get())) {
                        found = true;
                        st->Serialize(t, j);
                        JsonView typeTagH = AddParameter(j, typeTagKey);
                        SetValue<std::string>(typeTagH, st->typeTag);
                    }
                }
            }

            Common::Assert(found, "subtype value does not match any registered type");
        }
    };



    template<typename T>
    struct JsonArrayParser : public JsonParser<std::vector<T>> {
        std::unique_ptr<JsonParser<T>> itemParser;
        std::optional<std::int32_t> minSize;
        std::optional<std::int32_t> maxSize;

        template<typename P>
        P *SetItemParser(P &&parser) {
            static_assert(std::is_base_of_v<JsonParser<T>, std::remove_reference_t<P>>, "parser must derive from JsonParser<T>");
            auto parserU = std::make_unique<std::remove_reference_t<P>>(std::forward<P>(parser));
            auto parserP = parserU.get();
            itemParser = std::move(parserU);
            return parserP;
        }

        void SetSizeRange(std::optional<std::int32_t> min, std::optional<std::int32_t> max) {
            Common::Assert(!min.has_value() || !max.has_value() || min.value() <= max.value(), "expected min <= max");
            minSize = min;
            maxSize = max;
        }

        void Parse(const JsonView &j, std::vector<T> &t) const override {
            Common::Assert(IsArray(j), "Json value is not an array");
            Common::Assert(bool(itemParser), "item parser not set");

            std::size_t size = ArraySize(j);
            Common::Assert(
                ((!minSize.has_value()) || size >= minSize.value()) &&
                ((!maxSize.has_value()) || size <= maxSize.value()),
                std::format("Json array size {} out of expected range", size)
            );

            t.clear();
            t.reserve(size);
            for(std::size_t i = 0; i < size; ++i) {
                JsonView ji = GetAt(j, i);
                T item;
                try {
                    itemParser->Parse(ji, item);
                }
                catch(const std::exception &e) {
                    throw std::runtime_error(std::format("failed to parse array item: [{}]\n{}", i, e.what()));
                }
                t.push_back(std::move(item));
            }
        }

        void Serialize(const std::vector<T> &t, const JsonView &j) const override {
            Common::Assert(bool(itemParser), "item parser not set");
            Common::Assert(
                ((!minSize.has_value()) || t.size() >= minSize.value()) &&
                ((!maxSize.has_value()) || t.size() <= maxSize.value()),
                std::format("array size {} out of expected range", t.size())
            );

            if(!IsArray(j)) {
                SetArray(j);
            }
            for(std::size_t i = 0; i < t.size(); ++i) {
                JsonView ji = AddAt(j, i);
                itemParser->Serialize(t[i], ji);
            }
        }
    };


}


