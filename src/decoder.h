#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace rfe {
using Bytes = std::vector<uint8_t>;
struct Result { Bytes data; std::string format; std::string note; };
Result convert(const Bytes& input);
Bytes read(const std::filesystem::path& path);
void saveNew(const std::filesystem::path& path, const Bytes& data);
void saveReplacement(const std::filesystem::path& path, const Bytes& data);
bool samePath(const std::filesystem::path& a, const std::filesystem::path& b);
struct BatchResult { unsigned saved=0, skipped=0, failed=0; };
BatchResult batch(const std::filesystem::path& folder, bool recursive,
                 const std::function<void(const std::wstring&)>& log);
std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& text);
}
