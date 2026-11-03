#include "time/time_format.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>

namespace {

  bool expect(std::string_view name, std::string_view actual, std::string_view expected) {
    if (actual == expected) {
      return true;
    }
    std::fprintf(
        stderr, "%.*s: expected '%.*s', got '%.*s'\n", static_cast<int>(name.size()), name.data(),
        static_cast<int>(expected.size()), expected.data(), static_cast<int>(actual.size()), actual.data()
    );
    return false;
  }

} // namespace

int main() {
  std::setlocale(LC_ALL, "C");
  ::setenv("TZ", "UTC", 1);
  ::tzset();

  constexpr std::int64_t kExampleTime = 1777885507; // 2026-05-04 09:05:07 UTC
  bool passed = true;
  passed =
      expect("bare strftime", formatLocalUnixTime(kExampleTime, "%Y-%m-%d %H:%M:%S"), "2026-05-04 09:05:07") && passed;
  passed = expect("chrono fields", formatLocalUnixTime(kExampleTime, "{:%H:%M} {:%A}"), "09:05 Monday") && passed;
  passed =
      expect("newline escape", formatLocalUnixTime(kExampleTime, "{:%H:%M}\\n{:%F}"), "09:05\n2026-05-04") && passed;
  passed = expect("literal braces", formatLocalUnixTime(kExampleTime, "{{{:%H}}}"), "{09}") && passed;
  passed = expect("unix seconds", formatLocalUnixTime(kExampleTime, "%s"), "1777885507") && passed;
  passed = expect("empty format", formatLocalUnixTime(kExampleTime, ""), "") && passed;
  return passed ? 0 : 1;
}
