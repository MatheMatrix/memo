#include <elle/log.hh>

#include <memo/model/blocks/ACLBlock.hh>

ELLE_LOG_COMPONENT("memo.model.blocks.ACLBlock");

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      char const* ACLBlock::type = "acl";

      /*-------------.
      | Construction |
      `-------------*/

      ACLBlock::ACLBlock(Address address, elle::Buffer data, Address owner)
        : Super(address, std::move(data), std::move(owner))
      {}

      ACLBlock::ACLBlock(ACLBlock const& other)
        : Super(other)
      {}

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<Block>
      ACLBlock::clone() const
      {
        return std::unique_ptr<Block>(new ACLBlock(*this));
      }

      /*------------.
      | Permissions |
      `------------*/

      void
      ACLBlock::set_permissions(User const& user,
                                bool read,
                                bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permissions for %f: read = %s, write = %s",
                         *this, user, read, write);
        this->_set_permissions(user, read, write);
      }

      void
      ACLBlock::set_world_permissions(bool read, bool write)
      {
        ELLE_TRACE_SCOPE("%s: set world perms to r=%s w=%s",
                         *this, read, write);
        this->_set_world_permissions(read, write);
      }

      std::pair<bool,bool>
      ACLBlock::get_world_permissions() const
      {
        return this->_get_world_permissions();
      }

      void
      ACLBlock::copy_permissions(ACLBlock& to)
      {
        ELLE_TRACE_SCOPE("%s: copy permissions to %s", *this, to);
        this->_copy_permissions(to);
      }

      std::vector<ACLBlock::Entry>
      ACLBlock::list_permissions(boost::optional<Model const&> model) const
      {
        ELLE_TRACE_SCOPE("%s: list permissions", *this);
        return this->_list_permissions(model);
      }

      void
      ACLBlock::_set_permissions(User const&, bool, bool)
      {
        // FIXME: what do ?
      }

      void
      ACLBlock::_set_world_permissions(bool, bool)
      {}

      std::pair<bool, bool>
      ACLBlock::_get_world_permissions() const
      {
        return {false, false};
      }

      void
      ACLBlock::_copy_permissions(ACLBlock& to)
      {}

      std::vector<ACLBlock::Entry>
      ACLBlock::_list_permissions(boost::optional<Model const&>) const
      {
        return {};
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACLBlock::ACLBlock(elle::serialization::Serializer& input,
                         elle::Version const& version)
        : Super(input, version)
      {
        this->_serialize(input);
      }

      void
      ACLBlock::serialize(elle::serialization::Serializer& s,
                          elle::Version const& version)
      {
        this->Super::serialize(s, version);
        this->_serialize(s);
      }

      void
      ACLBlock::_serialize(elle::serialization::Serializer&)
      {}

      static const elle::serialization::Hierarchy<Block>::
      Register<ACLBlock> _register_serialization("acl");
    }
  }
}
