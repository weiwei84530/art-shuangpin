// Minimal JSON string escaping, shared by the CLI tools that emit data for
// the tutorial site and its audit script.

#ifndef MSPY_CLI_JSON_H_
#define MSPY_CLI_JSON_H_

#include <cstdio>
#include <string>

namespace mspy_cli {

inline std::string JsonString(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
  return out;
}

}  // namespace mspy_cli

#endif  // MSPY_CLI_JSON_H_
