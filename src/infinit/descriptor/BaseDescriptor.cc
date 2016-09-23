#include <infinit/descriptor/BaseDescriptor.hh>

#include <boost/regex.hpp>

#include <elle/assert.hh>
#include <elle/serialization/Serializer.hh>

namespace infinit
{
  namespace descriptor
  {
    /*-------.
    | Errors |
    `-------*/

    DescriptorError::DescriptorError(std::string const& message)
      : elle::Error(message)
    {}

    static const std::string invalid_name(
      "name (%s) must only contain lower case alphanumeric and - . _ "
      "characters but not start with the . character");

    DescriptorNameError::DescriptorNameError(std::string const& name)
      : DescriptorError(elle::sprintf(invalid_name, name))
    {}

    /*-----.
    | Name |
    `-----*/

    BaseDescriptor::Name::Name()
      : Name("")
    {}

    BaseDescriptor::Name::Name(std::string name)
      : std::string(std::move(name))
    {}

    BaseDescriptor::Name::Name(std::string const& qualifier,
                               std::string const& name)
      : std::string(elle::sprintf("%s/%s", qualifier, name))
    {}

    std::string
    BaseDescriptor::Name::qualifier() const
    {
      auto pos = this->find("/");
      ELLE_ASSERT_NEQ(pos, std::string::npos);
      return this->substr(0, pos);
    }

    std::string
    BaseDescriptor::Name::name() const
    {
      auto pos = this->find("/");
      ELLE_ASSERT_NEQ(pos, std::string::npos);
      return this->substr(pos + 1);
    }

    std::string
    BaseDescriptor::Name::unqualified(std::string const& qualifier) const
    {
      if (this->qualifier() == qualifier)
        return this->name();
      return *this;
    }

    /*-------------.
    | Construction |
    `-------------*/

    BaseDescriptor::BaseDescriptor(std::string name,
                                   boost::optional<std::string> description)
      : name(std::move(name))
      , description(std::move(description))
    {}

    BaseDescriptor::BaseDescriptor(BaseDescriptor const& descriptor)
      : name(descriptor.name)
      , description(descriptor.description)
    {}

    /*--------------.
    | Serialization |
    `--------------*/

    BaseDescriptor::BaseDescriptor(elle::serialization::SerializerIn& s)
      : name(s.deserialize<std::string>("name"))
      , description(s.deserialize<boost::optional<std::string>>("description"))
    {}

    void
    BaseDescriptor::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("description", this->description);
    }

    /*----------.
    | Printable |
    `----------*/

    void
    BaseDescriptor::print(std::ostream& out) const
    {
      out << "BaseDescriptor(" << this->name;
      if (this->description)
        out << ", \"" << this->description << "\"";
      out << ")";
    }
  }
}
