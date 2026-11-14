#pragma once

#include <stdexcept>

namespace ak650 {

// Anything the user can act on: no keyboard, no permission, bad file, a refused command.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace ak650
