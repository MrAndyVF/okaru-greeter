#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Formats local wall-clock time with the same bare strftime and C++ chrono
// field syntax used by Noctalia (for example "%H:%M" and "{:%H:%M}").
[[nodiscard]] std::string formatLocalTime(std::string_view format);

// Timestamp overload used by deterministic callers and tests.
[[nodiscard]] std::string formatLocalUnixTime(std::int64_t unixSeconds, std::string_view format);
