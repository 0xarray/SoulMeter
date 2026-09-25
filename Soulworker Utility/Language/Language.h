#pragma once
#include "pch.h"
#include <unordered_map>

#define LANGMANAGER Language::getInstance()

// Transparent hash so GetText can look up a const char* without building a std::string.
struct LangKeyHash {
	using is_transparent = void;
	size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using LangMap = std::unordered_map<std::string, std::string, LangKeyHash, std::equal_to<>>;

class Language : public Singleton<Language>
{

private:
	LangMap _textList;
	char _currentLang[128] = { 0 };
	std::vector<std::string> _notFoundText;

public:
	Language() : _currentLang("zh_tw.json") {}

	const char _langFolder[6] = "Lang/";

	char* GetCurrentLang()
	{
		return _currentLang;
	}
	DWORD SetCurrentLang(char* langFile);
	const std::string_view GetText(const char* text, LangMap* vector = nullptr);
	std::unordered_map<std::string, std::string> GetAllLangFile();
	auto GetLangFile(char* langFile, bool outputERROR = true);
	LangMap MapLangData(char* langFile, bool useReplace = true);
};