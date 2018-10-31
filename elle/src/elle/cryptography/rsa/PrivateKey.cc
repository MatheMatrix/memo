#include <elle/cryptography/rsa/PrivateKey.hh>

#include <openssl/engine.h>
#include <openssl/crypto.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <elle/log.hh>

#include <elle/cryptography/Error.hh>
#include <elle/cryptography/bn.hh>
#include <elle/cryptography/cryptography.hh>
#include <elle/cryptography/envelope.hh>
#include <elle/cryptography/finally.hh>
#include <elle/cryptography/hash.hh>
#include <elle/cryptography/raw.hh>
#include <elle/cryptography/context.hh>
#include <elle/cryptography/rsa/KeyPair.hh>
#include <elle/cryptography/rsa/Padding.hh>
#include <elle/cryptography/rsa/Seed.hh>
#include <elle/cryptography/rsa/der.hh>
#include <elle/cryptography/rsa/low.hh>
#include <elle/cryptography/rsa/serialization.hh>

#if defined ELLE_CRYPTOGRAPHY_ROTATION
# include <dopenssl/rsa.hh>
#endif

namespace elle
{
  namespace cryptography
  {
    namespace rsa
    {
      namespace privatekey
      {
        /*--------------.
        | Serialization |
        `--------------*/

        struct Serialization:
          public rsa::serialization::RSA
        {
          static
          elle::Buffer
          encode(::RSA* rsa)
          {
            return rsa::der::encode_private(rsa);
          }

          static
          ::RSA*
          decode(elle::ConstWeakBuffer const& buffer)
          {
            return rsa::der::decode_private(buffer);
          }
        };
      }

      /*-------------.
      | Construction |
      `-------------*/

      PrivateKey::PrivateKey(::EVP_PKEY* key)
        : _key(key)
      {
        ELLE_ASSERT(key);
        ELLE_ASSERT(key->pkey.rsa->n);
        ELLE_ASSERT(key->pkey.rsa->e);
        ELLE_ASSERT(key->pkey.rsa->d);
        ELLE_ASSERT(key->pkey.rsa->p);
        ELLE_ASSERT(key->pkey.rsa->q);
        ELLE_ASSERT(key->pkey.rsa->dmp1);
        ELLE_ASSERT(key->pkey.rsa->dmq1);
        ELLE_ASSERT(key->pkey.rsa->iqmp);

        // Make sure the cryptographic system is set up.
        cryptography::require();

        if (::EVP_PKEY_type(this->_key->type) != EVP_PKEY_RSA)
          throw Error(
            elle::sprintf("the EVP_PKEY key is not of type RSA: %s",
                          ::EVP_PKEY_type(this->_key->type)));

        this->_check();
      }

      PrivateKey::PrivateKey(::RSA* rsa)
      {
        ELLE_ASSERT(rsa);
        ELLE_ASSERT(rsa->n);
        ELLE_ASSERT(rsa->e);
        ELLE_ASSERT(rsa->d);
        ELLE_ASSERT(rsa->p);
        ELLE_ASSERT(rsa->q);
        ELLE_ASSERT(rsa->dmp1);
        ELLE_ASSERT(rsa->dmq1);
        ELLE_ASSERT(rsa->iqmp);

        // Make sure the cryptographic system is set up.
        cryptography::require();

        // Construct the private key based on the given RSA structure.
        this->_construct(rsa);

        this->_check();
      }

      PrivateKey::PrivateKey(PrivateKey const& other)
      {
        ELLE_ASSERT(other._key->pkey.rsa->n);
        ELLE_ASSERT(other._key->pkey.rsa->e);
        ELLE_ASSERT(other._key->pkey.rsa->d);
        ELLE_ASSERT(other._key->pkey.rsa->p);
        ELLE_ASSERT(other._key->pkey.rsa->q);
        ELLE_ASSERT(other._key->pkey.rsa->dmp1);
        ELLE_ASSERT(other._key->pkey.rsa->dmq1);
        ELLE_ASSERT(other._key->pkey.rsa->iqmp);

        // Make sure the cryptographic system is set up.
        cryptography::require();

        // Duplicate the RSA structure.
        RSA* _rsa = low::RSA_dup(other._key->pkey.rsa);

        ELLE_CRYPTOGRAPHY_FINALLY_ACTION_FREE_RSA(_rsa);

        this->_construct(_rsa);

        ELLE_CRYPTOGRAPHY_FINALLY_ABORT(_rsa);

        this->_check();
      }

