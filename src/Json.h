/**********************************************************************************
*        File: Json.h
* Description: Minimal JSON writer used by the events and by the web interface.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: Only writes JSON; the inputs of the web interface are form parameters.
**********************************************************************************/

#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace Json
{
  inline std::string Escape(const std::string& text)
  {
    std::string escaped;
    escaped.reserve(text.size() + 2U);
    escaped.push_back('"');

    for (const auto character : text)
    {
      switch (character)
      {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U)
        {
          std::ostringstream code;
          code << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character);
          escaped += code.str();
        }
        else
        {
          escaped.push_back(character);
        }
      }
    }

    escaped.push_back('"');
    return escaped;
  }

  inline std::string Number(const double value, const int decimals = 3)
  {
    if (!std::isfinite(value))
      return "null";

    std::ostringstream text;
    text << std::fixed << std::setprecision(decimals) << value;
    return text.str();
  }

  /**
   * \brief Builds a JSON object or array incrementally: Object().Add("a", 1).Add("b", "x").Str() == {"a":1,"b":"x"}.
   */
  class Writer final
  {
  public:
    static Writer Object() { return Writer('{', '}'); }
    static Writer Array() { return Writer('[', ']'); }

    Writer& Add(const std::string& key, const std::string& value) { return Raw(key, Escape(value)); }
    Writer& Add(const std::string& key, const char* value) { return Raw(key, Escape(value)); }
    Writer& Add(const std::string& key, const bool value) { return Raw(key, value ? "true" : "false"); }
    Writer& Add(const std::string& key, const int value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const unsigned int value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const long value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const unsigned long value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const long long value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const unsigned long long value) { return Raw(key, std::to_string(value)); }
    Writer& Add(const std::string& key, const double value, const int decimals = 3) { return Raw(key, Number(value, decimals)); }
    Writer& Add(const std::string& key, const Writer& value) { return Raw(key, value.Str()); }

    // Adds an already serialized JSON value (object member when 'key' is not empty, array item otherwise).
    Writer& Raw(const std::string& key, const std::string& json)
    {
      if (!m_empty)
        m_text << ',';

      if (m_open == '{')
        m_text << Escape(key) << ':';

      m_text << json;
      m_empty = false;
      return *this;
    }

    // Array items.
    Writer& Push(const std::string& json) { return Raw("", json); }
    Writer& Push(const Writer& value) { return Raw("", value.Str()); }
    Writer& PushString(const std::string& value) { return Raw("", Escape(value)); }
    Writer& PushNumber(const double value, const int decimals = 3) { return Raw("", Number(value, decimals)); }

    std::string Str() const { return m_text.str() + m_close; }

  private:
    Writer(const char open, const char close) : m_open(open), m_close(close) { m_text << open; }

  public:
    Writer(const Writer& other) : m_open(other.m_open), m_close(other.m_close), m_empty(other.m_empty) { m_text << other.m_text.str(); }

  private:
    char m_open;
    char m_close;
    bool m_empty{ true };
    std::ostringstream m_text;
  };
}
