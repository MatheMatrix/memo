#include <infinit/model/doughnut/GB.hh>

#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.GB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      GB::GB(Doughnut* owner, cryptography::rsa::KeyPair master)
      : Super(owner, {}, elle::Buffer("group", 5), master)
      , _master_key(master.k())
      {
        ELLE_TRACE_SCOPE("creating new group");
        keys().emplace(master);
        auto first_group_key = cryptography::rsa::keypair::generate(2048);
        this->_group_public_keys.push_back(first_group_key.K());
        this->_group_keys.push_back(first_group_key);
        auto user_key = owner->keys();
        auto ser_master = elle::serialization::binary::serialize(master.k());
        auto sealed = user_key.K().seal(ser_master);
        this->_group_admins.push_back(user_key.K());
        this->_ciphered_master_key.push_back(sealed);
        this->data(elle::serialization::binary::serialize(this->_group_keys));
        this->_acl_changed = true;
        this->set_permissions(user_key.K(), true, false);
        ELLE_ASSERT_EQ(_ciphered_master_key.size(), _group_admins.size());
        ELLE_TRACE("group created");
      }

      GB::GB(elle::serialization::SerializerIn& s,
             elle::Version const& version)
      : Super(s, version)
      {
        s.serialize("group_public_keys", this->_group_public_keys);
        s.serialize("group_admins", this->_group_admins);
        s.serialize("master_key", this->_ciphered_master_key);
        ELLE_ASSERT_EQ(_ciphered_master_key.size(), _group_admins.size());
      }

      void
      GB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s, version);
        ELLE_ASSERT_EQ(_ciphered_master_key.size(), _group_admins.size());
        s.serialize("group_public_keys", this->_group_public_keys);
        s.serialize("group_admins", this->_group_admins);
        s.serialize("master_key", this->_ciphered_master_key);
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
        s.serialize("group_admins", this->_block.group_admins());
        s.serialize("master_key", this->_block.ciphered_master_key());
      }

      std::unique_ptr<typename BaseOKB<blocks::GroupBlock>::OwnerSignature>
      GB::_sign() const
      {
        return elle::make_unique<OwnerSignature>(*this);
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
        s.serialize("group_public_keys", this->_block.group_public_keys());
        s.serialize("group_admins", this->_block.group_admins());
        s.serialize("master_key", this->_block.ciphered_master_key());
      }

      std::unique_ptr<GB::Super::DataSignature>
      GB::_data_sign() const
      {
        return elle::make_unique<DataSignature>(*this);
      }

      cryptography::rsa::PublicKey
      GB::current_public_key()
      {
        return this->_group_public_keys.back();
      }

      cryptography::rsa::KeyPair
      GB::current_key()
      {
        if (this->_group_keys.empty())
          this->_extract_group_keys();
        return this->_group_keys.back();
      }
      int
      GB::version()
      {
        return this->_group_public_keys.size();
      }
      std::vector<cryptography::rsa::KeyPair>
      GB::all_keys()
      {
        if (this->_group_keys.empty())
          this->_extract_group_keys();
        return this->_group_keys;
      }
      std::vector<cryptography::rsa::PublicKey>
      GB::all_public_keys()
      {
        return this->_group_public_keys;
      }
      void
      GB::_extract_group_keys()
      {
        boost::optional<elle::Buffer> d;
        try
        {
          d.emplace(this->data());
        }
        catch (ValidationFailed const& vf)
        {
          // we are not in the group, but maybe we are owner
          this->_extract_master_key();
          this->keys().emplace(
            cryptography::rsa::PublicKey(*this->_master_key),
            *this->_master_key);
          d.emplace(this->data());
        }
        ELLE_ASSERT(!d->empty());
        this->_group_keys = elle::serialization::binary::deserialize<
          decltype(this->_group_keys)>(*d);
      }
      void
      GB::_extract_master_key()
      {
        ELLE_TRACE_SCOPE("extract_master_key, have %s admins", _group_admins.size());
        ELLE_ASSERT_EQ(_group_admins.size(), _ciphered_master_key.size());
        auto it = std::find(_group_admins.begin(),
          _group_admins.end(), doughnut()->keys().K());

        if (it != this->_group_admins.end())
        {
          auto buf = doughnut()->keys().k().open(_ciphered_master_key[it - _group_admins.begin()]);
          this->_master_key.emplace(elle::serialization::binary::deserialize<
            cryptography::rsa::PrivateKey>(buf));
          return;
        }
        throw elle::Error("Access to master key denied.");
      }
      void
      GB::add_member(model::User const& user)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(*this->owner_key(),
          *this->_master_key));
        this->_set_permissions(user, true, false);
        this->_acl_changed = true;
      }
      void
      GB::remove_member(model::User const& user)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(*this->owner_key(),
          *this->_master_key));
        this->_set_permissions(user, false, false);
        this->_acl_changed = true;
        _extract_group_keys();
        auto new_key = infinit::cryptography::rsa::keypair::generate(2048);
        this->_group_public_keys.push_back(new_key.K());
        this->_group_keys.push_back(new_key);
        this->data(elle::serialization::binary::serialize(this->_group_keys));
      }
      void
      GB::add_admin(model::User const& user_)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(*this->owner_key(),
          *this->_master_key));
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto it = std::find(_group_admins.begin(),
          _group_admins.end(), user.key());
          if (it != this->_group_admins.end())
            return;
          auto ser_master = elle::serialization::binary::serialize(*this->_master_key);
          this->_group_admins.push_back(user.key());
          this->_ciphered_master_key.push_back(user.key().seal(ser_master));
          this->_acl_changed = true;
          ELLE_ASSERT_EQ(_ciphered_master_key.size(), _group_admins.size());
        }
        catch (std::bad_cast const&)
        {
          throw elle::Error("doughnut was passed a non-doughnut user.");
        }
      }
      void
      GB::remove_admin(model::User const& user_)
      {
        _extract_master_key();
        this->keys().emplace(cryptography::rsa::KeyPair(*this->owner_key(),
          *this->_master_key));
        try
        {
          auto& user = dynamic_cast<doughnut::User const&>(user_);
          auto it = std::find(_group_admins.begin(),
            _group_admins.end(), user.key());
          if (it == this->_group_admins.end())
            throw elle::Error("No such user in admin list");
          this->_ciphered_master_key.erase(
            _ciphered_master_key.begin() + (it - _group_admins.begin()));
          this->_group_admins.erase(it);
          this->_acl_changed = true;
          ELLE_ASSERT_EQ(_ciphered_master_key.size(), _group_admins.size());
        }
        catch (std::bad_cast const&)
        {
          throw elle::Error("doughnut was passed a non-doughnut user.");
        }
      }

      std::vector<std::unique_ptr<model::User>>
      GB::list_admins(bool ommit_names)
      {
        std::vector<std::unique_ptr<model::User>> res;
        for (auto const& key: this->_group_admins)
        {
          std::unique_ptr<model::User> user;
          if (ommit_names)
            user.reset(new doughnut::User(*this->owner_key(), ""));
          else
            user = this->doughnut()->make_user(
                elle::serialization::json::serialize(key));
            res.emplace_back(std::move(user));
        }
        return res;
      }

      std::unique_ptr<blocks::Block>
      GB::clone(bool sealed_copy) const
      {
        return std::unique_ptr<blocks::Block>(new Self(*this, sealed_copy));
      }
      GB::GB(const GB& other, bool sealed_copy)
      : Super(other, sealed_copy)
      , _group_public_keys(other._group_public_keys)
      , _group_admins(other._group_admins)
      , _ciphered_master_key(other._ciphered_master_key)
      {}
      GB::~GB()
      {}

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<GB> _register_gb_serialization("GB");
    }
  }
}
