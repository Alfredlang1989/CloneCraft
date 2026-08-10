# nlohmann/json

Clonecraft vendors the header-only `nlohmann/json` single-header distribution so
JSON parsing does not depend on a distro package.

- Version: 3.12.0
- Vendored file: `json.hpp`
- License: MIT (`LICENSE.MIT`)
- Source used for this bundle: the supplied nlohmann/json repository archive,
  `single_include/nlohmann/json.hpp`.

CMake exposes `third_party/` as the include root, so existing
`#include <nlohmann/json.hpp>` directives resolve to this copy only.
