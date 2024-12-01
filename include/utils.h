#include <type_traits>

namespace vkrt {
namespace utils {

template<typename T>
T alignedOffset(T offset, T alignment) { return (offset + alignment - static_cast<T>(1)) & ~(alignment - 1); }

template<typename T>
T paddingSize(T size, T alignment) { return (alignment - (size % alignment)) % alignment; }

}
}