#include "common.h"
#include "utils.h"
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string.h>
#include <stdarg.h>
#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

//--

namespace prv
{
    ///---

    template< typename T >
    inline static bool IsFloatNum(uint32_t index, T ch)
    {
        if (ch == '.') return true;
        if (ch >= '0' && ch <= '9') return true;
        if (ch == 'f' && (index > 0)) return true;
        if ((ch == '+' || ch == '-') && (index == 0)) return true;
        return false;
    }

    template< typename T >
    inline static bool IsAlphaNum(T ch)
    {
        if (ch >= '0' && ch <= '9') return true;
        if (ch >= 'A' && ch <= 'Z') return true;
        if (ch >= 'a' && ch <= 'z') return true;
        if (ch == '_') return true;
        return false;
    }

    template< typename T >
    inline static bool IsStringChar(T ch, const T* additionalDelims)
    {
        if (ch <= ' ') return false;
        if (ch == '\"' || ch == '\'') return false;

        if (strchr(additionalDelims, ch))
            return false;

        return true;
    }

    template< typename T >
    inline static bool IsIntNum(uint32_t index, T ch)
    {
        if (ch >= '0' && ch <= '9') return true;
        if ((ch == '+' || ch == '-') && (index == 0)) return true;
        return false;
    }

    template< typename T >
    inline static bool IsHex(T ch)
    {
        if (ch >= '0' && ch <= '9') return true;
        if (ch >= 'A' && ch <= 'F') return true;
        if (ch >= 'a' && ch <= 'f') return true;
        return false;
    }

    template< typename T >
    inline uint64_t GetHexValue(T ch)
    {
        switch (ch)
        {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'a': return 10;
        case 'A': return 10;
        case 'b': return 11;
        case 'B': return 11;
        case 'c': return 12;
        case 'C': return 12;
        case 'd': return 13;
        case 'D': return 13;
        case 'e': return 14;
        case 'E': return 14;
        case 'f': return 15;
        case 'F': return 15;
        }

        return 0;
    }

    static const uint32_t OffsetsFromUTF8[6] =
    {
        0x00000000UL,
        0x00003080UL,
        0x000E2080UL,
        0x03C82080UL,
        0xFA082080UL,
        0x82082080UL
    };

    static const uint8_t TrailingBytesForUTF8[256] =
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
    };

    inline static bool IsUTF8(char c)
    {
        return (c & 0xC0) != 0x80;
    }

    uint32_t NextChar(const char*& ptr, const char* end)
    {
        if (ptr >= end)
            return 0;

        uint32_t ch = 0;
        size_t sz = 0;
        do
        {
            ch <<= 6;
            ch += (uint8_t)*ptr++;
            sz++;
        }
        while (ptr < end && !IsUTF8(*ptr));
        if (sz > 1)
            ch -= OffsetsFromUTF8[sz - 1];
        return ch;
    }

    //--

    static const char Digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E','F' };

    static inline bool GetNumberValueForDigit(char ch, uint32_t base, uint8_t& outDigit)
    {
        for (uint8_t i = 0; i < base; ++i)
        {
            if (Digits[i] == ch)
            {
                outDigit = i;
                return true;
            }
        }

        return false;
    }

    template<typename T>
    static inline bool CheckNumericalOverflow(T val, T valueToAdd)
    {
        if (valueToAdd > 0)
        {
            auto left = std::numeric_limits<T>::max() - val;
            return valueToAdd > left;
        }
        else if (valueToAdd < 0)
        {
            auto left = std::numeric_limits<T>::lowest() - val;
            return valueToAdd < left;
        }

        return false;
    }

    template<typename Ch, typename T>
    inline static bool MatchInteger(const Ch* str, T& outValue, size_t strLength, uint32_t base)
    {
        static_assert(std::is_signed<T>::value || std::is_unsigned<T>::value, "Only integer types are allowed here");

        // empty strings are not valid input to this function
        if (!str || !*str)
            return false;

        // determine start and end of parsing range as well as the sign
        auto negative = (*str == '-');
        auto strStart = (*str == '+' || *str == '-') ? str + 1 : str;
        auto strEnd = str + strLength;

        // unsigned values cannot be negative :)
        if (std::is_unsigned<T>::value && negative)
            return false;

        T minValue = std::numeric_limits<T>::min();
        T maxValue = std::numeric_limits<T>::max();

        T value = 0;
        T mult = negative ? -1 : 1;

        // assemble number
        auto pos = strEnd;
        bool overflowed = false;
        while (pos > strStart)
        {
            auto ch = *(--pos);

            // if a non-zero digit is encountered we must make sure that he mult is not overflowed already
            uint8_t digitValue;
            if (!GetNumberValueForDigit((char)ch, base, digitValue))
                return false;

            // apply
            if (digitValue != 0 && overflowed)
                return false;

            // validate that we will not overflow the type
            auto valueToAdd = (T)(digitValue * mult);
            if ((valueToAdd / mult) != digitValue)
                return false;
            if (prv::CheckNumericalOverflow<T>(value, valueToAdd))
                return false;

            // accumulate
            value += valueToAdd;

            // advance to next multiplier
            T newMult = mult * 10;
            if (newMult / 10 != mult)
                overflowed = true;
            mult = newMult;
        }

        outValue = value;
        return true;
    }

    template<typename Ch>
    inline bool MatchFloat(const Ch* str, double& outValue, size_t strLength)
    {
        // empty strings are not valid input to this function
        if (!str || !*str)
            return false;

        // determine start and end of parsing range as well as the sign
        auto negative = (*str == '-');
        auto strEnd = str + strLength;
        auto strStart = (*str == '+' || *str == '-') ? str + 1 : str;

        // validate that we have a proper characters, discover the decimal point position
        auto strDecimal = strEnd; // if decimal point was not found assume it's at the end
        {
            auto pos = strStart;
            while (pos < strEnd)
            {
                auto ch = *pos++;

                if (pos == strEnd && ch == 'f')
                    break;

                if (ch == '.')
                {
                    strDecimal = pos - 1;
                }
                else
                {
                    uint8_t value = 0;
                    if (!prv::GetNumberValueForDigit((char)ch, 10, value))
                        return false;
                }
            }
        }

        // accumulate values
        double value = 0.0f;

        // TODO: this is tragic where it comes to the precision loss....
        // TODO: overflow/underflow
        {
            double mult = 1.0f;

            auto pos = strDecimal;
            while (pos > strStart)
            {
                auto ch = *(--pos);

                uint8_t digitValue = 0;
                if (!prv::GetNumberValueForDigit((char)ch, 10, digitValue))
                    return false;

                // accumulate
                value += (double)digitValue * mult;
                mult *= 10.0;
            }
        }

        // Fractional part
        if (strDecimal < strEnd)
        {
            double mult = 0.1f;

            auto pos = strDecimal + 1;
            while (pos < strEnd)
            {
                auto ch = *(pos++);

                if (pos == strEnd && ch == 'f')
                    break;

                uint8_t digitValue = 0;
                if (!prv::GetNumberValueForDigit((char)ch, 10, digitValue))
                    return false;

                // accumulate
                value += (double)digitValue * mult;
                mult /= 10.0;
            }
        }

        outValue = negative ? -value : value;
        return true;
    }

} // prv

//--



//--

