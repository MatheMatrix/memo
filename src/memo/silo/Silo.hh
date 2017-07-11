#pragma once

#include <atomic>
#include <cstdint>
#include <iosfwd>

#include <boost/filesystem.hpp>
#include <boost/signals2.hpp>

#include <elle/Buffer.hh>
#include <elle/attribute.hh>
#include <elle/optional.hh>
#include <elle/serialization/Serializer.hh>

#include <memo/descriptor/TemplatedBaseDescriptor.hh>
#include <memo/model/Address.hh>
#include <memo/model/prometheus.hh>
#include <memo/serialization.hh>
#include <memo/silo/fwd.hh>

namespace memo
{
  namespace silo
  {
    namespace bfs = boost::filesystem;

    enum class BlockStatus
    {
      exists,
      missing,
      unknown
    };

    class Silo
    {
    public:
      Silo(boost::optional<int64_t> capacity = {});
      virtual
      ~Silo();
      /// Get the data associated to key @a k.
      ///
      /// @param k Key of the looked-up data.
      /// @throw MissingKey if the key is absent.
      elle::Buffer
      get(Key k) const;
      /// Set the data associated to key @a k.
      ///
      /// @param k      Key of the set data.
      /// @param value  Value to associate to @a k.
      /// @param insert Whether to accept inserting a new key.
      /// @param update Whether to accept updating an existing key.
      /// @return The delta in used storage space in bytes.
      /// @throw Collision if the key is present and not @a update.
      /// @throw InsufficientSpace if there is not enough space left to store
      ///                          the data.
      /// @throw MissingKey if the key is absent and not @a insert.
      // FIXME: why not passing `value` by value?
      int
      set(Key k, elle::Buffer const& value,
          bool insert = true, bool update = false);

      /// Erase key @a k and associated data.
      ///
      /// @param k  Key to remove.
      /// @return   The delta (non positive!) in used storage space in bytes.
      /// @throw    MissingKey if the key is absent.
      int
      erase(Key k);

      /// List of all keys in the storage.
      ///
      /// @return A list of all keys in the storage.
      std::vector<Key>
      list();

      BlockStatus
      status(Key k);
      void
      register_notifier(std::function<void ()> f);

      /// The type of storage (e.g., "s3").
      virtual
      std::string
      type() const = 0;

    protected:
      virtual
      elle::Buffer
      _get(Key k) const = 0;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) = 0;
      virtual
      int
      _erase(Key k) = 0;
      virtual
      std::vector<Key>
      _list() = 0;

      /// Return the status of a given key.
      /// Implementations should check locally only if the information is
      /// available, or return BlockStatus::unknown.
      virtual
      BlockStatus
      _status(Key k);

      /// Notify subscribers to register_notifier.
      ///
      /// Should be called by ctors of subclasses if they update
      /// _usage, etc.
      void
      _notify_metrics();

      ELLE_ATTRIBUTE_R(boost::optional<int64_t>, capacity, protected);
      /// Number of bytes used.
      ELLE_ATTRIBUTE_R(std::atomic<int64_t>, usage, protected);
      ELLE_ATTRIBUTE(int64_t, base_usage);
      ELLE_ATTRIBUTE(int64_t, step);
      ELLE_ATTRIBUTE((std::unordered_map<Key, int>), size_cache,
                     mutable, protected);
      ELLE_ATTRIBUTE(boost::signals2::signal<void ()>, on_storage_size_change);
      /// Number of blocks.
      ELLE_ATTRIBUTE_R(std::atomic<int64_t>, block_count, protected);
    };

    std::unique_ptr<Silo>
    instantiate(std::string const& name,
                std::string const& args);

    /// Whether is a block name.
    inline
    bool
    is_block(std::string const& name)
    {
      return name.substr(0, 2) == "0x" && name.length() == 66;
    }

    /// Whether is a block name.
    inline
    bool
    is_block(bfs::directory_entry const& p)
    {
      return is_block(p.path().filename().string());
    }

    struct SiloConfig
      : public descriptor::TemplatedBaseDescriptor<SiloConfig>
      , public elle::serialization::VirtuallySerializable<SiloConfig, false>
    {
      SiloConfig() = default;
      SiloConfig(std::string name,
                    boost::optional<int64_t> capacity,
                    boost::optional<std::string> description);
      SiloConfig(elle::serialization::SerializerIn& input);

      void
      serialize(elle::serialization::Serializer& s) override;
      using serialization_tag = memo::serialization_tag;
      static constexpr char const* virtually_serializable_key = "type";
      virtual
      std::unique_ptr<memo::silo::Silo>
      make() = 0;

      boost::optional<int64_t> capacity;

      static
      std::string
      name_regex();
    };
  }
}
