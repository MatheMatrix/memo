#include <elle/Buffer.hh>
#include <elle/IOStream.hh>
#include <elle/log.hh>

#include <streambuf>

#include <sys/types.h>
#include <unistd.h>

ELLE_LOG_COMPONENT("elle.IOStream");

namespace elle
{
  /*--------------.
  | IOStreamClear |
  `--------------*/

  IOStreamClear::IOStreamClear(std::ios& s)
    : _stream(s)
  {}

  IOStreamClear::~IOStreamClear()
  {
    this->_stream.clear();
  }

  /*---------.
  | IOStream |
  `---------*/

  IOStream::IOStream(std::streambuf* buffer)
    : std::iostream(buffer)
    , _buffer(buffer)
  {
    exceptions(std::iostream::badbit);
  }

  IOStream::IOStream(IOStream&& source)
    : std::iostream(source._buffer)
    , _buffer(source._buffer)
  {
    source._buffer = nullptr;
  }

  IOStream::~IOStream()
  {
    if (this->_buffer)
    {
      this->_buffer->pubsync();
      delete this->_buffer;
    }
  }

  /*-------------.
  | StreamBuffer |
  `-------------*/

  StreamBuffer::StreamBuffer()
  {
    setg(0, 0, 0);
    setp(0, 0);
  }

  StreamBuffer::~StreamBuffer()
  {}

  int
  StreamBuffer::underflow()
  {
    ELLE_TRACE_SCOPE("%s: underflow", *this);
    WeakBuffer b = this->read_buffer();
    if (b.empty())
    {
      setg(0, 0, 0);
      return EOF;
    }
    else
    {
      auto cp = reinterpret_cast<char*>(b.mutable_contents());
      setg(cp, cp, cp + b.size());
      return static_cast<unsigned char>(b.contents()[0]);
    }
  }

  int
  StreamBuffer::overflow(int c)
  {
    ELLE_TRACE_SCOPE("%s: overflow", *this);
    this->sync();
    WeakBuffer b = this->write_buffer();
    setp(reinterpret_cast<char*>(b.mutable_contents()),
         reinterpret_cast<char*>(b.mutable_contents() + b.size()));
    b.mutable_contents()[0] = static_cast<Buffer::Byte>(c);
    pbump(1);
    // Success is indicated by "A value different from EOF".
    return EOF + 1;
  }

  int
  StreamBuffer::sync()
  {
    Size size = pptr() - pbase();
    ELLE_TRACE_SCOPE("%s: sync %s bytes", *this, size);
    setp(0, 0);
    if (size > 0)
      this->flush(size);
    // Success
    return 0;
  }

  void
  StreamBuffer::flush(Size)
  {}

  /*------------------.
  | PlainStreamBuffer |
  `------------------*/

  WeakBuffer
  PlainStreamBuffer::read_buffer()
  {
    ELLE_TRACE("read at most %s bytes", this->_bufsize)
    {
      Size size = this->read(this->_ibuf.data(), this->_bufsize);
      ELLE_TRACE("got %s bytes", size);
      return WeakBuffer(this->_ibuf.data(), size);
    }
  }

  WeakBuffer
  PlainStreamBuffer::write_buffer()
  {
    return WeakBuffer(this->_obuf.data(), this->_bufsize);
  }

  void
  PlainStreamBuffer::flush(Size size)
  {
    ELLE_TRACE("write %s bytes", size)
      this->write(this->_obuf.data(), size);
  }

  /*--------------------.
  | DynamicStreamBuffer |
  `--------------------*/

  DynamicStreamBuffer::DynamicStreamBuffer(Size size)
    : _bufsize(size)
    , _ibuf(new Byte[size])
    , _obuf(new Byte[size])
  {}

  DynamicStreamBuffer::~DynamicStreamBuffer()
  {
    delete [] this->_ibuf;
    delete [] this->_obuf;
  }

  WeakBuffer
  DynamicStreamBuffer::read_buffer()
  {
    ELLE_TRACE("read at most %s bytes", this->_bufsize)
    {
      Size size = this->read((char *)this->_ibuf, this->_bufsize);
      ELLE_TRACE("got %s bytes", size);
      return WeakBuffer{this->_ibuf, size};
    }
  }

  WeakBuffer
  DynamicStreamBuffer::write_buffer()
  {
    return WeakBuffer{this->_obuf, this->_bufsize};
  }

  void
  DynamicStreamBuffer::flush(Size size)
  {
    ELLE_TRACE("write %s bytes", size)
      write((char *)this->_obuf, size);
  }
}

namespace std
{
  streamsize
  readsome(std::istream& i, char* s, streamsize n)
  {
    if (n == 0)
      return 0;
    auto res = i.readsome(s, n);
    if (res > 0)
      return res;
    if (i.eof())
      return 0;
    int first = i.get();
    if (first == EOF)
      return 0;
    s[0] = first;
    res = 1;
    if (n >= 1)
      res += i.readsome(s + 1, n - 1);
    return res;
  }
}
