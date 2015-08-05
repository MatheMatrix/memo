#include <infinit/storage/Dropbox.hh>

#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Dropbox");

namespace infinit
{
  namespace storage
  {
    Dropbox::Dropbox(std::string token)
      : Dropbox(std::move(token), ".infinit")
    {}

    Dropbox::Dropbox(std::string token,
                     boost::filesystem::path root)
      : _dropbox(std::move(token))
      , _root(std::move(root))
    {}

    Dropbox::~Dropbox()
    {}

    boost::filesystem::path
    Dropbox::_path(Key key) const
    {
      return this->_root / elle::sprintf("%x", key);
    }

    elle::Buffer
    Dropbox::_get(Key key) const
    {
      try
      {
        return this->_dropbox.get(this->_path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    void
    Dropbox::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      if (insert)
      {
        auto insertion =
          this->_dropbox.put(this->_path(key), value, update);
        if (!insertion && !update)
          throw Collision(key);
      }
      else if (update)
      {
        ELLE_ABORT("not implemented (can dropbox handle it?)");
      }
      else
        throw elle::Error("neither inserting neither updating");
    }

    void
    Dropbox::_erase(Key key)
    {
      try
      {
        return this->_dropbox.delete_(this->_path(key));
      }
      catch (dropbox::NoSuchFile const&)
      {
        throw MissingKey(key);
      }
    }

    std::vector<Key>
    Dropbox::_list()
    {
      auto metadata = this->_dropbox.metadata(this->_root);
      std::vector<Key> res;
      if (!metadata.is_dir || !metadata.contents)
        throw elle::Error(".infinit is not a directory");
      for (auto const& entry: metadata.contents.get())
        res.push_back(model::Address::from_string(entry.path));
      return res;
    }

    struct DropboxStorageConfig
      : public StorageConfig
    {
    public:
      DropboxStorageConfig(elle::serialization::SerializerIn& input)
        : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("token", this->token);
        s.serialize("root", this->root);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        if (this->root)
          return elle::make_unique<infinit::storage::Dropbox>(
            this->token, this->root.get());
        else
          return elle::make_unique<infinit::storage::Dropbox>(this->token);
      }

      std::string token;
      boost::optional<std::string> root;
    };
    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<DropboxStorageConfig> _register_DropboxStorageConfig("dropbox");
  }
}
