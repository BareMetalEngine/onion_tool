#include "common.h"
#include "utils.h"
#include "json.h"

//--

SimpleJson::SimpleJson()
{}

SimpleJson::SimpleJson(std::string_view value)
	: m_type(SimpleJsonValueType::Value)
    , m_value(value)
{
}

SimpleJson::SimpleJson(SimpleJsonValueType type)
    : m_type(type)
{
}

SimpleJson::~SimpleJson()
{}

SimpleJsonPtr SimpleJson::Array()
{
    return std::make_shared<SimpleJson>(SimpleJsonValueType::Array);
}

SimpleJsonPtr SimpleJson::Object()
{
    return std::make_shared<SimpleJson>(SimpleJsonValueType::Object);
}

SimpleJsonPtr SimpleJson::Value(std::string_view txt)
{
    return std::make_shared<SimpleJson>(txt);
}

void SimpleJson::clear()
{
    m_value.clear();
    m_arrayElements.clear();
    m_dictionaryElements.clear();
}

void SimpleJson::set(std::string_view value)
{
    assert(m_type == SimpleJsonValueType::Value);
    m_value = value;
}

void SimpleJson::remove(std::string_view name)
{
    assert(m_type == SimpleJsonValueType::Object);
	auto it = m_dictionaryElements.find(std::string(name));
	if (it != m_dictionaryElements.end())
		m_dictionaryElements.erase(it);
}

void SimpleJson::remove(const SimpleJsonPtr& val)
{
    assert(m_type == SimpleJsonValueType::Object);

    auto it = std::find(m_arrayElements.begin(), m_arrayElements.end(), val);
	if (it != m_arrayElements.end())
        m_arrayElements.erase(it);
}

void SimpleJson::set(std::string_view name, const SimpleJsonPtr& val)
{
    assert(m_type == SimpleJsonValueType::Object);

    if (val)
        m_dictionaryElements[std::string(name)] = val;
    else
        remove(name);
}

void SimpleJson::set(std::string_view name, std::string_view value)
{
    assert(m_type == SimpleJsonValueType::Object);
    set(name, SimpleJson::Value(value));
}

void SimpleJson::append(const SimpleJsonPtr& val)
{
    assert(m_type == SimpleJsonValueType::Array);
    assert(val);
    m_arrayElements.push_back(val);
}

void SimpleJson::append(std::string_view value)
{
    append(SimpleJson::Value(value));
}

SimpleJsonPtr SimpleJson::get(std::string_view key) const
{
    if (m_type == SimpleJsonValueType::Object)
    {
		auto it = m_dictionaryElements.find(std::string(key));
        if (it != m_dictionaryElements.end())
            return it->second;
    }

    return nullptr;
}

SimpleJsonPtr SimpleJson::getSafe(std::string_view key, std::string_view defaultValue)
{
    if (const auto val = get(key))
        return val;
	return SimpleJson::Value(defaultValue);
}

void SimpleJson::PrintJsonString(std::stringstream& f, std::string_view txt)
{
    for (const auto ch : txt)
    {
        if (ch == '\"')
            f << "\\\"";
        else if (ch == '\'')
            f << "\\\'";
        else
            f << ch;
    }
}

void SimpleJson::print(std::stringstream& f) const
{
    if (m_type == SimpleJsonValueType::Value)
    {
        if (m_value == "true")
            f << "true";
		else if (m_value == "false")
			f << "false";
        else
        {
            f << "\"";
            PrintJsonString(f, m_value);
            f << "\"";
        }
    }
    else if (m_type == SimpleJsonValueType::Array)
	{
		f << "[";
        bool separator = false;
        for (const auto& value : m_arrayElements)
        {
            if (separator)
                f << ", ";
            value->print(f);
            separator = true;
        }
		f << "]";
    }
	else if (m_type == SimpleJsonValueType::Object)
	{
		f << "{";
		bool separator = false;
		for (const auto& it : m_dictionaryElements)
		{
			if (separator)
				f << ", ";

            f << "\"";
            PrintJsonString(f, it.first);
            f << "\": ";
			it.second->print(f);
			separator = true;
		}
		f << "}";
	}
}

