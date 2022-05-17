#pragma once

#include "xml/rapidxml.hpp"
#include <functional>

//--

typedef rapidxml::xml_document<char> XMLDoc;
typedef rapidxml::xml_node<char> XMLNode;

static inline std::string_view XMLNodeTag(const XMLNode* node)
{
	return node ? node->name() : "";
}

static inline std::string_view XMLNodeValue(const XMLNode* node)
{
	return node ? node->value() : "";
}

static inline bool XMLNodeValueBool(const XMLNode* node, bool defaultValue = false)
{
	if (node)
	{
		if (0 == _stricmp("true", node->value()))
			return true;
		else if (0 == _stricmp("false", node->value()))
			return false;
	}

	return defaultValue;
}

static inline int XMLNodeValueInt(const XMLNode* node, int defaultValue = 0)
{
	if (node)
		return atoi(node->value());

	return defaultValue;
}

static inline bool XMLNodeValueIntSafe(const XMLNode* node, int* outValue)
{
	if (node)
	{
		int val = 0;
		if (1 == sscanf_s(node->value(), "%d", &val))
		{
			*outValue = val;
			return true;
		}
	}

	return false;
}

static inline std::string_view XMLNodeAttrbiute(const XMLNode* node, std::string_view name, std::string_view defaultValue = "")
{
	if (node)
	{
		const auto* attr = node->first_attribute();
		while (attr)
		{
			if (name == attr->name())
				return attr->value();

			attr = attr->next_attribute();
		}
	}

	return defaultValue;
}

static inline void XMLNodeIterate(const XMLNode* node, std::string_view name, std::function<void(const XMLNode*)> func)
{
	if (node)
	{
		const auto* child = node->first_node();
		while (child)
		{
			if (name == child->name())
				func(child);

			child = child->next_sibling();
		}
	}
}

static inline void XMLNodeIterate(const XMLNode* node, std::function<void(const XMLNode*, std::string_view)> func)
{
	if (node)
	{
		const auto* child = node->first_node();
		while (child)
		{
			func(child, child->name());
			child = child->next_sibling();
		}
	}
}

//--