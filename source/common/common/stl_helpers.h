#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

#include "absl/strings/str_join.h"

namespace Envoy {
/**
 * See if a reference exists within a container of std::reference_wrappers.
 */
template <class Container, class T> bool containsReference(const Container& c, const T& t) {
  return std::find_if(c.begin(), c.end(), [&](std::reference_wrapper<T> e) -> bool {
           return &e.get() == &t;
         }) != c.end();
}

/**
 * Accumulates a vector of into a string of the format [string_func(element_0),
 * string_func(element_1), ...]
 */
template <class T>
std::string accumulateToString(const std::vector<T>& source,
                               std::function<std::string(const T&)> string_func) {
  if (source.empty()) {
    return "[]";
  }
  return std::accumulate(std::next(source.begin()), source.end(), "[" + string_func(source[0]),
                         [string_func](std::string acc, const T& element) {
                           return acc + ", " + string_func(element);
                         }) +
         "]";
}
} // namespace Envoy

// NOLINT(namespace-envoy)
// Overload functions in std library.
namespace std {
// Overload std::operator<< to output a vector.
template <class T> std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  out << "vector { " << absl::StrJoin(v, ", ", absl::StreamFormatter()) << " }";
  return out;
}

} // namespace std
