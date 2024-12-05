#include <type_traits>

namespace vkrt {
namespace utils {

// Calculates an aligned offset or size, assuming power of 2 alignment
template<typename T>
T alignedOffset(T offset, T alignment) { return (offset + alignment - static_cast<T>(1)) & ~(alignment - 1); }

template<typename T>
T paddingSize(T size, T alignment) { return (alignment - (size % alignment)) % alignment; }

// Determines if a is a subset of b
template <typename BitType>
bool isSubset(BitType a, BitType b) { return (a & b) == a; }

}
}