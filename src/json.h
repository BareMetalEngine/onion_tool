#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

//--

enum class SimpleJsonValueType : uint8_t
{
    Value,
    Array,
    Object,
};

class SimpleJson;
typedef std::shared_ptr<SimpleJson> SimpleJsonPtr;

struct SimpleJsonToken
{
    SimpleJsonToken();
    SimpleJsonToken(const SimpleJsonPtr& ptr);
    SimpleJsonToken(std::nullptr_t);
    ~SimpleJsonToken();

    inline operator bool() const { return m_ptr != nullptr; }

    SimpleJsonToken operator[](std::string_view key) const;
    SimpleJsonToken operator[](const char* key) const;
    SimpleJsonToken operator[](int index) const;

    std::vector<SimpleJsonToken> values() const;

    const std::string& str() const;

private:
    SimpleJsonPtr m_ptr;
};

class SimpleJson
{
public:
    SimpleJson();
    SimpleJson(std::string_view value);
    SimpleJson(SimpleJsonValueType type);
    ~SimpleJson();

    inline SimpleJsonValueType type() const { return m_type; }
    inline const std::string& value() const { return m_value; }

    inline const std::vector<SimpleJsonPtr>& values() const { return m_arrayElements; }

    static SimpleJsonPtr Array();
    static SimpleJsonPtr Object();
    static SimpleJsonPtr Value(std::string_view txt);

    void clear();
    void set(std::string_view value);
    void remove(std::string_view name);
    void remove(const SimpleJsonPtr& val);

    void set(std::string_view name, const SimpleJsonPtr& val);
    void set(std::string_view name, std::string_view value);

    void append(const SimpleJsonPtr& val);
    void append(std::string_view value);

    SimpleJsonPtr get(std::string_view key) const;
    SimpleJsonPtr getSafe(std::string_view key, std::string_view defaultValue = "");

    void print(std::stringstream& f) const;
    std::string toString() const;

    static SimpleJsonPtr Parse(std::string_view txt);
    static SimpleJsonPtr Parse(Parser& p);

private:
	SimpleJsonValueType m_type = SimpleJsonValueType::Value;

	std::string m_value;
	std::vector<SimpleJsonPtr> m_arrayElements;
	std::unordered_map<std::string, SimpleJsonPtr> m_dictionaryElements;

    static void PrintJsonString(std::stringstream& f, std::string_view txt);
};

//--