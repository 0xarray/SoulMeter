#pragma once
#include <string>

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;

typedef std::chrono::system_clock::time_point timePoint;

#define FLOOR(x) (float)((int)x)

inline bool UTF16toUTF8(_In_ wchar_t* src, _Out_ char* dest, _In_ size_t destLen) {

	if (src == nullptr || dest == nullptr || destLen == 0)
		return FALSE;

	if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, (int)destLen, NULL, NULL) <= 0) {
		dest[0] = 0;
		return FALSE;
	}

	return TRUE;
}

// Despite the name this converts UTF-8 to the ANSI code page (for MessageBox text).
inline bool ANSItoUTF8(_In_ char* src, _Out_ char* dest, _In_ int32_t destLen) {

	if (src == nullptr || dest == nullptr)
		return FALSE;

	int len = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
	if (len < 1)
		return FALSE;

	std::wstring wide(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, src, -1, wide.data(), len);

	len = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
	if (len < 1 || len >= destLen)
		return FALSE;

	WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, dest, destLen, NULL, NULL);

	return TRUE;
}

inline bool TextCommma(_In_ char* src, _Out_ char* dest) {

	if (src == nullptr || dest == nullptr) {
		return FALSE;
	}

	size_t len = strlen(src);

	while (*src) {
		*dest++ = *src++;

		if (--len && (len % 3) == 0)
			*dest++ = ',';
	}
	*dest++ = 0;

	return TRUE;
}

inline bool TextCommmaIncludeDecimal(_In_ double src, _In_ size_t destLen, _Out_ char* dest) 
{
	if (dest == nullptr) {
		return FALSE;
	}

	char tmp[128] = { 0 };
	char comma[128] = { 0 };
	double whole = floor(src);
	int decimal = int((src - whole) * 10) % 10;

	sprintf_s(tmp, "%.0f", whole);
	TextCommma(tmp, comma);
	sprintf_s(dest, destLen, "%s.%d", comma, decimal);

	return TRUE;
}

inline uint64_t GetCurrentTimeStamp() {
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline bool file_contents(const std::filesystem::path& path, std::string* str)
{
	if (!std::filesystem::is_regular_file(path))
		return FALSE;

	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file.is_open())
		return FALSE;

	std::string content{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

	file.close();

	*str = std::move(content);

	return TRUE;
}
