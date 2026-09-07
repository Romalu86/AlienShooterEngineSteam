#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace as1 { namespace script
{
    template <class T>
    class RetailRawArray
    {
    public:
        RetailRawArray() = default;
        RetailRawArray(const RetailRawArray&) = delete;
        RetailRawArray& operator=(const RetailRawArray&) = delete;

        RetailRawArray(RetailRawArray&& other) noexcept
        {
            swap(other);
        }

        RetailRawArray& operator=(RetailRawArray&& other) noexcept
        {
            if (this != &other)
            {
                release();
                swap(other);
            }
            return *this;
        }

        ~RetailRawArray()
        {
            release();
        }

        std::size_t size() const noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            return m_size;
#else
            return m_items.size();
#endif
        }

        std::size_t capacity() const noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            return m_capacity;
#else
            return m_items.capacity();
#endif
        }

        bool empty() const noexcept { return size() == 0; }

        T* data() noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            return m_data;
#else
            return m_items.empty() ? nullptr : m_items.data();
#endif
        }

        const T* data() const noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            return m_data;
#else
            return m_items.empty() ? nullptr : m_items.data();
#endif
        }

        T& operator[](std::size_t index) noexcept { return data()[index]; }
        const T& operator[](std::size_t index) const noexcept { return data()[index]; }

        T& at(std::size_t index)
        {
            if (index >= size())
                throw std::out_of_range("RetailRawArray::at");
            return (*this)[index];
        }

        const T& at(std::size_t index) const
        {
            if (index >= size())
                throw std::out_of_range("RetailRawArray::at");
            return (*this)[index];
        }

        T* begin() noexcept { return data(); }
        const T* begin() const noexcept { return data(); }
        T* end() noexcept { return data() ? data() + size() : nullptr; }
        const T* end() const noexcept { return data() ? data() + size() : nullptr; }

        void reserve(std::size_t newCapacity)
        {
#if defined(_WIN32) && defined(_M_IX86)
            if (newCapacity <= m_capacity)
                return;
            reallocate(newCapacity, m_size);
#else
            m_items.reserve(newCapacity);
#endif
        }

        void resize(std::size_t newSize)
        {
#if defined(_WIN32) && defined(_M_IX86)
            if (newSize > m_capacity)
                reallocate(newSize, m_size);
            m_size = newSize;
#else
            m_items.resize(newSize);
#endif
        }

        void clear() noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            release();
#else
            m_items.clear();
#endif
        }

        void push_back(const T& value)
        {
#if defined(_WIN32) && defined(_M_IX86)
            if (m_size >= m_capacity)
            {
                const std::size_t next = m_capacity * 2u + 4u;
                reallocate(next, m_size);
            }
            m_data[m_size] = value;
            ++m_size;
#else
            m_items.push_back(value);
#endif
        }

        T* erase(T* position)
        {
#if defined(_WIN32) && defined(_M_IX86)
            if (!m_data || position < m_data || position >= m_data + m_size)
                return end();
            const std::size_t index = static_cast<std::size_t>(position - m_data);
            for (std::size_t i = index; i + 1u < m_size; ++i)
                m_data[i] = m_data[i + 1u];
            if (m_size != 0)
            {
                --m_size;
                m_data[m_size] = T();
            }
            return m_data + index;
#else
            if (m_items.empty() || position < m_items.data() || position >= m_items.data() + m_items.size())
                return m_items.empty() ? nullptr : m_items.data() + m_items.size();
            const std::size_t index = static_cast<std::size_t>(position - m_items.data());
            m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
            return m_items.empty() ? nullptr : m_items.data() + (index < m_items.size() ? index : m_items.size());
#endif
        }

        void swap(RetailRawArray& other) noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            std::swap(m_data, other.m_data);
            std::swap(m_size, other.m_size);
            std::swap(m_capacity, other.m_capacity);
#else
            m_items.swap(other.m_items);
#endif
        }

        std::uint32_t retailHeaderCount() const noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            return m_data ? *(reinterpret_cast<const std::uint32_t*>(m_data) - 1) : 0u;
#else
            return static_cast<std::uint32_t>(capacity());
#endif
        }

        T* detachRetailAllocation() noexcept
        {
#if defined(_WIN32) && defined(_M_IX86)
            T* const result = m_data;
            m_data = nullptr;
            m_size = 0;
            m_capacity = 0;
            return result;
#else
            return nullptr;
#endif
        }

    private:
#if defined(_WIN32) && defined(_M_IX86)
        static T* allocateConstructed(std::size_t capacity)
        {
            if (capacity == 0)
                return nullptr;
            const std::size_t bytes = sizeof(std::uint32_t) + capacity * sizeof(T);
            auto* raw = static_cast<std::uint8_t*>(::operator new(bytes));
            *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(capacity);
            T* records = reinterpret_cast<T*>(raw + sizeof(std::uint32_t));
            std::size_t constructed = 0;
            try
            {
                for (; constructed < capacity; ++constructed)
                    ::new (static_cast<void*>(records + constructed)) T();
            }
            catch (...)
            {
                while (constructed != 0)
                {
                    --constructed;
                    records[constructed].~T();
                }
                ::operator delete(raw);
                throw;
            }
            return records;
        }

        static void destroyAllocation(T* records) noexcept
        {
            if (!records)
                return;
            auto* header = reinterpret_cast<std::uint32_t*>(records) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
                records[i - 1u].~T();
            ::operator delete(static_cast<void*>(header));
        }

        void reallocate(std::size_t newCapacity, std::size_t preservedSize)
        {
            T* replacement = allocateConstructed(newCapacity);
            const std::size_t copyCount = m_capacity < newCapacity ? m_capacity : newCapacity;
            try
            {
                for (std::size_t i = 0; i < copyCount; ++i)
                    replacement[i] = m_data[i];
            }
            catch (...)
            {
                destroyAllocation(replacement);
                throw;
            }
            destroyAllocation(m_data);
            m_data = replacement;
            m_capacity = newCapacity;
            m_size = preservedSize < newCapacity ? preservedSize : newCapacity;
        }

        void release() noexcept
        {
            destroyAllocation(m_data);
            m_data = nullptr;
            m_size = 0;
            m_capacity = 0;
        }

        T* m_data = nullptr;
        std::size_t m_size = 0;
        std::size_t m_capacity = 0;
#else
        void release() noexcept { m_items.clear(); std::vector<T>().swap(m_items); }
        std::vector<T> m_items;
#endif
    };
} }
