#pragma once

#include <memory>
#include <stdexcept>

#include <elle/Backtrace.hh>
#include <elle/attribute.hh>
#include <elle/compiler.hh>
#include <elle/serialization.hh>
#include <elle/serialization/fwd.hh>

namespace elle ELLE_API
{
  /// Base class for exceptions, with Backtrace.
  class Exception
    : public std::runtime_error
    , public elle::serialization::VirtuallySerializable<Exception, true>
  {
  /*-------------.
  | Construction |
  `-------------*/
  public:
    using Self = Exception;

    /// Construct an Exception.
    ///
    /// @param message An explanatory string.
    /// @param skip The number of stack frames to skip
    Exception(std::string const& message, int skip = 0);
    /// Construct an Exception.
    ///
    /// @param bt The Backtrace to attach to the Exception.
    /// @param message An explanatory string.
    Exception(Backtrace bt, std::string const& message);
    ~Exception() noexcept override;
    /// Set an inner exception.
    ///
    /// @param ex An exception pointer.
    void
    inner_exception(std::exception_ptr ex);

  /*--------------.
  | Serialization |
  `--------------*/
  public:
    /// Construct an Exception from a serialized Exception.
    ///
    /// @param input A SerializerIn.
    using serialization_tag = elle::serialization_tag;
    Exception(elle::serialization::SerializerIn& input);

    /// Serialize an Exception.
    ///
    /// @see elle::serialization::VirtuallySerializable::serialize.
    void
    serialize(elle::serialization::Serializer& s,
              elle::Version const& version) override;

  private:
    template <typename T>
    friend
    void
    throw_with_nested(T&& t);
    ELLE_ATTRIBUTE_RW(Backtrace, backtrace);
    ELLE_ATTRIBUTE_R(std::exception_ptr, inner_exception);
  };

  std::ostream&
  operator << (std::ostream& s, Exception const& e);

  /// Like std::throw_with_nested but strip the Backtrace with the current
  /// Exception.
  ///
  /// @see Backtrace::strip_base.
  ///
  /// @tparam T Type of exception to nest.
  /// @param t The exception to nest.
  template <typename T>
  ELLE_COMPILER_ATTRIBUTE_NORETURN
  void
  throw_with_nested(T&& t);

  /// Try to return the most exhaustive string representing the given
  /// exception (or the current one if none is given).
  ///
  /// If no exception is given, use the current exception.
  ///
  /// @param err The exception, defaults to the current one.
  ///            The default value is not shown here, it's in elle/fwd.hh.
  /// @returns The string representing the given exception.
  std::string
  exception_string(std::exception_ptr err);

  /// Bouncer for exception_string.
  std::string
  exception_string(std::exception_ptr err, std::exception_ptr cur);
}

#include <elle/Exception.hxx>
