#include <elle/Error.hh>
#include <elle/serialization/Serializer.hh>

namespace elle
{
  /*-------------.
  | Construction |
  `-------------*/

  Error::Error(std::string const& message)
    : Super{message, 1}
  {}

  Error::Error(elle::Backtrace bt, std::string const& message)
    : Super{std::move(bt), message}
  {}

  /*--------------.
  | Serialization |
  `--------------*/

  Error::Error(elle::serialization::SerializerIn& input)
    : Super(input)
  {}

  namespace
  {
    const elle::serialization::Hierarchy<elle::Exception>::
    Register<Error> _register_serialization;
  }
}
