#include <memo/model/doughnut/GB.hh>

#include <elle/serialization/json.hh>

#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/User.hh>
#include <memo/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("memo.model.doughnut.GB");

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      static elle::Version group_description_version(0, 8, 0);

      GB::GB(Doughnut* owner,
             elle::cryptography::rsa::KeyPair master)
        : Super(owner, {}, elle::Buffer("group", 5), master)
      {
        ELLE_TRACE_SCOPE("%s: create", *this);
        auto const first_group_key
          = elle::cryptography::rsa::keypair::generate(2048);
        this->_public_keys.push_back(first_group_key.K());
        this->_keys.push_back(first_group_key);
        auto const user_key = owner->keys();
        auto const ser_master = elle::serialization::binary::serialize(master.k());
        auto sealed = user_key.K().seal(ser_master);
        this->_admin_keys.emplace(user_key.K(), sealed);
        this->data(elle::serialization::binary::serialize(this->_keys));
        this->_acl_changed = true;
        this->set_permissions(user_key.K(), true, false);
      }

      GB::GB(elle::serialization::SerializerIn& s,
             elle::Version const& version)
        : Super(s, version)
      {
        ELLE_TRACE_SCOPE("%s: deserialize", *this);
        this->_serialize(s, version);
        // Extract owner key if possible
        if (this->doughnut())
        {
          auto const& keys = this->doughnut()->keys();
          auto it = this->_admin_keys.find(keys.K());
          if (it != this->_admin_keys.end())
          {
            ELLE_DEBUG("we are group admin");
            this->_owner_private_key =
              std::make_shared(elle::serialization::binary::deserialize<
                             elle::cryptography::rsa::PrivateKey>(
                               keys.k().open(it->second)));
          }
          else
            ELLE_DEBUG("we are not group admin");
        }
      }

      void
      GB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s, version);
        this->_serialize(s, version);
      }

      void
      GB::_serialize(elle::serialization::Serializer& s,
                     elle::Version const& version)
      {
        s.serialize("public_keys", this->_public_keys);
        s.serialize("admin_keys", this->_admin_keys);
        if (version >= group_description_version)
          s.serialize("description", this->_description);
      }

      GB::OwnerSignature::OwnerSignature(GB const& b)
        : Super::OwnerSignature(b)
        , _block(b)
      {}

      void
      GB::OwnerSignature::_serialize(elle::serialization::SerializerOut& s,
                                     elle::Version const& v)
      {
        ELLE_ASSERT_GTE(v, elle::Version(0, 4, 0));
        GB::Super::OwnerSignature::_serialize(s, v);
        s.serialize("admins", this->_block._admin_keys);
      }

      std::unique_ptr<typename BaseOKB<blocks::GroupBlock>::OwnerSignature>
      GB::_sign() const
      {
        return std::make_unique<OwnerSignature>(*this);
      }

      GB::DataSignature::DataSignature(GB const& block)
        : GB::Super::DataSignature(block)
        , _block(block)
      {}

      void
      GB::DataSignature::serialize(elle::serialization::Serializer& s_,
                                   elle::Version const& v)
      {
        // FIXME: Improve when split-serialization is added.
        ELLE_ASSERT(s_.out());
        auto& s = reinterpret_cast<elle::serialization::SerializerOut&>(s_);
        GB::Super::DataSignature::serialize(s, v);
        s.serialize("public_keys", this->_block.public_keys());
        if (v >= group_description_version)
          s.serialize("description", this->_block._description);
      }

      std::unique_ptr<GB::Super::DataSignature>
      GB::_data_sign() const
      {
        return std::make_unique<DataSignature>(*this);
      }

      elle::cryptography::rsa::PublicKey
      GB::current_public_key() const
      {
        return this->_public_keys.back();
      }

      elle::cryptography::rsa::KeyPair
      GB::current_key() const
      {
        if (this->_keys.empty())
          elle::unconst(this)->_extract_keys();
        return this->_keys.back();
      }

      int
      GB::group_version() const
      {
        return this->_public_keys.size();
      }

      std::vector<elle::cryptography::rsa::KeyPair>
      GB::all_keys() const
      {
        if (this->_keys.empty())
          elle::unconst(this)->_extract_keys();
        return this->_keys;
      }

      std::vector<elle::cryptography::rsa::PublicKey>
      GB::all_public_keys() const
      {
        return this->_public_keys;
      }

      void
      GB::_extract_keys()
      {
        this->_keys = elle::serialization::binary::deserialize<
          decltype(this->_keys)>(this->data());
      }

      void
      GB::add_member(model::User const& user)
      {
        this->_set_permissions(user, true, false);
        this->_acl_changed = true;
      }

      void
      GB::remove_member(model::User const& user)
      {
        this->_set_permissions(user, false, false);
        this->_acl_changed = true;
        _extract_keys();
        auto new_key = elle::cryptography::rsa::keypair::generate(2048);
        this->_public_keys.push_back(new_key.K());
        this->_keys.push_back(new_key);
        this->data(elle::serialization::binary::serialize(this->_keys));
      }

      void
      GB::add_admin(model::User const& user_)
      {
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          if (contains(this->_admin_keys, user.key()))
            return;
          auto ser_master = elle::serialization::binary::serialize(
            *this->_owner_private_key);
          this->_admin_keys.emplace(user.key(), user.key().seal(ser_master));
          this->_acl_changed = true;
        }
        catch (std::bad_cast const&)
        {
          elle::err("doughnut was passed a non-doughnut user");
        }
      }

      void
      GB::remove_admin(model::User const& user_)
      {
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto it = this->_admin_keys.find(user.key());
          if (it == this->_admin_keys.end())
            elle::err("no such admin: %s", user.key());
          this->_admin_keys.erase(it);
          this->_acl_changed = true;
        }
        catch (std::bad_cast const&)
        {
          elle::err("doughnut was passed a non-doughnut user");
        }
      }

      std::vector<std::unique_ptr<model::User>>
      GB::list_admins(bool ommit_names) const
      {
        std::vector<std::unique_ptr<model::User>> res;
        for (auto const& key: this->_admin_keys)
        {
          std::unique_ptr<model::User> user;
          if (ommit_names)
            user.reset(new doughnut::User(key.first, ""));
          else
            user = this->doughnut()->make_user(
              elle::serialization::json::serialize(key.first));
          if (!user)
            ELLE_TRACE("Failed to create user from key %x", key.first);
          else
            res.emplace_back(std::move(user));
        }
        return res;
      }

      std::shared_ptr<elle::cryptography::rsa::PrivateKey>
      GB::control_key() const
      {
        return _owner_private_key;
      }

      boost::optional<std::string> const&
      GB::description() const
      {
        if (this->doughnut()->version() < group_description_version)
          elle::err("description is only supported in version 0.8.0 or later");
        return this->_description;
      }

      void
      GB::description(boost::optional<std::string> const& description)
      {
        if (this->doughnut()->version() < group_description_version)
          elle::err("description is only supported in version 0.8.0 or later");
        this->_description = description;
        this->_acl_changed = true;
      }

      std::unique_ptr<blocks::Block>
      GB::clone() const
      {
        return std::unique_ptr<blocks::Block>(new Self(*this));
      }

      GB::GB(const GB& other)
        : Super(other)
        , _public_keys(other._public_keys)
        , _admin_keys(other._admin_keys)
        , _description(other._description)
      {}

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<GB> _register_gb_serialization("GB");

      /*----------.
      | Printable |
      `----------*/

      void
      GB::print(std::ostream& ouptut) const
      {
        elle::fprintf(ouptut, "%s(%f)",
                      elle::type_info<GB>(), *this->owner_key());
      }
    }
  }
}
