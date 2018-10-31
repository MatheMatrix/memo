#include <elle/Exception.hh>
#include <elle/serialization/Error.hh>
#include <elle/serialization/Serializer.hh>

namespace elle
{
  namespace serialization
  {
    Error::Error(std::string const& message)
      : elle::Error(message)
    {}

    Error::Error(SerializerIn& input)
      : elle::Error(input)
    {}

    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<Error> _register_serialization;
  }
}
