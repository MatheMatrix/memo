#pragma once

#include <boost/filesystem.hpp>

#include <elle/Error.hh>

#include <memo/silo/Silo.hh>
#include <memo/silo/Key.hh>
#include <memo/silo/GoogleAPI.hh>

#include <elle/reactor/http/Request.hh>
#include <elle/reactor/http/url.hh>

namespace memo
{
  namespace silo
  {
    class GCS
      : public Silo, public GoogleAPI
    {
    public:
      GCS(std::string const& name,
          std::string const& bucket,
          std::string const& root,
          std::string const& refresh_token);

      std::string
      type() const override { return "gcs"; }

    protected:
      elle::Buffer
      _get(Key k) const override;

      int
      _set(Key k,
           elle::Buffer const& value,
           bool insert,
           bool update) override;

      int
      _erase(Key k) override;

      std::vector<Key>
      _list() override;

      ELLE_ATTRIBUTE_R(std::string, bucket);
      ELLE_ATTRIBUTE_R(std::string, root);

      std::string
      _url(Key key) const;
    };

    struct GCSConfig: public SiloConfig
    {
      GCSConfig(std::string const& name,
                std::string const& bucket,
                std::string const& root,
                std::string const& user_name,
                std::string const& refresh_token,
                boost::optional<int64_t> capacity,
                boost::optional<std::string> description);
      GCSConfig(elle::serialization::SerializerIn& input);
      void serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<memo::silo::Silo> make() override;
      std::string bucket;
      std::string root;
      std::string refresh_token;
      std::string user_name;
    };
  }
}
