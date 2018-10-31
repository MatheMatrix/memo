#include <elle/Exception.hh>

#include <elle/assert.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/Serializer.hh>

namespace elle
{
  /*-------------.
  | Construction |
  `-------------*/

  Exception::Exception(std::string const& message, int skip)
    : Exception(Backtrace::current(1 + skip), message)
  {}

  Exception::Exception(Backtrace bt, std::string const& message)
    : std::runtime_error(message)
    , _backtrace(std::move(bt))
  {}

  Exception::~Exception() noexcept = default;

  void
  Exception::inner_exception(std::exception_ptr exception)
  {
    this->_inner_exception = exception;
  }

  /*--------------.
  | Serialization |
  `--------------*/

  Exception::Exception(elle::serialization::SerializerIn& input)
    : std::runtime_error(input.deserialize<std::string>("message"))
  {}

  void
  Exception::serialize(elle::serialization::Serializer& s,
                       elle::Version const& version)
  {
    auto message = std::string{this->what()};
    s.serialize("message", message);
  }

  /*--------.
  | Helpers |
  `--------*/

  std::ostream&
  operator <<(std::ostream& s, Exception const& e)
  {
    s << e.what();
    if (os::getenv("ELLE_DEBUG_BACKTRACE", false))
      s << e.backtrace();
    return s;
  }

  std::string
  exception_string(std::exception_ptr err)
  {
    return exception_string(err, std::current_exception());
  }

  std::string
  exception_string(std::exception_ptr eptr, std::exception_ptr cur)
  {
    if (!eptr)
    {
      ELLE_ASSERT(cur);
      eptr = cur;
    }
    if (!eptr)
      throw Exception{"no current exception"};
    try
    {
      std::rethrow_exception(eptr);
    }
    catch (std::exception const& e)
    {
      return e.what();
    }
    catch (...)
    {
      return "unknown exception type";
    }

    elle::unreachable();
  }
}
