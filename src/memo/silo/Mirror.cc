#include <memo/silo/Mirror.hh>

#include <boost/algorithm/string.hpp>

#include <elle/factory.hh>
#include <elle/from-string.hh>
#include <elle/reactor/Scope.hh>

#include <memo/model/Address.hh>

ELLE_LOG_COMPONENT("memo.silo.Mirror");

namespace memo
{
  namespace silo
  {
    Mirror::Mirror(std::vector<std::unique_ptr<Silo>> backend,
                   bool balance_reads, bool parallel)
      : _balance_reads(balance_reads)
      , _backend(std::move(backend))
      , _read_counter(0)
      , _parallel(parallel)
    {}

    elle::Buffer
    Mirror::_get(Key k) const
    {
      const_cast<Mirror*>(this)->_read_counter++;
      int target = _balance_reads? _read_counter % _backend.size() : 0;
      return _backend[target]->get(k);
    }

    int
    Mirror::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      if (_parallel)
        elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& s)
        {
          for (auto& e: _backend)
          {
            Silo* ptr = e.get();
            s.run_background("mirror set", [&,ptr] {
                ptr->set(k, value, insert, update);
              });
          }
          s.wait();
        };
      else
        for (auto& e: _backend)
          e->set(k, value, insert, update);
      return 0;
    }

    int
    Mirror::_erase(Key k)
    {
      if (_parallel)
      {
        elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& s)
        {
          for (auto& e: _backend)
          {
            Silo* ptr = e.get();
            s.run_background("mirror erase", [&,ptr] { ptr->erase(k);});
          }
          s.wait();
        };
      }
      else
      {
        for (auto& e: _backend)
        {
          e->erase(k);
        }
      }

      return 0;
    }

    std::vector<Key>
    Mirror::_list()
    {
      return _backend.front()->list();
    }

    namespace
    {
      std::unique_ptr<Silo>
      make(std::vector<std::string> const& args)
      {
        bool const balance_reads = elle::from_string<bool>(args[0]);
        bool const parallel = elle::from_string<bool>(args[1]);
        auto backends = std::vector<std::unique_ptr<Silo>>{};
        // FIXME: we don't check that i+1 is ok.
        for (int i = 2; i < signed(args.size()); i += 2)
          backends.emplace_back(instantiate(args[i], args[i+1]));
        return std::make_unique<Mirror>(std::move(backends), balance_reads, parallel);
      }
    }

    struct MirrorSiloConfig
      : public SiloConfig
    {
      bool parallel;
      bool balance;
      std::vector<std::unique_ptr<SiloConfig>> storage;

      MirrorSiloConfig(std::string name,
                          boost::optional<int64_t> capacity,
                          boost::optional<std::string> description)
        : SiloConfig(
            std::move(name), std::move(capacity), std::move(description))
      {}

      MirrorSiloConfig(elle::serialization::SerializerIn& s)
        : SiloConfig(s)
      {
        this->serialize(s);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        SiloConfig::serialize(s);
        s.serialize("parallel", this->parallel);
        s.serialize("balance", this->balance);
        s.serialize("backend", this->storage);
      }

      virtual
      std::unique_ptr<memo::silo::Silo>
      make() override
      {
        std::vector<std::unique_ptr<memo::silo::Silo>> s;
        for(auto const& c: storage)
        {
          ELLE_ASSERT(!!c);
          s.push_back(c->make());
        }
        return std::make_unique<memo::silo::Mirror>(
          std::move(s), balance, parallel);
      }
    };

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<MirrorSiloConfig>
    _register_MirrorSiloConfig("mirror");
  }
}

FACTORY_REGISTER(memo::silo::Silo, "mirror", &memo::silo::make);