      PrivateKey::PrivateKey(PrivateKey&& other):
        _key(std::move(other._key))
      {
        // Make sure the cryptographic system is set up.
        cryptography::require();

        this->_check();
      }

      /*--------.
      | Methods |
      `--------*/

      void
      PrivateKey::_construct(::RSA* rsa)
      {
        ELLE_ASSERT(rsa);

        // Initialise the private key structure.
        ELLE_ASSERT_EQ(this->_key, nullptr);
        this->_key.reset(::EVP_PKEY_new());

        if (this->_key == nullptr)
          throw Error(
            elle::sprintf("unable to allocate the EVP_PKEY structure: %s",
                          ::ERR_error_string(ERR_get_error(), nullptr)));

        // Set the rsa structure into the private key.
        if (::EVP_PKEY_assign_RSA(this->_key.get(), rsa) <= 0)
          throw Error(
            elle::sprintf("unable to assign the RSA key to the EVP_PKEY "
                          "structure: %s",
                          ::ERR_error_string(ERR_get_error(), nullptr)));
      }

      void
      PrivateKey::_check() const
      {
        ELLE_ASSERT(this->_key);
        ELLE_ASSERT(this->_key->pkey.rsa);
        ELLE_ASSERT(this->_key->pkey.rsa->n);
        ELLE_ASSERT(this->_key->pkey.rsa->e);
        ELLE_ASSERT(this->_key->pkey.rsa->d);
        ELLE_ASSERT(this->_key->pkey.rsa->p);
        ELLE_ASSERT(this->_key->pkey.rsa->q);
        ELLE_ASSERT(this->_key->pkey.rsa->dmp1);
        ELLE_ASSERT(this->_key->pkey.rsa->dmq1);
        ELLE_ASSERT(this->_key->pkey.rsa->iqmp);
      }

      elle::Buffer
      PrivateKey::open(elle::ConstWeakBuffer const& code,
                       Cipher const cipher,
                       Mode const mode) const
      {
        auto is = elle::IOStream(code.istreambuf());
        std::stringstream plain;

        this->open(is, plain,
                   cipher, mode);

        return plain.str();
      }

      void
      PrivateKey::open(std::istream& code,
                       std::ostream& plain,
                       Cipher const cipher,
                       Mode const mode) const
      {
        envelope::open(this->_key.get(),
                       cipher::resolve(cipher, mode),
                       code,
                       plain);
      }

      elle::Buffer
      PrivateKey::decrypt(elle::ConstWeakBuffer const& code,
                          Padding const padding) const
      {
        auto prolog =
          [padding](::EVP_PKEY_CTX* context)
          {
            padding::pad(context, padding);
          };

        return raw::asymmetric::decrypt(this->_key.get(),
                                        code,
                                        prolog);
      }

      elle::Buffer
      PrivateKey::sign(elle::ConstWeakBuffer const& plain,
                       Padding const padding,
                       Oneway const oneway) const
      {
        auto is = elle::IOStream(plain.istreambuf());
        return this->sign(is,
                          padding, oneway);
      }

      elle::Buffer
      PrivateKey::sign(std::istream& plain,
                       Padding const padding,
                       Oneway const oneway) const
      {
        auto prolog =
          [padding](::EVP_MD_CTX* context, ::EVP_PKEY_CTX* ctx)
          {
            padding::pad(ctx, padding);
          };

        return raw::asymmetric::sign(
                  this->_key.get(),
                  oneway::resolve(oneway),
                  plain,
                  prolog);
      }

      uint32_t
      PrivateKey::size() const
      {
        return static_cast<uint32_t>(
                  ::EVP_PKEY_size(this->_key.get()));
      }

      uint32_t
      PrivateKey::length() const
      {
        return static_cast<uint32_t>(
                  ::EVP_PKEY_bits(this->_key.get()));
      }

#if defined ELLE_CRYPTOGRAPHY_ROTATION
      /*---------.
      | Rotation |
      `---------*/

