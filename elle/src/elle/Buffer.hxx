#include <cstring>
#include <iosfwd>
#include <stdexcept>

#include <elle/BadAlloc.hh>
#include <elle/assert.hh>

namespace elle
{
  inline
  void
  detail::MallocDeleter::operator ()(void* data)
  {
    ::free(data);
  }

  /*-------------.
  | Construction |
  `-------------*/

  template <typename T,
            std::enable_if_t<std::is_integral<T>::value, int>>
  Buffer::Buffer(T size)
    : _size(static_cast<Size>(size))
    , _capacity(size)
    , _contents(nullptr)
  {
    if ((this->_contents =
         static_cast<Byte*>(::malloc(this->_capacity))) == nullptr)
      throw std::bad_alloc();
  }

  inline
  ConstWeakBuffer::ConstWeakBuffer()
    : _size(0)
    , _contents(nullptr)
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(const void* data, Size size)
    : _size(size)
    , _contents(static_cast<Buffer::Byte*>(const_cast<void*>(data)))
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(std::string const& data)
    : Self(data.c_str(), data.size())
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(char const* data)
    : Self(data, strlen(data))
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(Buffer const& buffer)
    : Self(buffer.mutable_contents(), buffer.size())
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(ConstWeakBuffer const& other)
    : Self(other._contents, other._size)
  {}

  inline
  ConstWeakBuffer::ConstWeakBuffer(ConstWeakBuffer&& other)
    : Self(other)
  {
    other._contents = nullptr;
    other._size = 0;
  }

  inline
  WeakBuffer::WeakBuffer()
    : ConstWeakBuffer()
  {}

  template <typename Char, std::size_t S,
            typename>
  ConstWeakBuffer::ConstWeakBuffer(Char const (&array)[S], Size size)
    : Self(&array, size)
  {}

  template <typename Char, std::size_t S,
            typename>
  ConstWeakBuffer::ConstWeakBuffer(Char const (&array)[S])
    : Self(array, S)
  {}

  inline
  WeakBuffer::WeakBuffer(void* data, Size size)
    : Super(data, size)
  {}

  inline
  WeakBuffer::WeakBuffer(Buffer const& buffer)
    : Super(buffer)
  {}

  inline
  WeakBuffer::WeakBuffer(WeakBuffer const& other)
    : Super(other)
  {}

  inline
  WeakBuffer::WeakBuffer(WeakBuffer&& other)
    : Super(std::move(other))
  {}

  template <typename Char, std::size_t S,
            typename>
  WeakBuffer::WeakBuffer(Char (&array)[S])
    : Self(&array, S)
  {}

  inline
  Buffer::Byte*
  WeakBuffer::mutable_contents() const
  {
    return const_cast<Buffer::Byte*>(this->contents());
  }

  inline
  Buffer::Byte*
  Buffer::mutable_contents() const
  {
    return this->_contents;
  }
}
