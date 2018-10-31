#include <memo/silo/Memory.hh>

#include <elle/factory.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>

#include <memo/silo/Collision.hh>
#include <memo/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("memo.silo.Memory");

namespace
{
  // As an int, since that's the type of Silo::_usage.
  template <typename Map>
  int mapped_size(Map const& map)
  {
    int res = 0;
    for (auto const& b: map)
      res += b.second.size();
    return res;
  }
}

namespace memo
{
  namespace silo
  {
    Memory::Memory()
      : _blocks(new Blocks, std::default_delete<Blocks>())
    {}

    Memory::Memory(Blocks& blocks)
      : _blocks(&blocks, [] (Blocks*) {})
    {
      ELLE_ASSERT_EQ(this->_usage, 0);
      ELLE_ASSERT_EQ(this->_block_count, 0);
      this->_usage = mapped_size(*this->_blocks);
      this->_block_count = int(this->_blocks->size());
    }

    Memory::~Memory()
    {
      _check_invariants();
    }

    auto
    Memory::_check_invariants() const
      -> void
    {
      ELLE_ASSERT_EQ(this->_block_count, int(this->_blocks->size()));
      ELLE_ASSERT_EQ(this->_usage, mapped_size(*this->_blocks));
    }

    auto
    Memory::_find(Key key)
      -> typename Blocks::iterator
    {
      auto it = this->_blocks->find(key);
      if (it == this->_blocks->end())
        throw MissingKey(key);
      else
        return it;
    }

    auto
    Memory::_find(Key key) const
      -> typename Blocks::const_iterator
    {
      auto it = this->_blocks->find(key);
      if (it == this->_blocks->end())
        throw MissingKey(key);
      else
        return it;
    }

    elle::Buffer
    Memory::_get(Key key) const
    {
      return _find(key)->second;
    }

    std::size_t
    Memory::size() const
    {
      _check_invariants();
      return this->_usage;
    }

    int
    Memory::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      if (insert)
      {
        auto p = this->_blocks->emplace(key, elle::Buffer());
        if (!p.second && !update)
          throw Collision(key);
        auto prev_size = p.second ? 0 : p.first->second.size();
        p.first->second = elle::Buffer(value.contents(), value.size());
        if (p.second)
        {
          ELLE_DEBUG("%s: block inserted", this);
          this->_block_count += 1;
        }
        else
          ELLE_DEBUG("%s: block updated: %s", this, p.first->second);
        return value.size() - prev_size;
      }
      else
      {
        auto it = _find(key);
        auto prev_size = it->second.size();
        it->second = elle::Buffer(value.contents(), value.size());
        ELLE_DEBUG("%s: block updated: %s", this, it->second);
        return value.size() - prev_size;
      }
    }

    int
    Memory::_erase(Key key)
    {
      auto it = _find(key);
      auto prev_size = it->second.size();
      this->_blocks->erase(it);
      this->_block_count -= 1;
      return - prev_size;
    }

    std::vector<Key>
    Memory::_list()
    {
      _check_invariants();
      return elle::make_vector(*this->_blocks,
                               [](auto const& b)
                               {
                                 return b.first;
                               });
    }

    void
    MemorySiloConfig::serialize(elle::serialization::Serializer& s)
    {
      SiloConfig::serialize(s);
    }

    std::unique_ptr<memo::silo::Silo>
    MemorySiloConfig::make()
    {
      return std::make_unique<memo::silo::Memory>();
    }
  }
}

namespace
{
  const auto reg
    = elle::serialization::Hierarchy<memo::silo::SiloConfig>::
    Register<memo::silo::MemorySiloConfig>("memory");

  std::unique_ptr<memo::silo::Silo>
  make(std::vector<std::string> const& args)
  {
    return std::make_unique<memo::silo::Memory>();
  }

  FACTORY_REGISTER(memo::silo::Silo, "memory", make);
}