      PrivateKey::PrivateKey(Seed const& seed)
      {
        // Make sure the cryptographic system is set up.
        cryptography::require();

        // Deduce the RSA key from the given seed.
        if (auto rsa = ::dRSA_deduce_privatekey(
               seed.length(),
               static_cast<unsigned char const*>(seed.buffer().contents()),
               seed.buffer().size()))
        {
          ELLE_CRYPTOGRAPHY_FINALLY_ACTION_FREE_RSA(rsa);

          // Construct the private key based on the given RSA structure.
          this->_construct(rsa);

          ELLE_CRYPTOGRAPHY_FINALLY_ABORT(rsa);

          this->_check();
        }
        else
          throw Error(
            elle::sprintf("unable to deduce the RSA key from the given "
                          "seed: %s",
                          ::ERR_error_string(ERR_get_error(), nullptr)));
      }

      Seed
      PrivateKey::rotate(Seed const& seed) const
      {
        auto prolog =
          [this](::EVP_PKEY_CTX* ctx)
          {
            padding::pad(ctx, rsa::Padding::none);
          };

        // Note that in these cases, using no RSA padding is not dangerous
        // because (1) the content being rotated is always random (2) the
        // content is always the size of the RSA key's modulus.
        elle::Buffer buffer = raw::asymmetric::rotate(this->_key.get(),
                                                      seed.buffer(),
                                                      prolog);

        return Seed(buffer, seed.length());
      }
#endif

      /*----------.
      | Operators |
      `----------*/

      bool
      PrivateKey::operator ==(PrivateKey const& other) const
      {
        if (this == &other)
          return true;

        ELLE_ASSERT(this->_key);
        ELLE_ASSERT(other._key);

        // Compare the public components because it is sufficient to
        // uniquely distinguish keys.
        return ::EVP_PKEY_cmp(this->_key.get(), other._key.get()) == 1;
      }

      PrivateKey&
      PrivateKey::operator =(PrivateKey&& other)
      {
        this->_key = std::move(other._key);
        cryptography::require();
        this->_check();
        return *this;
      }

      /*--------------.
      | Serialization |
      `--------------*/

      PrivateKey::PrivateKey(elle::serialization::SerializerIn& serializer)
      {
        // Make sure the cryptographic system is set up.
        cryptography::require();

        // Allocate the EVP key to receive the deserialized's RSA structure.
        this->_key.reset(::EVP_PKEY_new());

        // Set the EVP key as being of type RSA.
        if (::EVP_PKEY_set_type(this->_key.get(), EVP_PKEY_RSA) <= 0)
          throw Error(
            elle::sprintf("unable to set the EVP key's type: %s",
                          ::ERR_error_string(ERR_get_error(), nullptr)));

        this->serialize(serializer);

        this->_check();
      }

      void
      PrivateKey::serialize(elle::serialization::Serializer& serializer)
      {
        ELLE_ASSERT(this->_key);

        cryptography::serialize<privatekey::Serialization>(
          serializer,
          this->_key->pkey.rsa);
        ELLE_ASSERT(this->_key->pkey.rsa);
      }

      /*----------.
      | Printable |
      `----------*/

      void
      PrivateKey::print(std::ostream& o) const
      {
        ELLE_ASSERT(this->_key);
        ELLE_ASSERT(this->_key->pkey.rsa);
        ELLE_ASSERT(this->_key->pkey.rsa->n);
        ELLE_ASSERT(this->_key->pkey.rsa->e);
        ELLE_ASSERT(this->_key->pkey.rsa->d);
        elle::print(o, "PrivateKey(%f)", privatekey::der::encode(*this));
      }
    }
  }
}

//
// ---------- DER -------------------------------------------------------------
//

namespace elle
{
  namespace cryptography
  {
    namespace rsa
    {
      namespace privatekey
      {
        namespace der
        {
          /*----------.
          | Functions |
          `----------*/

          elle::Buffer
          encode(PrivateKey const& K)
          {
            return rsa::der::encode_private(K.key()->pkey.rsa);
          }

          PrivateKey
          decode(elle::ConstWeakBuffer const& buffer)
          {
            ::RSA* rsa = rsa::der::decode_private(buffer);

            return PrivateKey(rsa);
          }
        }
      }
    }
  }
}

namespace std
{
  size_t
  hash<elle::cryptography::rsa::PrivateKey>::operator ()(
    elle::cryptography::rsa::PrivateKey const& value) const
  {

    std::stringstream stream;
    {
      elle::serialization::binary::SerializerOut output(stream);
      output.serialize("value", value);
    }

    return std::hash<std::string>()(stream.str());
  }
}