Parser::Parser(std::string_view txt)
{
    m_start = txt.data();
    m_end = m_start + txt.length();
    m_cur = m_start;
}

bool Parser::testKeyword(std::string_view keyword) const
{
    auto cur = m_cur;
    while (cur < m_end && *cur <= ' ')
        ++cur;

    auto keyLength = keyword.length();
    for (uint32_t i = 0; i < keyLength; ++i)
    {
        if (cur >= m_end || *cur != keyword.data()[i])
            return false;

        cur += 1;
    }

    return true;
}

void Parser::push()
{

}

void Parser::pop()
{

}

bool Parser::parseWhitespaces()
{
    while (m_cur < m_end && *m_cur <= ' ')
    {
        if (*m_cur == '\n')
            m_line += 1;
        m_cur++;
    }

    return m_cur < m_end;
}

bool Parser::parseTillTheEndOfTheLine(std::string_view* outIdent /*= nullptr*/)
{
    const char* firstNonEmptyChar = nullptr;
    const char* lastNonEmptyChar = nullptr;

    while (m_cur < m_end)
    {
        if (*m_cur > ' ')
        {
            if (!firstNonEmptyChar)
                firstNonEmptyChar = m_cur;

            lastNonEmptyChar = m_cur;
        }

        if (*m_cur++ == '\n')
            break;
    }

    if (outIdent)
    {
        if (lastNonEmptyChar != nullptr && m_cur < m_end)
            *outIdent = std::string_view(firstNonEmptyChar, (lastNonEmptyChar + 1) - firstNonEmptyChar);
        else
            *outIdent = std::string_view();
    }

    return m_cur < m_end;
}

bool Parser::parseIdentifier(std::string_view& outIdent)
{
    if (!parseWhitespaces())
        return false;

    if (!(*m_cur == '_' || *m_cur == ':' || std::iswalpha(*m_cur)))
        return false;

    auto identStart = m_cur;
    while (m_cur < m_end && (*m_cur == '_' || *m_cur == ':' || std::iswalnum(*m_cur)))
        m_cur += 1;

    assert(m_cur > identStart);
    outIdent = std::string_view(identStart, m_cur - identStart);
    return true;
}

bool Parser::parseString(std::string_view& outValue, const char* additionalDelims/* = ""*/)
{
    if (!parseWhitespaces())
        return false;

    auto startPos = m_cur;
    auto startLine = m_line;

    if (*m_cur == '\"' || *m_cur == '\'')
    {
        auto quote = *m_cur++;
        auto stringStart = m_cur;
        while (m_cur < m_end && *m_cur != quote)
            if (*m_cur++ == '\n')
                m_line += 1;

        if (m_cur >= m_end)
        {
            m_cur = startPos;
            m_line = startLine;
            return false;
        }

        outValue = std::string_view(stringStart, m_cur - stringStart);
        m_cur += 1;

        return true;
    }
    else
    {
        while (m_cur < m_end && prv::IsStringChar(*m_cur, additionalDelims))
            m_cur += 1;

        outValue = std::string_view(startPos, m_cur - startPos);
        return true;
    }
}

bool Parser::parseStringWithScapement(std::string& outValue, const char* additionalDelims/* = ""*/)
{
	if (!parseWhitespaces())
		return false;

	auto startPos = m_cur;
	auto startLine = m_line;

	if (*m_cur == '\"' || *m_cur == '\'')
	{
		auto quote = *m_cur++;
		auto stringStart = m_cur;

        std::stringstream txt;
        while (m_cur < m_end && *m_cur != quote)
        {
            auto ch = *m_cur++;

            if (ch == '\n')
                m_line += 1;

            if (ch == '\\')
            {
                const auto next = (m_cur < m_end) ? *m_cur++ : 0;
                if (next == 'n')
                    txt << "\n";
                else if (next == 'r')
                    txt << "\r";
                else if (next == 'b')
                    txt << "\b";
				else if (next == 't')
					txt << "\t";
				else if (next == '\"')
					txt << "\"";
				else if (next == '\'')
					txt << "\'";
                else
                    txt << next;
            }
            else
            {
                txt << ch;
            }
        }

		if (m_cur >= m_end)
		{
			m_cur = startPos;
			m_line = startLine;
			return false;
		}

        outValue = txt.str();
		m_cur += 1;

		return true;
	}
	else
	{
		while (m_cur < m_end && prv::IsStringChar(*m_cur, additionalDelims))
			m_cur += 1;

		outValue = std::string_view(startPos, m_cur - startPos);
		return true;
	}
}

bool Parser::parseLine(std::string_view& outValue, const char* additionalDelims/* = ""*/, bool eatLeadingWhitespaces/*= true*/)
{
    while (m_cur < m_end)
    {
        if (*m_cur == '\n')
        {
            m_line += 1;
            m_cur++;
            return false;
        }

        if (*m_cur > ' ' || !eatLeadingWhitespaces)
            break;

        ++m_cur;
    }

    auto startPos = m_cur;
    auto startLine = m_line;

    while (m_cur < m_end)
    {
        if (*m_cur == '\n')
        {
            outValue = std::string_view(startPos, m_cur - startPos);
            m_line += 1;
            m_cur++;
            return true;
        }

        if (strchr(additionalDelims, *m_cur))
        {
            outValue = std::string_view(startPos, m_cur - startPos);
            m_cur++;
            return true;
        }

        ++m_cur;
    }

    if (startPos == m_cur)
        return false;

    outValue = std::string_view(startPos, m_cur - startPos);
    return true;
}

bool Parser::parseKeyword(std::string_view keyword)
{
    if (!parseWhitespaces())
        return false;

    auto keyStart = m_cur;
    auto keyLength = keyword.length();
    for (uint32_t i = 0; i < keyLength; ++i)
    {
        if (m_cur >= m_end || *m_cur != keyword.data()[i])
        {
            m_cur = keyStart;
            return false;
        }

        m_cur += 1;
    }

    return true;
}

bool Parser::parseChar(uint32_t& outChar)
{
    auto* cur = m_cur;
    const auto ch = prv::NextChar(cur, m_end);
    if (ch != 0)
    {
        outChar = ch;
        m_cur = cur;
        return true;
    }

    return false;
}

bool Parser::parseFloat(float& outValue)
{
    double doubleValue;
    if (parseDouble(doubleValue))
    {
        outValue = (float)doubleValue;
        return true;
    }

    return false;
}

bool Parser::parseDouble(double& outValue)
{
    if (!parseWhitespaces())
        return false;

    auto originalPos = m_cur;
    if (*m_cur == '-' || *m_cur == '+')
    {
        m_cur += 1;
    }

    uint32_t numChars = 0;
    while (m_cur < m_end && prv::IsFloatNum(numChars, *m_cur))
    {
        ++numChars;
        m_cur += 1;
    }

    if (numChars && prv::MatchFloat(originalPos, outValue, numChars))
        return true;

    m_cur = originalPos;
    return false;
}

bool Parser::parseBoolean(bool& outValue)
{
    if (parseKeyword("true"))
    {
        outValue = true;
        return true;
    }
    else if (parseKeyword("false"))
    {
        outValue = false;
        return true;
    }

    int64_t numericValue = 0;
    if (parseInt64(numericValue))
    {
        outValue = (numericValue != 0);
        return true;
    }

    return false;
}

