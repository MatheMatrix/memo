#include <elle/serialization/Serializer.hh>
#include <elle/utils.hh>

#include <memo/model/Conflict.hh>

namespace memo
{
  namespace model
  {
    Conflict::Conflict(std::string const& reason,
                       std::unique_ptr<blocks::Block> current)
      : Super(elle::sprintf("conflict editing block: %s", reason))
      , _current(std::move(current))
    {}

    // FIXME: Unfortunately serialization needs exceptions to be copiable (see
    // elle/serialization/Serializer.hxx). This actually moves the exception.
    Conflict::Conflict(Conflict const& source)
      : Super(source.what())
      , _current(std::move(elle::unconst(source)._current))
    {}

    /*--------------.
    | Serialization |
    `--------------*/

    Conflict::Conflict(elle::serialization::SerializerIn& input)
      : Super(input)
    {
      this->_serialize(input);
    }

    void
    Conflict::serialize(elle::serialization::Serializer& s,
                        elle::Version const& v)
    {
      Super::serialize(s, v);
      this->_serialize(s);
    }

    void
    Conflict::_serialize(elle::serialization::Serializer& s)
    {
      s.serialize("current", this->_current);
    }

    // Exception was moved, but we cannot change the name for backward compatibility
    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<Conflict> _register_serialization("infinit::model::doughnut::Conflict");
  }
}
