#include <memo/model/doughnut/UB.hh>

#include <elle/log.hh>
#include <elle/utils.hh>

#include <elle/cryptography/hash.hh>
#include <elle/cryptography/rsa/KeyPair.hh>

#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("memo.model.doughnut.UB");

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      UB::UB(Doughnut* dn, std::string name, Passport const& passport, bool reverse)
        : Super(reverse
                ? UB::hash_address(passport.user(), *dn)
                : UB::hash_address(name, *dn))
        , _name(std::move(name))
        , _key(passport.user())
        , _reverse(reverse)
        , _passport(passport)
        , _doughnut(dn)
      {}

      UB::UB(Doughnut* dn, std::string name, elle::cryptography::rsa::PublicKey key, bool reverse)
        : Super(reverse ?
                UB::hash_address(key, *dn) : UB::hash_address(name, *dn))
        , _name(std::move(name))
        , _key(std::move(key))
        , _reverse(reverse)
        , _passport()
        , _doughnut(dn)
      {}

      UB::UB(UB const& other)
        : Super(other)
        , _name{other._name}
        , _key{other._key}
        , _reverse{other._reverse}
        , _passport{other._passport}
        , _doughnut(other._doughnut)
      {}

      Address
      UB::hash_address(std::string const& name, Doughnut const& dht)
      {
        auto hash = elle::cryptography::hash (elle::sprintf("UB/%s", name),
                                        elle::cryptography::Oneway::sha256);
        return Address(hash.contents(), flags::immutable_block,
                       dht.version() >= elle::Version(0, 5, 0));
      }

      Address
      UB::hash_address(elle::cryptography::rsa::PublicKey const& key,
                       Doughnut const& dht)
      {
        auto buf = elle::cryptography::rsa::publickey::der::encode(key);
        auto hash = elle::cryptography::hash (elle::sprintf("RUB/%s", buf),
                                        elle::cryptography::Oneway::sha256);
        return Address(hash.contents(), flags::immutable_block,
                       dht.version() >= elle::Version(0, 5, 0));
      }

      elle::Buffer
      UB::hash(elle::cryptography::rsa::PublicKey const& key)
      {
        auto serial = elle::cryptography::rsa::publickey::der::encode(key);
        auto hash = elle::cryptography::hash(serial, elle::cryptography::Oneway::sha256);
        return hash;
      }

      /*-------.
      | Clone  |
      `-------*/

      std::unique_ptr<blocks::Block>
      UB::clone() const
      {
        return std::unique_ptr<blocks::Block>(new UB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      UB::_seal(boost::optional<int>)
      {}

      // FIXME: factor with CHB
      blocks::ValidationResult
      UB::_validate(Model const& model, bool writing) const
      {
        ELLE_DEBUG_SCOPE("%s: validate", *this);
        auto expected_address = this->reverse() ?
          UB::hash_address(this->key(), *this->_doughnut)
          : UB::hash_address(this->name(), *this->_doughnut);

        if (!equal_unflagged(this->address(), expected_address))
        {
          auto reason = elle::sprintf("address %x invalid, expecting %x",
                                      this->address(), expected_address);
          ELLE_DUMP("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        if (this->_passport && this->_passport->user() != this->key())
        {
          auto reason = elle::sprintf("user key mismatch in passport: %s v s %s",
                                      this->key(), this->_passport->user());
          ELLE_DEBUG("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        return blocks::ValidationResult::success();
      }

      blocks::RemoveSignature
      UB::_sign_remove(Model& model) const
      {
        auto const& dht = dynamic_cast<Doughnut const&>(model);
        auto& keys = dht.keys();
        if (keys.K() != this->_key && keys.K() != *dht.owner())
          elle::err("Only block owner and network owner can delete UB");
        auto to_sign = elle::serialization::binary::serialize((Block*)elle::unconst(this));
        auto signature = keys.k().sign(to_sign);
        blocks::RemoveSignature res;
        res.signature_key.emplace(keys.K());
        res.signature.emplace(signature);
        return res;
      }

      blocks::ValidationResult
      UB::_validate_remove(Model& model,
                           blocks::RemoveSignature const& sig) const
      {
        auto const& dht = dynamic_cast<Doughnut const&>(model);
        if (!sig.signature_key || !sig.signature)
          return blocks::ValidationResult::failure("Missing key or signature");
        auto to_sign = elle::serialization::binary::serialize((Block*)elle::unconst(this));
        bool ok = sig.signature_key->verify(*sig.signature, to_sign);
        if (!ok)
          return blocks::ValidationResult::failure("Invalid signature");
        if (*sig.signature_key != *dht.owner()
            && *sig.signature_key != this->_key)
          return blocks::ValidationResult::failure("Unauthorized signing key");
        return blocks::ValidationResult::success();
      }

      blocks::ValidationResult
      UB::_validate(Model const& model, const Block& new_block) const
      {
        auto ub = dynamic_cast<const UB*>(&new_block);
        if (ub)
        {
          if (this->_name == ub->_name
            && this->_key == ub->_key
            && this->_reverse == ub->_reverse)
          return blocks::ValidationResult::success();
        }
        return blocks::ValidationResult::failure("UB overwrite denied");
      }

      /*--------------.
      | Serialization |
      `--------------*/

      UB::UB(elle::serialization::SerializerIn& input,
             elle::Version const& version)
        : Super(input, version)
        , _name(input.deserialize<std::string>("name"))
        , _key(input.deserialize<elle::cryptography::rsa::PublicKey>("key"))
        , _reverse(input.deserialize<bool>("reverse"))
      {
        input.serialize_context<Doughnut*>(this->_doughnut);
        if (version >= elle::Version(0, 5, 0))
          input.serialize("passport", this->_passport);
      }

      void
      UB::serialize(elle::serialization::Serializer& s,
                    elle::Version const& version)
      {
        Super::serialize(s,version);
        this->_serialize(s, version);
      }

      void
      UB::_serialize(elle::serialization::Serializer& s,
                     elle::Version const& version)
      {
        s.serialize("name", this->_name);
        s.serialize("key", this->_key);
        s.serialize("reverse", this->_reverse);
        if (version >= elle::Version(0, 5, 0))
          s.serialize("passport", this->_passport);
      }

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<UB> _register_ub_serialization("UB");
    }
  }
}
