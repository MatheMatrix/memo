#include <infinit/storage/MissingKey.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace storage
  {
    MissingKey::MissingKey(Key key)
      : Super(elle::sprintf("missing key: %x", key))
      , _key(key)
    {}
    MissingKey::MissingKey(elle::serialization::SerializerIn& input)
    : Super(input)
    {
      input.serialize("key", _key);
    }
    void
    MissingKey::serialize(elle::serialization::Serializer& s)
    {
      Super::serialize(s);
      s.serialize("key", _key);
    }
    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<MissingKey> _register_serialization;
  }
}