std::string SimpleJson::toString() const
{
    std::stringstream stream;
    print(stream);
    return stream.str();
}

SimpleJsonPtr SimpleJson::Parse(Parser& p)
{
    if (p.parseKeyword("["))
    {
        auto ret = SimpleJson::Array();
        bool needsSeparator = false;
        while (p.parseWhitespaces())
        {
            if (p.parseKeyword("]"))
                return ret;

            if (needsSeparator)
            {
                if (!p.parseKeyword(","))
                {
					LogError() << "[JSON] Expected , between array elements";
					return nullptr;
                }
            }

            if (auto element = Parse(p))
                ret->m_arrayElements.push_back(element);
            else
                return nullptr;

            needsSeparator = true;
        }

        LogError() << "[JSON] Unexpected end of JSon array";
        return nullptr;
    }
	else if (p.parseKeyword("{"))
	{
		auto ret = SimpleJson::Object();
		bool needsSeparator = false;
		while (p.parseWhitespaces())
		{
			if (p.parseKeyword("}"))
				return ret;

			if (needsSeparator)
			{
				if (!p.parseKeyword(","))
				{
					LogError() << "[JSON] Expected , between object elements";
					return nullptr;
				}
			}

            std::string_view name;
            if (!p.parseString(name, ":,}"))
            {
				LogError() << "[JSON] Expected object property name";
				return nullptr;
            }

            if (!p.parseKeyword(":"))
            {
				LogError() << "[JSON] Expected : between property name and value in object";
				return nullptr;
            }

            if (auto element = Parse(p))
                ret->m_dictionaryElements[std::string(name)] = element;
			else
				return nullptr;

            needsSeparator = true;
		}

		LogError() << "[JSON] Unexpected end of JSon object";
		return nullptr;
	}
    else
    {
		std::string value;
		if (!p.parseStringWithScapement(value, ",}]"))
		{
			LogError() << "[JSON] Expected value";
			return nullptr;
		}

        return SimpleJson::Value(value);
    }
}

SimpleJsonPtr SimpleJson::Parse(std::string_view txt)
{
    Parser p(txt);
    return Parse(p);
}

//--

SimpleJsonToken::SimpleJsonToken()
{}

SimpleJsonToken::SimpleJsonToken(const SimpleJsonPtr& ptr)
    : m_ptr(ptr)
{}

SimpleJsonToken::SimpleJsonToken(std::nullptr_t)
{}

SimpleJsonToken::~SimpleJsonToken()
{}

SimpleJsonToken SimpleJsonToken::operator[](std::string_view key) const
{
    if (!m_ptr || key.empty())
        return SimpleJsonToken();

    if (m_ptr->type() != SimpleJsonValueType::Object)
        return SimpleJsonToken();

    return m_ptr->get(key);
}

SimpleJsonToken SimpleJsonToken::operator[](const char* key) const
{
	if (!m_ptr || !key || !*key)
		return SimpleJsonToken();

	if (m_ptr->type() != SimpleJsonValueType::Object)
		return SimpleJsonToken();

	return m_ptr->get(key);
}

SimpleJsonToken SimpleJsonToken::operator[](int index) const
{
	if (!m_ptr || index < 0)
		return SimpleJsonToken();

	if (m_ptr->type() != SimpleJsonValueType::Array)
		return SimpleJsonToken();

	if (index >= (int)m_ptr->values().size())
		return SimpleJsonToken();

    return m_ptr->values()[index];
}

static std::string TheEmptyString;

const std::string& SimpleJsonToken::str() const
{
    if (!m_ptr)
        return TheEmptyString;

    if (m_ptr->type() != SimpleJsonValueType::Value)
        return TheEmptyString;

    return m_ptr->value();
}

std::vector<SimpleJsonToken> SimpleJsonToken::values() const
{
    std::vector<SimpleJsonToken> ret;

    if (m_ptr && m_ptr->type() == SimpleJsonValueType::Array)
        for (const auto& val : m_ptr->values())
            ret.push_back(val);

    return ret;
}

//--