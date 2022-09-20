// Enable ROOT version prior to 6.26 to be compiled with C++17
// Extracted from: https://github.com/acts-project/acts/issues/505

#ifndef FAKE_RStringView_H
#define FAKE_RStringView_H
#define RStringView_H

#include <cstddef>
#include <string_view>

namespace ROOT {
namespace Internal {
  class TStringView {
    const char* fData {nullptr};
    size_t fLength {0};

public:
    explicit TStringView(const char* cstr, size_t len)
        : fData(cstr)
        , fLength(len)
    {
    }

    operator std::string_view() const { return std::string_view(fData, fLength); }
  };
} // namespace Internal
} // namespace ROOT
#endif
