#pragma once

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/User.hh>
#include <memo/model/blocks/MutableBlock.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      class ACLBlock
        : public MutableBlock
        , private InstanceTracker<ACLBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = ACLBlock;
        using Super = MutableBlock;
        static char const* type;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ACLBlock(ACLBlock const& other);
      protected:
        ACLBlock(Address address,
                 elle::Buffer data = {},
                 Address owner = Address::null);
        friend class memo::model::Model;

      /*-------.
      | Clone  |
      `-------*/
      public:
        std::unique_ptr<blocks::Block>
        clone() const override;

      /*------------.
      | Permissions |
      `------------*/
      public:
        void
        set_permissions(User const& user,
                        bool read,
                        bool write);
        void
        set_world_permissions(bool read, bool write);
        std::pair<bool, bool>
        get_world_permissions() const;
        void
        copy_permissions(ACLBlock& to);

        struct Entry
        {
          Entry()
          {}
          Entry(std::unique_ptr<User> u, bool r, bool w, bool a, bool o = false)
            : user(std::move(u)), admin(a), owner(o), read(r), write(w)
          {}
          Entry(Entry&& b) = default;

          std::unique_ptr<User> user;
          bool admin;
          bool owner;
          bool read;
          bool write;
        };

        std::vector<Entry>
        list_permissions(boost::optional<Model const&> model) const;

      protected:
        virtual
        void
        _set_permissions(User const& user,
                         bool read,
                         bool write);
        virtual
        void
        _set_world_permissions(bool read, bool write);
        virtual
        std::pair<bool, bool>
        _get_world_permissions() const;
        virtual
        void
        _copy_permissions(ACLBlock& to);
        virtual
        std::vector<Entry>
        _list_permissions(boost::optional<Model const&> model) const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ACLBlock(elle::serialization::Serializer& input,
                 elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}
