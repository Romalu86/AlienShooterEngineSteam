#pragma once

#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace as1 { namespace script
{
    template <class T>
    class RetailNoInitAllocator : public std::allocator<T>
    {
    public:
        using value_type = T;

        RetailNoInitAllocator() noexcept = default;
        template <class U>
        RetailNoInitAllocator(const RetailNoInitAllocator<U>&) noexcept {}

        template <class U>
        struct rebind { using other = RetailNoInitAllocator<U>; };

        template <class U>
        void construct(U* p)
        {
#if defined(_WIN32) && defined(_M_IX86)
            ::new (static_cast<void*>(p)) U;
#else
            ::new (static_cast<void*>(p)) U();
#endif
        }

        template <class U, class Arg0, class... Args>
        void construct(U* p, Arg0&& arg0, Args&&... args)
        {
            ::new (static_cast<void*>(p)) U(
                std::forward<Arg0>(arg0), std::forward<Args>(args)...);
        }
    };

    template <class T, class U>
    inline bool operator==(const RetailNoInitAllocator<T>&, const RetailNoInitAllocator<U>&) noexcept { return true; }
    template <class T, class U>
    inline bool operator!=(const RetailNoInitAllocator<T>&, const RetailNoInitAllocator<U>&) noexcept { return false; }

    using RetailByteBuffer = std::vector<std::uint8_t, RetailNoInitAllocator<std::uint8_t>>;
} }
