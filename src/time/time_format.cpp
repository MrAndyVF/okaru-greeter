#include "time/time_format.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <locale>
#include <optional>
#include <string>
#include <string_view>

namespace {

  [[nodiscard]] std::string normalizeFormatEscapes(std::string_view format) {
    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size(); ++i) {
      if (format[i] == '\\' && i + 1 < format.size() && format[i + 1] == 'n') {
        out.push_back('\n');
        ++i;
      } else {
        out.push_back(format[i]);
      }
    }
    return out;
  }

  [[nodiscard]] bool shouldUseStrftimeCompat(std::string_view format) {
    const auto has = [format](const std::string_view token) { return format.find(token) != std::string_view::npos; };
    return has("%-") || (has("%") && (!has("{") || has("{:")));
  }

  // musl resolves %Z from its own timezone state and ignores tm_zone.
  [[nodiscard]] std::string substituteTimezoneAbbreviation(std::string_view format, const char* abbreviation) {
    if (format.find("%Z") == std::string_view::npos || abbreviation == nullptr) {
      return std::string(format);
    }

    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] != '%' || i + 1 >= format.size()) {
        out.push_back(format[i++]);
      } else if (format[i + 1] == '%') {
        out.append("%%");
        i += 2;
      } else if (format[i + 1] == 'Z') {
        for (const char* c = abbreviation; *c != '\0'; ++c) {
          if (*c == '%') {
            out.push_back('%');
          }
          out.push_back(*c);
        }
        i += 2;
      } else {
        out.push_back(format[i++]);
      }
    }
    return out;
  }

  [[nodiscard]] std::string formatStrftimeRaw(std::string_view format, const std::tm& local) {
    const std::string spec = substituteTimezoneAbbreviation(format, local.tm_zone);
    std::size_t size = std::max<std::size_t>(64, spec.size() * 4 + 16);
    for (int attempt = 0; attempt < 6; ++attempt) {
      std::string buffer(size, '\0');
      const std::size_t written = std::strftime(buffer.data(), buffer.size(), spec.c_str(), &local);
      if (written > 0 || spec.empty()) {
        buffer.resize(written);
        return buffer;
      }
      size *= 2;
    }
    return {};
  }

  [[nodiscard]] std::string
  formatStrftime(std::string_view format, const std::tm& local, const std::optional<std::int64_t> unixSeconds) {
    if (!unixSeconds.has_value() || format.find("%s") == std::string_view::npos) {
      return formatStrftimeRaw(format, local);
    }

    std::string out;
    std::string chunk;
    out.reserve(format.size() + 16);
    chunk.reserve(format.size());
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] != '%' || i + 1 >= format.size()) {
        chunk.push_back(format[i++]);
      } else if (format[i + 1] == '%') {
        chunk.append("%%");
        i += 2;
      } else if (format[i + 1] == 's') {
        out += formatStrftimeRaw(chunk, local);
        chunk.clear();
        out += std::to_string(*unixSeconds);
        i += 2;
      } else {
        chunk.push_back(format[i++]);
      }
    }
    out += formatStrftimeRaw(chunk, local);
    return out;
  }

  [[nodiscard]] std::optional<std::string>
  formatStrftimeCompat(std::string_view format, const std::tm& local, const std::optional<std::int64_t> unixSeconds) {
    if (!shouldUseStrftimeCompat(format)) {
      return std::nullopt;
    }
    if (format.find('{') == std::string_view::npos) {
      return formatStrftime(format, local, unixSeconds);
    }

    std::string out;
    out.reserve(format.size());
    bool formattedField = false;
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{') {
        out.push_back('{');
        i += 2;
        continue;
      }
      if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}') {
        out.push_back('}');
        i += 2;
        continue;
      }
      if (format[i] != '{') {
        out.push_back(format[i++]);
        continue;
      }

      const std::size_t end = format.find('}', i + 1);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      const std::string_view field = format.substr(i + 1, end - i - 1);
      const std::size_t colon = field.find(':');
      if (colon == std::string_view::npos) {
        return std::nullopt;
      }
      std::string_view spec = field.substr(colon + 1);
      const std::size_t firstPercent = spec.find('%');
      if (firstPercent == std::string_view::npos) {
        return std::nullopt;
      }
      spec.remove_prefix(firstPercent);
      out += formatStrftime(spec, local, unixSeconds);
      formattedField = true;
      i = end + 1;
    }
    return formattedField ? std::make_optional(std::move(out)) : std::nullopt;
  }

} // namespace

std::string formatLocalUnixTime(const std::int64_t unixSeconds, const std::string_view format) {
  if (format.empty()) {
    return {};
  }

  using namespace std::chrono;
  const std::string normalized = normalizeFormatEscapes(format);
  const auto timePoint = sys_seconds{seconds{unixSeconds}};
  const std::time_t raw = system_clock::to_time_t(timePoint);
  std::tm local{};
  localtime_r(&raw, &local);
  if (const auto compat = formatStrftimeCompat(normalized, local, unixSeconds)) {
    return *compat;
  }

  const auto localTime = local_seconds{seconds{raw + local.tm_gmtoff}};
  try {
    return std::vformat(std::locale(""), normalized, std::make_format_args(localTime));
  } catch (...) {
    return normalized;
  }
}

std::string formatLocalTime(const std::string_view format) {
  using namespace std::chrono;
  const auto now = floor<seconds>(system_clock::now());
  return formatLocalUnixTime(duration_cast<seconds>(now.time_since_epoch()).count(), format);
}
