#ifndef DYNAMICBUFFER_H
#define DYNAMICBUFFER_H

#include <cstddef>
#include <memory>

namespace csval
{
    const std::size_t defaultSize = 5000;
}

namespace cs
{
    /*!
        RAII fixed dynamic memory c-array wrapper
    */
    class DynamicBuffer final
    {
    public:
        explicit DynamicBuffer(std::size_t size = csval::defaultSize);

        DynamicBuffer(const DynamicBuffer& buffer);
        DynamicBuffer(DynamicBuffer&& buffer);

        DynamicBuffer& operator=(const DynamicBuffer& buffer);
        DynamicBuffer& operator=(DynamicBuffer&& buffer);

        ~DynamicBuffer();

        char& operator[](std::size_t index);
        const char& operator[](std::size_t index) const;

        /*!
            Returns pointer to c-array
        */
        char* get() const;

        /*!
            get() method similar
        */
        char* operator*() const;

        /*!
            Returns c-array fixed size
        */
        std::size_t size() const;

        // stl - like interace
        char* begin();
        char* end();
        const char* begin() const;
        const char* end() const;

    private:
        char* mArray = nullptr;
        std::size_t mSize = 0;

        friend void swap(DynamicBuffer&, DynamicBuffer&);
    };

    bool operator==(const DynamicBuffer& lhs, const DynamicBuffer& rhs);
    bool operator!=(const DynamicBuffer& lhs, const DynamicBuffer& rhs);

    void swap(DynamicBuffer& lhs, DynamicBuffer& rhs);

    /*!
        Smart dynamic buffer
    */
    using DynamicBufferPtr = std::shared_ptr<DynamicBuffer>;
}

#endif // DYNAMICBUFFER_H