bool Parser::parseHex(uint64_t& outValue, uint32_t maxLength /*= 0*/, uint32_t* outValueLength /*= nullptr*/)
{
    if (!parseWhitespaces())
        return false;

    const char* original = m_cur;
    const char* maxEnd = maxLength ? (m_cur + maxLength) : m_end;
    uint64_t ret = 0;
    while (m_cur < m_end && prv::IsHex(*m_cur) && (m_cur < maxEnd))
    {
        ret = (ret << 4) | prv::GetHexValue(*m_cur);
        m_cur += 1;
    }

    if (original == m_cur)
        return false;

    outValue = ret;
    if (outValueLength)
        *outValueLength = (uint32_t)(m_cur - original);
    return true;
}

bool Parser::parseInt8(char& outValue)
{
    auto start = m_cur;

    int64_t bigVal = 0;
    if (!parseInt64(bigVal))
        return false;

    if (bigVal < std::numeric_limits<char>::min() || bigVal > std::numeric_limits<char>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (char)bigVal;
    return true;
}

bool Parser::parseInt16(short& outValue)
{
    auto start = m_cur;

    int64_t bigVal = 0;
    if (!parseInt64(bigVal))
        return false;

    if (bigVal < std::numeric_limits<short>::min() || bigVal > std::numeric_limits<short>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (short)bigVal;
    return true;
}

bool Parser::parseInt32(int& outValue)
{
    auto start = m_cur;

    int64_t bigVal = 0;
    if (!parseInt64(bigVal))
        return false;

    if (bigVal < std::numeric_limits<int>::min() || bigVal > std::numeric_limits<int>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (int)bigVal;
    return true;
}

bool Parser::parseInt64(int64_t& outValue)
{
    if (!parseWhitespaces())
        return false;

    auto originalPos = m_cur;
    if (*m_cur == '-' || *m_cur == '+')
        m_cur += 1;

    uint32_t numChars = 0;
    while (m_cur < m_end && prv::IsIntNum(numChars, *m_cur))
    {
        ++numChars;
        ++m_cur;
    }

    if (numChars && prv::MatchInteger(originalPos, outValue, numChars, 10))
        return true;

    m_cur = originalPos;
    return false;
}

bool Parser::parseUint8(uint8_t& outValue)
{
    auto start = m_cur;

    uint64_t bigVal = 0;
    if (!parseUint64(bigVal))
        return false;

    if (bigVal > std::numeric_limits<uint8_t>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (uint8_t)bigVal;
    return true;
}

bool Parser::parseUint16(uint16_t& outValue)
{
    auto start = m_cur;

    uint64_t bigVal = 0;
    if (!parseUint64(bigVal))
        return false;

    if (bigVal > std::numeric_limits<uint16_t>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (uint16_t)bigVal;
    return true;
}

bool Parser::parseUint32(uint32_t& outValue)
{
    auto start = m_cur;

    uint64_t bigVal = 0;
    if (!parseUint64(bigVal))
        return false;

    if (bigVal > std::numeric_limits<uint32_t>::max())
    {
        m_cur = start;
        return false;
    }

    outValue = (uint32_t)bigVal;
    return true;
}

bool Parser::parseUint64(uint64_t& outValue)
{
    if (!parseWhitespaces())
        return false;

    uint32_t numChars = 0;
    auto originalPos = m_cur;
    while (m_cur < m_end && prv::IsIntNum(numChars, *m_cur))
    {
        ++numChars;
        ++m_cur;
    }

    if (numChars && prv::MatchInteger(originalPos, outValue, numChars, 10))
        return true;

    m_cur = originalPos;
    return false;
}

//--

RequestArgs::RequestArgs()
{}

RequestArgs::~RequestArgs()
{}

RequestArgs& RequestArgs::clear()
{
    m_arguments.clear();
    return *this;
}

RequestArgs& RequestArgs::setText(std::string_view name, std::string_view value)
{
    if (value.empty())
        return remove(name);

    if (!name.empty())
        m_arguments[std::string(name)] = value;    
    return *this;
}

RequestArgs& RequestArgs::remove(std::string_view name)
{
    auto it = m_arguments.find(std::string(name));
    if (it != m_arguments.end())
        m_arguments.erase(it);
    return *this;
}

RequestArgs& RequestArgs::setNumber(std::string_view name, int value)
{
    char txt[64];
    sprintf_s(txt, sizeof(txt), "%d", value);
    return setText(name, txt);
}

RequestArgs& RequestArgs::setBool(std::string_view name, bool value)
{
	return setText(name, value ? "true" : "false");
}

static bool IsSafeURLChar(char ch)
{
    if (ch >= 'a' && ch <= 'z') return true;
    if (ch >= 'A' && ch <= 'Z') return true;
    if (ch >= '0' && ch <= '9') return true;
    if (ch == '_' || ch == '!' || ch == '*' || ch == '(' || ch == ')' || ch == '~' || ch == '.' || ch == '\'') return true;
    return false;
}

static void PrintURL(std::stringstream& f, std::string_view txt)
{
    for (auto ch : txt)
    {
        if (ch == ' ')
            f << "+";
        else if (IsSafeURLChar(ch))
            f << ch;
        else
        {
            char txt[16];
            sprintf_s(txt, sizeof(txt), "%%%02X", (unsigned char)ch);
            f << txt;
        }
    }
}

void RequestArgs::print(std::stringstream& f) const
{
    bool separator = false;

    for (const auto& it : m_arguments)
    {
        f << (separator ? "&" : "?");
        f << it.first;
        f << "=";
        PrintURL(f, it.second);
        separator = true;
    }
}

//--

std::string_view Commandline::get(std::string_view name, std::string_view defaultValue) const
{
	for (const auto& entry : args)
		if (entry.key == name)
			return entry.value;

	return defaultValue;
}

const std::string& Commandline::get(std::string_view name) const
{
    for (const auto& entry : args)
        if (entry.key == name)
            return entry.value;

    static std::string theEmptyString;
    return theEmptyString;
}

const std::vector<std::string>& Commandline::getAll(std::string_view name) const
{
    for (const auto& entry : args)
        if (entry.key == name)
            return entry.values;

    static std::vector<std::string> theEmptyStringArray;
    return theEmptyStringArray;
}

bool Commandline::has(std::string_view name) const
{
    for (const auto& entry : args)
        if (entry.key == name)
            return true;

    return false;
}

bool Commandline::parse(std::string_view text)
{
    Parser parser(text);

    bool parseInitialChar = true;
    while (parser.parseWhitespaces())
    {
        if (parser.parseKeyword("-"))
        {
            parseInitialChar = false;
            break;
        }

        // get the command
        std::string_view commandName;
        if (!parser.parseIdentifier(commandName))
        {
            std::cout << "Commandline parsing error: expecting command name. Application may not work as expected.\n";
            return false;
        }

        commands.push_back(std::string(commandName));
    }

    while (parser.parseWhitespaces())
    {
        if (!parser.parseKeyword("-") && parseInitialChar)
            break;
        parseInitialChar = true;

        std::string_view paramName;
        if (!parser.parseIdentifier(paramName))
        {
            std::cout << "Commandline parsing error: expecting param name after '-'. Application may not work as expected.\n";
            return false;
        }

        std::string_view paramValue;
        if (parser.parseKeyword("="))
        {
            // Read value
            if (!parser.parseString(paramValue))
            {
                std::cout << "Commandline parsing error: expecting param value after '=' for param '" << paramName << "'. Application may not work as expected.\n";
                return false;
            }
        }

        bool exists = false;
        for (auto& param : this->args)
        {
            if (param.key == paramName)
            {
                if (!paramValue.empty())
                {
                    param.values.push_back(std::string(paramValue));
                    param.value = paramValue;
                }

                exists = true;
                break;
            }
        }

        if (!exists)
        {
            Arg arg;
            arg.key = paramName;

            if (!paramValue.empty())
            {
                arg.values.push_back(std::string(paramValue));
                arg.value = paramValue;
            }

            this->args.push_back(arg);
        }
    }

    return true;
}

//--

bool LoadFileToString(const fs::path& path, std::string& outText)
{
    try
    {
        std::ifstream f(path);
        if (!f.is_open())
            return false;

        std::stringstream buffer;
        buffer << f.rdbuf();
        outText = buffer.str();
        return true;
    }
    catch (std::exception& e)
    {
        std::cout << "Error reading file " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool LoadFileToBuffer(const fs::path& path, std::vector<uint8_t>& outBuffer)
{
	try
	{
		std::ifstream file(path, std::ios::binary);
		file.unsetf(std::ios::skipws);

		file.seekg(0, std::ios::end);
		auto fileSize = file.tellg();
		file.seekg(0, std::ios::beg);

        outBuffer.resize(fileSize);
        file.read((char*)outBuffer.data(), fileSize);

		return true;
	}
	catch (std::exception& e)
	{
		std::cout << "Error reading file " << path << ": " << e.what() << "\n";
		return false;
	}
}

bool SaveFileFromString(const fs::path& path, std::string_view txt, bool print /*=true*/, bool force /*= false*/, uint32_t* outCounter, fs::file_time_type customTime /*= fs::file_time_type()*/)
{
    std::string newContent(txt);

    if (!force)
    {
        std::string currentContent;
        if (LoadFileToString(path, currentContent))
        {
            if (currentContent == txt)
            {
                if (customTime != fs::file_time_type())
                {
                    if (print)
                    {
                        const auto currentTimeStamp = fs::last_write_time(path);
                        std::cout << "File " << path << " is the same, updating timestamp only to " << customTime.time_since_epoch().count() << ", current: " << currentTimeStamp.time_since_epoch().count() << "\n";
                    }

                    fs::last_write_time(path, customTime);
                }

                return true;
            }
        }

        if (print)
            std::cout << "File " << path << " has changed and has to be saved\n";
    }

    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }

    try
    {
        std::ofstream file(path);
        file << txt;
    }
    catch (std::exception& e)
    {
        std::cout << "Error writing file " << path << ": " << e.what() << "\n";
        return false;
    }

    if (outCounter)
        (*outCounter) += 1;

    return true;
}

bool SaveFileFromBuffer(const fs::path& path, const std::vector<uint8_t>& buffer, bool print /*=true*/, bool force /*= false*/, uint32_t* outCounter, fs::file_time_type customTime /*= fs::file_time_type()*/)
{
	if (!force)
	{
		std::vector<uint8_t> currentContent;
		if (LoadFileToBuffer(path, currentContent))
		{
			if (currentContent == buffer)
			{
				if (customTime != fs::file_time_type())
				{
					if (print)
					{
						const auto currentTimeStamp = fs::last_write_time(path);
						std::cout << "File " << path << " is the same, updating timestamp only to " << customTime.time_since_epoch().count() << ", current: " << currentTimeStamp.time_since_epoch().count() << "\n";
					}

					fs::last_write_time(path, customTime);
				}

				return true;
			}
		}

		if (print)
			std::cout << "File " << path << " has changed and has to be saved\n";
	}

	{
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
	}

	try
	{
        std::ofstream file(path, std::ios::binary);
        file.write((const char*)buffer.data(), buffer.size());
	}
	catch (std::exception& e)
	{
		std::cout << "Error writing file " << path << ": " << e.what() << "\n";
		return false;
	}

	if (outCounter)
		(*outCounter) += 1;

	return true;
}

//--

void SplitString(std::string_view txt, std::string_view delim, std::vector<std::string_view>& outParts)
{
    size_t prev = 0, pos = 0;
    do
    {
        pos = txt.find(delim, prev);
        if (pos == std::string::npos) 
            pos = txt.length();

        std::string_view token = txt.substr(prev, pos - prev);
        if (!token.empty()) 
            outParts.push_back(token);

        prev = pos + delim.length();
    }
    while (pos < txt.length() && prev < txt.length());
}

bool BeginsWith(std::string_view txt, std::string_view end)
{
    if (txt.length() >= end.length())
        return (0 == txt.compare(0, end.length(), end));
    return false;
}

bool EndsWith(std::string_view txt, std::string_view end)
{
    if (txt.length() >= end.length())
        return (0 == txt.compare(txt.length() - end.length(), end.length(), end));
    return false;
}

std::string_view PartBefore(std::string_view txt, std::string_view end)
{
    auto pos = txt.find(end);
    if (pos != -1)
        return txt.substr(0, pos);
    return "";
}

std::string_view PartBeforeLast(std::string_view txt, std::string_view end)
{
	auto pos = txt.rfind(end);
	if (pos != -1)
		return txt.substr(0, pos);
	return "";
}

static bool NeedsEscapeArgument(std::string_view txt)
{
    for (const auto ch : txt)
    {
        if (ch == ' ') return true;
        if (ch == '\"') return true;
    }

    return false;
}

std::string EscapeArgument(std::string_view txt)
{
    if (!NeedsEscapeArgument(txt))
        return std::string(txt);

    std::stringstream str;
    str << "\"";

    for (const auto ch : txt)
    {
        if (ch == '\"')
            str << "\\\"";
        else
            str << ch;
    }

    str << "\"";

    return str.str();
}

std::string_view Trim(std::string_view txt)
{
    const auto* start = txt.data();
    const auto* end = txt.data() + txt.length() - 1;

    while (start < end)
    {
        if (*start > 32) break;
        ++start;
    }

    while (start < end)
    {
        if (*end > 32) break;
        --end;
    }

    return std::string_view(start, (end - start) + 1);
}

std::pair<std::string_view, std::string_view> SplitIntoKeyValue(std::string_view txt)
{
	auto pos = txt.find("=");
    if (pos == -1)
        return std::make_pair(txt, "");

    const auto key = txt.substr(0, pos);
    const auto value = txt.substr(pos + 1);

    return std::make_pair(key, value);
}

void TokenizeIntoParts(std::string_view txt, std::vector<std::string_view>& outOptions)
{
    outOptions.erase(outOptions.begin(), outOptions.end());

    const auto* ch = txt.data();
    const auto* chEnd = ch + txt.length();
    
    while (ch < chEnd)
    {
        if (*ch <= ' ')
        {
            ++ch;
            continue;
        }

		if (*ch == '#')
			break; // comment

        const auto* start = ch;
        
        char quoteChar = 0;
        if (*start == '\"' || *start == '\'')
            quoteChar = *start++;

        while (ch < chEnd)
        {
            if (quoteChar)
            {
                if (*ch == quoteChar)
                    break;
            }
            else
            {
                if (*ch <= ' ')
                    break;
                if (*ch == '#')
                    break; // comment
            }

            ++ch;
        }

        auto option = std::string_view(start, ch - start);
        outOptions.push_back(option);

        if (quoteChar)
            ch += 1;
    }
}

std::string_view PartAfter(std::string_view txt, std::string_view end)
{
    auto pos = txt.find(end);
    if (pos != -1)
        return txt.substr(pos + end.length());
    return "";
}

std::string_view PartAfterLast(std::string_view txt, std::string_view end, bool fullOnEmpty)
{
    auto pos = txt.rfind(end);
    if (pos != -1)
        return txt.substr(pos + end.length());
    return fullOnEmpty ? txt : "";
}

std::string MakeGenericPath(std::string_view txt)
{
    auto ret = std::string(txt);
    std::replace(ret.begin(), ret.end(), '\\', '/');
    return ret;
}

std::string MakeGenericPathEx(const fs::path& path)
{
    return MakeGenericPath(path.u8string());
}

static bool IsValidSymbolChar(char ch)
{
    if (ch >= 'a' && ch <= 'z') return true;
    if (ch >= 'A' && ch <= 'Z') return true;
    if (ch >= '0' && ch <= '9') return true;
    return false;
}

std::string MakeSymbolName(std::string_view txt)
{
    std::stringstream str;

    bool lastValid = false;
    for (uint8_t ch : txt)
    {
        if (IsValidSymbolChar(ch))
        {
            str << ch;
            lastValid = true;
        }
        else
        {
            if (lastValid)
                str << "_";
            lastValid = false;
        }
    }

    return str.str();
}

std::string ToUpper(std::string_view txt)
{
    std::string ret(txt);
    transform(ret.begin(), ret.end(), ret.begin(), ::toupper);    
    return ret;
}

std::string ToLower(std::string_view txt)
{
	std::string ret(txt);
	transform(ret.begin(), ret.end(), ret.begin(), ::tolower);
	return ret;
}

std::string ReplaceAll(std::string_view txt, std::string_view from, std::string_view replacement)
{
	if (txt.empty())
		return "";

    std::string str(txt);
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
		str.replace(start_pos, from.length(), replacement);
		start_pos += replacement.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}

    return str;
}

void writeln(std::stringstream& s, std::string_view txt)
{
    s << txt;
    s << "\n";
}

void writelnf(std::stringstream& s, const char* txt, ...)
{
    char buffer[8192];
    va_list args;
    va_start(args, txt);
    vsprintf_s(buffer, sizeof(buffer), txt, args);
    va_end(args);

    s << buffer;
    s << "\n";
}

std::string GuidFromText(std::string_view txt)
{
    union {        
        struct {
            uint32_t Data1;
            uint16_t Data2;
            uint16_t Data3;
            uint8_t Data4[8];
        } g;

        uint32_t data[4];
    } guid;

    guid.data[0] = (uint32_t)std::hash<std::string_view>()(txt);
    guid.data[1] = (uint32_t)std::hash<std::string>()("part1_" + std::string(txt));
    guid.data[2] = (uint32_t)std::hash<std::string>()("part2_" + std::string(txt));
    guid.data[3] = (uint32_t)std::hash<std::string>()("part3_" + std::string(txt));

    // 2150E333-8FDC-42A3-9474-1A3956D46DE8

    char str[128];
    sprintf_s(str, sizeof(str), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid.g.Data1, guid.g.Data2, guid.g.Data3,
        guid.g.Data4[0], guid.g.Data4[1], guid.g.Data4[2], guid.g.Data4[3],
        guid.g.Data4[4], guid.g.Data4[5], guid.g.Data4[6], guid.g.Data4[7]);

    return str;
}

//--

#define MATCH(_txt, _val) if (txt == _txt) { outType = _val; return true; }

template< typename T >
bool ParseEnumValue(std::string_view txt, T& outType)
{
    for (int i = 0; i < (int)(T::MAX); ++i)
    {
        const auto valueName = NameEnumOption((T)i);
        if (!valueName.empty() && valueName == txt)
        {
            outType = (T)i;
            return true;
        }
    }

    return false;
}

std::string_view DefaultPlatformStr()
{
#ifdef _WIN32
	return "windows";
#else
    return "linux";
#endif
}

PlatformType DefaultPlatform()
{
#ifdef _WIN32
	return PlatformType::Windows;
#else
    return PlatformType::Linux;
#endif
}

bool ParseConfigurationType(std::string_view txt, ConfigurationType& outType)
{
    return ParseEnumValue(txt, outType);
}

bool ParseBuildType(std::string_view txt, BuildType& outType)
{
    return ParseEnumValue(txt, outType);
}

bool ParseLibraryType(std::string_view txt, LibraryType& outType)
{
    return ParseEnumValue(txt, outType);
}

bool ParsePlatformType(std::string_view txt, PlatformType& outType)
{
    return ParseEnumValue(txt, outType);
}

bool ParseGeneratorType(std::string_view txt, GeneratorType& outType)
{
    return ParseEnumValue(txt, outType);
}


//--

std::string_view NameEnumOption(ConfigurationType type)
{
    switch (type)
    {
    case ConfigurationType::Checked: return "checked";
    case ConfigurationType::Debug: return "debug";
    case ConfigurationType::Release: return "release";
    case ConfigurationType::Final: return "final";
    }
    return "";
}

std::string_view NameEnumOption(BuildType type)
{
    switch (type)
    {
    case BuildType::Development: return "dev";
    //case BuildType::Standalone: return "standalone";
    case BuildType::Shipment: return "ship";
    }
    return "";
}

std::string_view NameEnumOption(LibraryType type)
{
    switch (type)
    {
    case LibraryType::Shared: return "shared";
    case LibraryType::Static: return "static";
    }
    return "";
}

std::string_view NameEnumOption(PlatformType type)
{
    switch (type)
    {
    case PlatformType::Linux: return "linux";
    case PlatformType::Windows: return "windows";
    case PlatformType::UWP: return "uwp";
    case PlatformType::Scarlett: return "scarlett";
    case PlatformType::Prospero: return "prospero";
    case PlatformType::iOS: return "ios";
    case PlatformType::Android: return "android";
    }
    return "";
}

std::string_view NameEnumOption(GeneratorType type)
{
    switch (type)
    {
    case GeneratorType::VisualStudio19: return "vs2019"; 
    case GeneratorType::VisualStudio22: return "vs2022";
    case GeneratorType::CMake: return "cmake";
    }
    return "";
}

//--

bool IsFileSourceNewer(const fs::path& source, const fs::path& target)
{
    try
    {
        if (!fs::is_regular_file(source))
            return false;

        if (!fs::is_regular_file(target))
            return true;

        auto sourceTimestamp = fs::last_write_time(source);
        auto targetTimestamp = fs::last_write_time(target);
        return sourceTimestamp > targetTimestamp;
    }
    catch (std::exception & e)
    {
        std::cout << "Failed to check file write time: " << e.what() << "\n";
        return false;
    }    
}

bool CreateDirectories(const fs::path& path)
{
    if (!fs::is_directory(path))
    {
		try
		{
            fs::create_directories(path);
		}
		catch (std::exception& e)
		{
			std::cout << "Failed to create directories: " << e.what() << "\n";
			return false;
		}
    }

    return true;
}

bool CopyNewerFile(const fs::path& source, const fs::path& target, bool* outActuallyCopied/*= nullptr*/)
{
    try
    {
        if (!fs::is_regular_file(source))
            return false;

        if (fs::is_regular_file(target))
        {
            auto sourceTimestamp = fs::last_write_time(source);
            auto targetTimestamp = fs::last_write_time(target);
            if (targetTimestamp >= sourceTimestamp)
            {
                if (outActuallyCopied)
                    *outActuallyCopied = false;
                return true;
            }
        }

        std::cout << "Copying " << target << "\n";
        fs::remove(target);
        fs::create_directories(target.parent_path());
        fs::copy(source, target);

        if (outActuallyCopied)
            *outActuallyCopied = true;

        return true;
    }
    catch (std::exception & e)
    {
        std::cerr << KRED << "[BREAKING] Failed to copy file: " << e.what() << "\n" << RST;
        return false;
    }
}

bool CopyNewerFilesRecursive(const fs::path& sourceDir, const fs::path& targetDir, uint32_t* outActuallyCopied)
{
	try
	{
		if (!fs::is_directory(sourceDir))
			return false;

        if (!fs::is_directory(targetDir))
        {
            std::error_code ec;
            if (!fs::create_directories(targetDir, ec))
            {
                std::cerr << KRED << "[BREAKING] Failed to create directory: " << targetDir << ": " << ec << "\n" << RST;
                return false;
            }
        }

        for (const auto& entry : fs::directory_iterator(sourceDir))
        {
            const auto name = entry.path().filename().u8string();
            const auto targetChildName = targetDir / name;

            if (entry.is_directory())
            {                
                if (!CopyNewerFilesRecursive(entry.path(), targetChildName, outActuallyCopied))
                    return false;
            }
            else if (entry.is_regular_file())
            {
                bool copied = false;
                if (!CopyNewerFile(entry.path(), targetChildName, &copied))
                    return false;

                if (copied && outActuallyCopied)
                    *outActuallyCopied += 1;
            }
        }

		return true;
	}
	catch (std::exception& e)
	{
		std::cerr << KRED << "[BREAKING] Failed to copy directories: " << e.what() << "\n" << RST;
		return false;
	}
}


//--

int GetWeek(struct tm* date)
{
	if (NULL == date)
		return 0; // or -1 or throw exception

	if (::mktime(date) < 0) // Make sure _USE_32BIT_TIME_T is NOT defined.
		return 0; // or -1 or throw exception

	// The basic calculation:
	// {Day of Year (1 to 366) + 10 - Day of Week (Mon = 1 to Sun = 7)} / 7
	int monToSun = (date->tm_wday == 0) ? 7 : date->tm_wday; // Adjust zero indexed week day
	int week = ((date->tm_yday + 11 - monToSun) / 7); // Add 11 because yday is 0 to 365.

	// Now deal with special cases:
	// A) If calculated week is zero, then it is part of the last week of the previous year.
	if (week == 0)
	{
		// We need to find out if there are 53 weeks in previous year.
		// Unfortunately to do so we have to call mktime again to get the information we require.
		// Here we can use a slight cheat - reuse this function!
		// (This won't end up in a loop, because there's no way week will be zero again with these values).
		tm lastDay = { 0 };
		lastDay.tm_mday = 31;
		lastDay.tm_mon = 11;
		lastDay.tm_year = date->tm_year - 1;
		// We set time to sometime during the day (midday seems to make sense)
		// so that we don't get problems with daylight saving time.
		lastDay.tm_hour = 12;
		week = GetWeek(&lastDay);
	}

	// B) If calculated week is 53, then we need to determine if there really are 53 weeks in current year
	//    or if this is actually week one of the next year.
	else if (week == 53)
	{
		// We need to find out if there really are 53 weeks in this year,
		// There must be 53 weeks in the year if:
		// a) it ends on Thurs (year also starts on Thurs, or Wed on leap year).
		// b) it ends on Friday and starts on Thurs (a leap year).
		// In order not to call mktime again, we can work this out from what we already know!
		int lastDay = date->tm_wday + 31 - date->tm_mday;
		if (lastDay == 5) // Last day of the year is Friday
		{
			// How many days in the year?
			int daysInYear = date->tm_yday + 32 - date->tm_mday; // add 32 because yday is 0 to 365
			if (daysInYear < 366)
			{
				// If 365 days in year, then the year started on Friday
				// so there are only 52 weeks, and this is week one of next year.
				week = 1;
			}
		}
		else if (lastDay != 4) // Last day is NOT Thursday
		{
			// This must be the first week of next year
			week = 1;
		}
		// Otherwise we really have 53 weeks!
	}

	return week;
}

int GetWeek
(          // Valid values:
	int day,   // 1 to 31
	int month, // 1 to 12.  1 = Jan.
	int year   // 1970 to 3000
)
{
	tm date = { 0 };
	date.tm_mday = day;
	date.tm_mon = month - 1;
	date.tm_year = year - 1900;
	// We set time to sometime during the day (midday seems to make sense)
	// so that we don't get problems with daylight saving time.
	date.tm_hour = 12;
	return GetWeek(&date);
}

int GetCurrentWeek()
{
	tm today;
	time_t now;
	time(&now);
	errno_t error = ::localtime_s(&today, &now);
	return GetWeek(&today);
}

std::string GetCurrentWeeklyTimestamp()
{
	time_t now;
	time(&now);

	tm today;
	::localtime_s(&today, &now);

    int week = GetWeek(&today);
    int year = today.tm_year % 100;

    char txt[64];
    sprintf_s(txt, sizeof(txt), "%02d%02d", year, week);
    return txt;    
}

//--

static const uint64_t crc64_tab[256] = {
	UINT64_C(0x0000000000000000), UINT64_C(0x7ad870c830358979),
	UINT64_C(0xf5b0e190606b12f2), UINT64_C(0x8f689158505e9b8b),
	UINT64_C(0xc038e5739841b68f), UINT64_C(0xbae095bba8743ff6),
	UINT64_C(0x358804e3f82aa47d), UINT64_C(0x4f50742bc81f2d04),
	UINT64_C(0xab28ecb46814fe75), UINT64_C(0xd1f09c7c5821770c),
	UINT64_C(0x5e980d24087fec87), UINT64_C(0x24407dec384a65fe),
	UINT64_C(0x6b1009c7f05548fa), UINT64_C(0x11c8790fc060c183),
	UINT64_C(0x9ea0e857903e5a08), UINT64_C(0xe478989fa00bd371),
	UINT64_C(0x7d08ff3b88be6f81), UINT64_C(0x07d08ff3b88be6f8),
	UINT64_C(0x88b81eabe8d57d73), UINT64_C(0xf2606e63d8e0f40a),
	UINT64_C(0xbd301a4810ffd90e), UINT64_C(0xc7e86a8020ca5077),
	UINT64_C(0x4880fbd87094cbfc), UINT64_C(0x32588b1040a14285),
	UINT64_C(0xd620138fe0aa91f4), UINT64_C(0xacf86347d09f188d),
	UINT64_C(0x2390f21f80c18306), UINT64_C(0x594882d7b0f40a7f),
	UINT64_C(0x1618f6fc78eb277b), UINT64_C(0x6cc0863448deae02),
	UINT64_C(0xe3a8176c18803589), UINT64_C(0x997067a428b5bcf0),
	UINT64_C(0xfa11fe77117cdf02), UINT64_C(0x80c98ebf2149567b),
	UINT64_C(0x0fa11fe77117cdf0), UINT64_C(0x75796f2f41224489),
	UINT64_C(0x3a291b04893d698d), UINT64_C(0x40f16bccb908e0f4),
	UINT64_C(0xcf99fa94e9567b7f), UINT64_C(0xb5418a5cd963f206),
	UINT64_C(0x513912c379682177), UINT64_C(0x2be1620b495da80e),
	UINT64_C(0xa489f35319033385), UINT64_C(0xde51839b2936bafc),
	UINT64_C(0x9101f7b0e12997f8), UINT64_C(0xebd98778d11c1e81),
	UINT64_C(0x64b116208142850a), UINT64_C(0x1e6966e8b1770c73),
	UINT64_C(0x8719014c99c2b083), UINT64_C(0xfdc17184a9f739fa),
	UINT64_C(0x72a9e0dcf9a9a271), UINT64_C(0x08719014c99c2b08),
	UINT64_C(0x4721e43f0183060c), UINT64_C(0x3df994f731b68f75),
	UINT64_C(0xb29105af61e814fe), UINT64_C(0xc849756751dd9d87),
	UINT64_C(0x2c31edf8f1d64ef6), UINT64_C(0x56e99d30c1e3c78f),
	UINT64_C(0xd9810c6891bd5c04), UINT64_C(0xa3597ca0a188d57d),
	UINT64_C(0xec09088b6997f879), UINT64_C(0x96d1784359a27100),
	UINT64_C(0x19b9e91b09fcea8b), UINT64_C(0x636199d339c963f2),
	UINT64_C(0xdf7adabd7a6e2d6f), UINT64_C(0xa5a2aa754a5ba416),
	UINT64_C(0x2aca3b2d1a053f9d), UINT64_C(0x50124be52a30b6e4),
	UINT64_C(0x1f423fcee22f9be0), UINT64_C(0x659a4f06d21a1299),
	UINT64_C(0xeaf2de5e82448912), UINT64_C(0x902aae96b271006b),
	UINT64_C(0x74523609127ad31a), UINT64_C(0x0e8a46c1224f5a63),
	UINT64_C(0x81e2d7997211c1e8), UINT64_C(0xfb3aa75142244891),
	UINT64_C(0xb46ad37a8a3b6595), UINT64_C(0xceb2a3b2ba0eecec),
	UINT64_C(0x41da32eaea507767), UINT64_C(0x3b024222da65fe1e),
	UINT64_C(0xa2722586f2d042ee), UINT64_C(0xd8aa554ec2e5cb97),
	UINT64_C(0x57c2c41692bb501c), UINT64_C(0x2d1ab4dea28ed965),
	UINT64_C(0x624ac0f56a91f461), UINT64_C(0x1892b03d5aa47d18),
	UINT64_C(0x97fa21650afae693), UINT64_C(0xed2251ad3acf6fea),
	UINT64_C(0x095ac9329ac4bc9b), UINT64_C(0x7382b9faaaf135e2),
	UINT64_C(0xfcea28a2faafae69), UINT64_C(0x8632586aca9a2710),
	UINT64_C(0xc9622c4102850a14), UINT64_C(0xb3ba5c8932b0836d),
	UINT64_C(0x3cd2cdd162ee18e6), UINT64_C(0x460abd1952db919f),
	UINT64_C(0x256b24ca6b12f26d), UINT64_C(0x5fb354025b277b14),
	UINT64_C(0xd0dbc55a0b79e09f), UINT64_C(0xaa03b5923b4c69e6),
	UINT64_C(0xe553c1b9f35344e2), UINT64_C(0x9f8bb171c366cd9b),
	UINT64_C(0x10e3202993385610), UINT64_C(0x6a3b50e1a30ddf69),
	UINT64_C(0x8e43c87e03060c18), UINT64_C(0xf49bb8b633338561),
	UINT64_C(0x7bf329ee636d1eea), UINT64_C(0x012b592653589793),
	UINT64_C(0x4e7b2d0d9b47ba97), UINT64_C(0x34a35dc5ab7233ee),
	UINT64_C(0xbbcbcc9dfb2ca865), UINT64_C(0xc113bc55cb19211c),
	UINT64_C(0x5863dbf1e3ac9dec), UINT64_C(0x22bbab39d3991495),
	UINT64_C(0xadd33a6183c78f1e), UINT64_C(0xd70b4aa9b3f20667),
	UINT64_C(0x985b3e827bed2b63), UINT64_C(0xe2834e4a4bd8a21a),
	UINT64_C(0x6debdf121b863991), UINT64_C(0x1733afda2bb3b0e8),
	UINT64_C(0xf34b37458bb86399), UINT64_C(0x8993478dbb8deae0),
	UINT64_C(0x06fbd6d5ebd3716b), UINT64_C(0x7c23a61ddbe6f812),
	UINT64_C(0x3373d23613f9d516), UINT64_C(0x49aba2fe23cc5c6f),
	UINT64_C(0xc6c333a67392c7e4), UINT64_C(0xbc1b436e43a74e9d),
	UINT64_C(0x95ac9329ac4bc9b5), UINT64_C(0xef74e3e19c7e40cc),
	UINT64_C(0x601c72b9cc20db47), UINT64_C(0x1ac40271fc15523e),
	UINT64_C(0x5594765a340a7f3a), UINT64_C(0x2f4c0692043ff643),
	UINT64_C(0xa02497ca54616dc8), UINT64_C(0xdafce7026454e4b1),
	UINT64_C(0x3e847f9dc45f37c0), UINT64_C(0x445c0f55f46abeb9),
	UINT64_C(0xcb349e0da4342532), UINT64_C(0xb1eceec59401ac4b),
	UINT64_C(0xfebc9aee5c1e814f), UINT64_C(0x8464ea266c2b0836),
	UINT64_C(0x0b0c7b7e3c7593bd), UINT64_C(0x71d40bb60c401ac4),
	UINT64_C(0xe8a46c1224f5a634), UINT64_C(0x927c1cda14c02f4d),
	UINT64_C(0x1d148d82449eb4c6), UINT64_C(0x67ccfd4a74ab3dbf),
	UINT64_C(0x289c8961bcb410bb), UINT64_C(0x5244f9a98c8199c2),
	UINT64_C(0xdd2c68f1dcdf0249), UINT64_C(0xa7f41839ecea8b30),
	UINT64_C(0x438c80a64ce15841), UINT64_C(0x3954f06e7cd4d138),
	UINT64_C(0xb63c61362c8a4ab3), UINT64_C(0xcce411fe1cbfc3ca),
	UINT64_C(0x83b465d5d4a0eece), UINT64_C(0xf96c151de49567b7),
	UINT64_C(0x76048445b4cbfc3c), UINT64_C(0x0cdcf48d84fe7545),
	UINT64_C(0x6fbd6d5ebd3716b7), UINT64_C(0x15651d968d029fce),
	UINT64_C(0x9a0d8ccedd5c0445), UINT64_C(0xe0d5fc06ed698d3c),
	UINT64_C(0xaf85882d2576a038), UINT64_C(0xd55df8e515432941),
	UINT64_C(0x5a3569bd451db2ca), UINT64_C(0x20ed197575283bb3),
	UINT64_C(0xc49581ead523e8c2), UINT64_C(0xbe4df122e51661bb),
	UINT64_C(0x3125607ab548fa30), UINT64_C(0x4bfd10b2857d7349),
	UINT64_C(0x04ad64994d625e4d), UINT64_C(0x7e7514517d57d734),
	UINT64_C(0xf11d85092d094cbf), UINT64_C(0x8bc5f5c11d3cc5c6),
	UINT64_C(0x12b5926535897936), UINT64_C(0x686de2ad05bcf04f),
	UINT64_C(0xe70573f555e26bc4), UINT64_C(0x9ddd033d65d7e2bd),
	UINT64_C(0xd28d7716adc8cfb9), UINT64_C(0xa85507de9dfd46c0),
	UINT64_C(0x273d9686cda3dd4b), UINT64_C(0x5de5e64efd965432),
	UINT64_C(0xb99d7ed15d9d8743), UINT64_C(0xc3450e196da80e3a),
	UINT64_C(0x4c2d9f413df695b1), UINT64_C(0x36f5ef890dc31cc8),
	UINT64_C(0x79a59ba2c5dc31cc), UINT64_C(0x037deb6af5e9b8b5),
	UINT64_C(0x8c157a32a5b7233e), UINT64_C(0xf6cd0afa9582aa47),
	UINT64_C(0x4ad64994d625e4da), UINT64_C(0x300e395ce6106da3),
	UINT64_C(0xbf66a804b64ef628), UINT64_C(0xc5bed8cc867b7f51),
	UINT64_C(0x8aeeace74e645255), UINT64_C(0xf036dc2f7e51db2c),
	UINT64_C(0x7f5e4d772e0f40a7), UINT64_C(0x05863dbf1e3ac9de),
	UINT64_C(0xe1fea520be311aaf), UINT64_C(0x9b26d5e88e0493d6),
	UINT64_C(0x144e44b0de5a085d), UINT64_C(0x6e963478ee6f8124),
	UINT64_C(0x21c640532670ac20), UINT64_C(0x5b1e309b16452559),
	UINT64_C(0xd476a1c3461bbed2), UINT64_C(0xaeaed10b762e37ab),
	UINT64_C(0x37deb6af5e9b8b5b), UINT64_C(0x4d06c6676eae0222),
	UINT64_C(0xc26e573f3ef099a9), UINT64_C(0xb8b627f70ec510d0),
	UINT64_C(0xf7e653dcc6da3dd4), UINT64_C(0x8d3e2314f6efb4ad),
	UINT64_C(0x0256b24ca6b12f26), UINT64_C(0x788ec2849684a65f),
	UINT64_C(0x9cf65a1b368f752e), UINT64_C(0xe62e2ad306bafc57),
	UINT64_C(0x6946bb8b56e467dc), UINT64_C(0x139ecb4366d1eea5),
	UINT64_C(0x5ccebf68aecec3a1), UINT64_C(0x2616cfa09efb4ad8),
	UINT64_C(0xa97e5ef8cea5d153), UINT64_C(0xd3a62e30fe90582a),
	UINT64_C(0xb0c7b7e3c7593bd8), UINT64_C(0xca1fc72bf76cb2a1),
	UINT64_C(0x45775673a732292a), UINT64_C(0x3faf26bb9707a053),
	UINT64_C(0x70ff52905f188d57), UINT64_C(0x0a2722586f2d042e),
	UINT64_C(0x854fb3003f739fa5), UINT64_C(0xff97c3c80f4616dc),
	UINT64_C(0x1bef5b57af4dc5ad), UINT64_C(0x61372b9f9f784cd4),
	UINT64_C(0xee5fbac7cf26d75f), UINT64_C(0x9487ca0fff135e26),
	UINT64_C(0xdbd7be24370c7322), UINT64_C(0xa10fceec0739fa5b),
	UINT64_C(0x2e675fb4576761d0), UINT64_C(0x54bf2f7c6752e8a9),
	UINT64_C(0xcdcf48d84fe75459), UINT64_C(0xb71738107fd2dd20),
	UINT64_C(0x387fa9482f8c46ab), UINT64_C(0x42a7d9801fb9cfd2),
	UINT64_C(0x0df7adabd7a6e2d6), UINT64_C(0x772fdd63e7936baf),
	UINT64_C(0xf8474c3bb7cdf024), UINT64_C(0x829f3cf387f8795d),
	UINT64_C(0x66e7a46c27f3aa2c), UINT64_C(0x1c3fd4a417c62355),
	UINT64_C(0x935745fc4798b8de), UINT64_C(0xe98f353477ad31a7),
	UINT64_C(0xa6df411fbfb21ca3), UINT64_C(0xdc0731d78f8795da),
	UINT64_C(0x536fa08fdfd90e51), UINT64_C(0x29b7d047efec8728),
};

uint64_t Crc64(uint64_t crc, const uint8_t* s, uint64_t l)
{
	uint64_t j;

	for (j = 0; j < l; j++) {
		uint8_t byte = s[j];
		crc = crc64_tab[(uint8_t)crc ^ byte] ^ (crc >> 8);
	}
	return crc;
}

uint64_t Crc64(const uint8_t* s, uint64_t l)
{
    return Crc64(0, s, l);
}

//--

bool CompressLZ4(const std::vector<uint8_t>& uncompressedData, std::vector<uint8_t>& outBuffer)
{
    return CompressLZ4(uncompressedData.data(), (uint32_t)uncompressedData.size(), outBuffer);
}

bool CompressLZ4(const void* data, uint32_t size, std::vector<uint8_t>& outBuffer)
{
    if (size == 0 || !data)
    {
        outBuffer.resize(0);
        return true;
    }

    const auto maxSize = LZ4_compressBound(size);
    outBuffer.resize(maxSize);

    int compressedSize = LZ4_compress_HC((const char*)data, (char*)outBuffer.data(), size, maxSize, LZ4HC_CLEVEL_MAX);
    if (!compressedSize)
    {
        std::cerr << KRED << "[BREAKING] Compression failed for buffer of size " << size << "\n" << RST;
        return false;
    }

    std::cout << "Compressed " << size << "->" << compressedSize << "\n";
    outBuffer.resize(compressedSize);

    return true;
}

bool DecompressLZ4(const std::vector<uint8_t>& compresedData, std::vector<uint8_t>& outBuffer)
{
    return DecompressLZ4(compresedData.data(), (uint32_t)compresedData.size(), outBuffer);
}

bool DecompressLZ4(const void* data, uint32_t size, std::vector<uint8_t>& outBuffer)
{
    if (!data || !size)
    {
        outBuffer.resize(0);
        return true;
    }

    int decompressedSize = LZ4_decompress_safe((const char*)data, (char*)outBuffer.data(), size, outBuffer.size());
	if (!decompressedSize)
	{
		std::cerr << KRED << "[BREAKING] Decompression failed for buffer of size " << size << "\n" << RST;
		return false;
	}

    outBuffer.resize(decompressedSize);
    return true;
}

//--