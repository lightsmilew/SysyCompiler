#include "antlr4-runtime.h"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <stdint.h>
#include <set>
#include <stdexcept>
#include <stack>
#include <bitset>
#include <limits>
#include <cstring>
#include <queue>

#define makePtr std::make_shared
#define Move std::move
#define Scast static_cast
#define thisPtr(T) std::enable_shared_from_this<T>::shared_from_this()

#define castPtr std::dynamic_pointer_cast
using String = std::string;
template <typename T>
using Ptr = std::shared_ptr<T>;
template <typename T>
using Vector = std::vector<T>;
template <typename T>
using Set = std::unordered_set<T>;
template <typename T>
using OrderedSet = std::set<T>;
template <typename TKey, typename TValue>
using Map = std::unordered_map<TKey, TValue>;
template <typename TKey, typename TValue>
using OrderedMap = std::map<TKey, TValue>;

#define BITSET_SIZE 100000
using Bitset = std::bitset<BITSET_SIZE>;

#pragma endregion

#pragma region 方法

template <typename T>
Set<T> toSet(const Vector<T> &vec) {
    return Set<T>(vec.begin(), vec.end());
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifdef ANTLR_4_9_3
#define AS(obj, T) obj.as<T>()
#define IS(T) is<T>()
#else
#define AS(obj, T) std::any_cast<T>(obj)
#define IS(T) type() == typeid(T)
#endif

template <class T>
inline void hashCombine(std::size_t &s, const T &v) {
    std::hash<T> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}
