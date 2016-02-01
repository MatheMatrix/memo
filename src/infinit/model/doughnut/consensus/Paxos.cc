#include <infinit/model/doughnut/consensus/Paxos.hh>

#include <functional>

#include <elle/memory.hh>
#include <elle/bench.hh>

#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/hash.hh>

#include <infinit/RPC.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/DummyPeer.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Paxos");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.paxos." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {

        template<typename F>
        auto network_exception_to_unavailable(F f) -> decltype(f())
        {
          try
          {
            return f();
          }
          catch(reactor::network::Exception const& e)
          {
            ELLE_TRACE("network exception in paxos: %s", e);
            throw Paxos::PaxosClient::Peer::Unavailable();
          }
        }

        static
        Address
        uid(cryptography::rsa::PublicKey const& key)
        {
          auto serial = cryptography::rsa::publickey::der::encode(key);
          return
            cryptography::hash(serial, cryptography::Oneway::sha256).contents();
        }

        template <typename T>
        static
        void
        null_deleter(T*)
        {}

        struct BlockOrPaxos
        {
          BlockOrPaxos(blocks::Block* b)
            : block(b)
            , paxos()
          {}

          BlockOrPaxos(Paxos::LocalPeer::Decision* p)
            : block(nullptr)
            , paxos(p)
          {}

          BlockOrPaxos(elle::serialization::SerializerIn& s)
          {
            this->serialize(s);
          }

          blocks::Block* block;
          Paxos::LocalPeer::Decision* paxos;

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("block", this->block);
            s.serialize("paxos", this->paxos);
          }

          typedef infinit::serialization_tag serialization_tag;
        };

        /*-------------.
        | Construction |
        `-------------*/

        Paxos::Paxos(Doughnut& doughnut, int factor, bool lenient_fetch)
          : Super(doughnut)
          , _factor(factor)
          , _lenient_fetch(lenient_fetch)
        {
          if (getenv("INFINIT_PAXOS_LENIENT_FETCH"))
            _lenient_fetch = true;
        }

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Paxos::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return elle::make_unique<consensus::Paxos::LocalPeer>(
            this->factor(),
            this->doughnut(),
            this->doughnut().id(),
            std::move(storage),
            port ? port.get() : 0);
        }

        /*-----.
        | Peer |
        `-----*/

        class Peer
          : public Paxos::PaxosClient::Peer
        {
        public:
          Peer(overlay::Overlay::Member member, Address address)
            : Paxos::PaxosClient::Peer(member->id())
            , _member(std::move(member))
            , _address(address)
          {}

          virtual
          boost::optional<Paxos::PaxosClient::Accepted>
          propose(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p) override
          {
            BENCH("propose");
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(this->_member.get()))
                return local->propose(
                  q, this->_address, p);
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(this->_member.get()))
                return remote->propose(
                  q, this->_address, p);
              else if (dynamic_cast<DummyPeer*>(this->_member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", *this->_member);
            });
          }

          virtual
          Paxos::PaxosClient::Proposal
          accept(Paxos::PaxosClient::Quorum const& q,
                 Paxos::PaxosClient::Proposal const& p,
                 Paxos::Value const& value) override
          {
            BENCH("accept");
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(this->_member.get()))
                return local->accept(
                  q, this->_address, p, value);
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(this->_member.get()))
                return remote->accept(
                  q, this->_address, p, value);
              else if (dynamic_cast<DummyPeer*>(this->_member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", *this->_member);
            });
          }

          ELLE_ATTRIBUTE(overlay::Overlay::Member, member);
          ELLE_ATTRIBUTE(Address, address);
          ELLE_ATTRIBUTE(int, version);
        };

        /*-----------.
        | RemotePeer |
        `-----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::RemotePeer::propose(PaxosServer::Quorum const& peers,
                                   Address address,
                                   PaxosClient::Proposal const& p)
        {
          return network_exception_to_unavailable([&] {
            auto propose = make_rpc<boost::optional<PaxosClient::Accepted>(
              PaxosServer::Quorum,
              Address,
              PaxosClient::Proposal const&)>("propose");
            propose.set_context<Doughnut*>(&this->_doughnut);
            return propose(peers, address, p);
          });
        }

        Paxos::PaxosClient::Proposal
        Paxos::RemotePeer::accept(PaxosServer::Quorum const& peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p,
                                  Value const& value)
        {
          return network_exception_to_unavailable([&] {
            auto accept = make_rpc<Paxos::PaxosClient::Proposal (
              PaxosServer::Quorum peers,
              Address,
              Paxos::PaxosClient::Proposal const&,
              Value const&)>("accept");
            accept.set_context<Doughnut*>(&this->_doughnut);
            return accept(peers, address, p, value);
          });
        }

        std::pair<Paxos::PaxosServer::Quorum,
                  std::unique_ptr<Paxos::PaxosClient::Accepted>>
        Paxos::RemotePeer::_fetch_paxos(Address address)
        {
          auto fetch = make_rpc
            <std::pair<PaxosServer::Quorum,
                       std::unique_ptr<Paxos::PaxosClient::Accepted>>(Address)>
            ("fetch_paxos");
          fetch.set_context<Doughnut*>(&this->doughnut());
          return fetch(address);
        }

        /*----------.
        | LocalPeer |
        `----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::propose(PaxosServer::Quorum peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          ELLE_TRACE_SCOPE("%s: get proposal at %s: %s",
                           *this, address, p);
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              if (!stored.paxos)
                throw elle::Error("running Paxos on an immutable block");
              decision = this->_addresses.emplace(
                address, std::move(*stored.paxos)).first;
            }
            catch (storage::MissingKey const&)
            {
              decision = this->_addresses.emplace(
                address,
                Decision(PaxosServer(this->id(), peers))).first;
            }
          auto res = decision->second.paxos.propose(std::move(peers), p);
          this->storage()->set(
            address,
            elle::serialization::binary::serialize(
              BlockOrPaxos(&decision->second)),
            true, true);
          return res;
        }

        Paxos::PaxosClient::Proposal
        Paxos::LocalPeer::accept(PaxosServer::Quorum peers,
                                 Address address,
                                 Paxos::PaxosClient::Proposal const& p,
                                 Value const& value)
        {
          ELLE_TRACE_SCOPE("%s: accept at %s: %s",
                           *this, address, p);
          // FIXME: factor with validate in doughnut::Local::store
          std::shared_ptr<blocks::Block> block;
          if (value.is<std::shared_ptr<blocks::Block>>())
            block = value.get<std::shared_ptr<blocks::Block>>();
          if (block)
          {
            ELLE_DEBUG("validate block")
              if (auto res = block->validate()); else
                throw ValidationFailed(res.reason());
          }
          auto& decision = this->_addresses.at(address);
          auto& paxos = decision.paxos;
          if (block)
            if (auto highest = paxos.highest_accepted_value())
            {
              auto& val = highest->value.get<std::shared_ptr<blocks::Block>>();
              auto valres = val->validate(*block);
              if (!valres)
                throw Conflict("peer validation failed", block->clone());
            }
          auto res = paxos.accept(std::move(peers), p, value);
          {
            ELLE_DEBUG_SCOPE("store accepted paxos");
            this->storage()->set(
              address,
              elle::serialization::binary::serialize(BlockOrPaxos(&decision)),
              true, true);
          }
          if (block)
            on_store(*block, STORE_ANY);
          return std::move(res);
        }

        void
        Paxos::LocalPeer::_register_rpcs(RPCServer& rpcs)
        {
          Local::_register_rpcs(rpcs);
          namespace ph = std::placeholders;
          rpcs.add(
            "propose",
            std::function<
            boost::optional<Paxos::PaxosClient::Accepted>(
              PaxosServer::Quorum, Address,
              Paxos::PaxosClient::Proposal const&)>
            (std::bind(&LocalPeer::propose,
                       this, ph::_1, ph::_2, ph::_3)));
          rpcs.add(
            "accept",
            std::function<
            Paxos::PaxosClient::Proposal(
              PaxosServer::Quorum, Address,
              Paxos::PaxosClient::Proposal const& p,
              Value const& value)>
            (std::bind(&LocalPeer::accept,
                       this, ph::_1, ph::_2, ph::_3, ph::_4)));
          rpcs.add(
            "fetch_paxos",
            std::function<std::pair<PaxosServer::Quorum,
                                   std::unique_ptr<Paxos::PaxosClient::Accepted>>
                                   (Address)>
            (std::bind(&LocalPeer::_fetch_paxos,
                       this, ph::_1)));
        }

        template <typename T>
        T&
        unconst(T const& v)
        {
          return const_cast<T&>(v);
        }

        static
        Paxos::PaxosClient::Peers
        lookup_nodes(Doughnut& dht,
                     Paxos::PaxosServer::Quorum const& q,
                     Address address)
        {
          Paxos::PaxosClient::Peers res;
          for (auto member: dht.overlay()->lookup_nodes(q))
          {
            res.push_back(
              elle::make_unique<consensus::Peer>(
                std::move(member), address));
          }
          return res;
        }

        std::pair<Paxos::PaxosServer::Quorum,
                  std::unique_ptr<Paxos::PaxosClient::Accepted>>
        Paxos::LocalPeer::_fetch_paxos(Address address)
        {
          auto decision = this->_addresses.find(address);
           if (decision == this->_addresses.end())
            try
            {
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto data =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  this->storage()->get(address), true, context);
              if (data.block)
              {
                ELLE_DEBUG("loaded immutable block from storage");
                return std::make_pair(
                  PaxosServer::Quorum(),
                  elle::make_unique<PaxosClient::Accepted>(
                    PaxosClient::Proposal(-1, -1, this->doughnut().id()),
                    std::shared_ptr<blocks::Block>(data.block)));
              }
              else
              {
                ELLE_DEBUG("loaded mutable block from storage");
                decision = const_cast<LocalPeer*>(this)->_addresses.emplace(
                  address, std::move(*data.paxos)).first;
              }
            }
            catch (storage::MissingKey const& e)
            {
              ELLE_TRACE("missing block %x", address);
              throw MissingBlock(e.key());
            }
          else
            ELLE_DEBUG("mutable block already loaded");
          auto& paxos = decision->second.paxos;
          auto highest = paxos.highest_accepted_value();
          if (!highest)
            throw MissingBlock(address);
          return std::make_pair(
            paxos.quorum(),
            elle::make_unique<PaxosClient::Accepted>(*highest));
        }

        std::unique_ptr<blocks::Block>
        Paxos::LocalPeer::_fetch(Address address,
                                 boost::optional<int> local_version) const
        {
          if (this->doughnut().version() >= elle::Version(0, 5, 0))
            throw elle::Error("_fetch called on PAXOS consensus");
          // pre-0.5.0 RPC left for backward compatibility
          ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto data =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  this->storage()->get(address), true, context);
              if (data.block)
              {
                ELLE_DEBUG("loaded immutable block from storage");
                return std::unique_ptr<blocks::Block>(data.block);
              }
              else
              {
                ELLE_DEBUG("loaded mutable block from storage");
                decision = const_cast<LocalPeer*>(this)->_addresses.emplace(
                  address, std::move(*data.paxos)).first;
              }
            }
            catch (storage::MissingKey const& e)
            {
              ELLE_TRACE("missing block %x", address);
              throw MissingBlock(e.key());
            }
          else
            ELLE_DEBUG("mutable block already loaded");
          auto& paxos = decision->second.paxos;
          if (auto highest = paxos.highest_accepted_value())
          {
            auto version = highest->proposal.version;
            if (decision->second.chosen == version
              && highest->value.is<std::shared_ptr<blocks::Block>>())
            {
              ELLE_DEBUG("return already chosen mutable block");
              return highest->value.get<std::shared_ptr<blocks::Block>>()
                ->clone();
            }
            else
            {
              ELLE_TRACE_SCOPE(
                "finalize running Paxos for version %s (last chosen %s)"
                , version, decision->second.chosen);
              auto block = highest->value;
              Paxos::PaxosClient::Peers peers =
                lookup_nodes(
                  this->doughnut(), paxos.quorum(), address);
              if (peers.empty())
                throw elle::Error(
                  elle::sprintf("No peer available for fetch %x", address));
              Paxos::PaxosClient client(uid(this->doughnut().keys().K()),
                                        std::move(peers));
              auto chosen = client.choose(version, block);
              // FIXME: factor with the end of doughnut::Local::store
              ELLE_DEBUG("%s: store chosen block", *this)
              unconst(decision->second).chosen = version;
              {
                this->storage()->set(
                  address,
                  elle::serialization::binary::serialize(
                    BlockOrPaxos(const_cast<Decision*>(&decision->second))),
                  true, true);
              }
              // ELLE_ASSERT(block.unique());
              // FIXME: Don't clone, it's useless, find a way to steal
              // ownership from the shared_ptr.
              return block.get<std::shared_ptr<blocks::Block>>()->clone();
            }
          }
          else
          {
            ELLE_TRACE("%s: block has running Paxos but no value: %x",
                       *this, address);
            throw MissingBlock(address);
          }
        }

        void
        Paxos::LocalPeer::store(blocks::Block const& block, StoreMode mode)
        {
          ELLE_TRACE_SCOPE("%s: store %f", *this, block);
          ELLE_DEBUG("%s: validate block", *this)
            if (auto res = block.validate()); else
              throw ValidationFailed(res.reason());
          if (!dynamic_cast<blocks::ImmutableBlock const*>(&block))
            throw ValidationFailed("bypassing Paxos for a non-immutable block");
          // validate with previous version
          try
          {
            auto previous_buffer = this->storage()->get(block.address());
            elle::IOStream s(previous_buffer.istreambuf());
            typename elle::serialization::binary::SerializerIn input(s);
            input.set_context<Doughnut*>(&this->doughnut());
            auto stored = input.deserialize<BlockOrPaxos>();
            elle::SafeFinally cleanup([&] {
                  delete stored.block;
                  delete stored.paxos;
            });
            if (!stored.block)
              ELLE_WARN("No block, cannot validate update");
            else
            {
              auto vr = stored.block->validate(block);
              if (!vr)
                if (vr.conflict())
                  throw Conflict(vr.reason(), stored.block->clone());
                else
                  throw ValidationFailed(vr.reason());
            }
          }
          catch (storage::MissingKey const&)
          {
          }
          elle::Buffer data =
            elle::serialization::binary::serialize(
              BlockOrPaxos(const_cast<blocks::Block*>(&block)));
          this->storage()->set(block.address(), data,
                              mode == STORE_ANY || mode == STORE_INSERT,
                              mode == STORE_ANY || mode == STORE_UPDATE);
          on_store(block, mode);
        }

        void
        Paxos::LocalPeer::remove(Address address, blocks::RemoveSignature rs)
        {
          if (this->doughnut().version() >= elle::Version(0, 4, 0))
          {
            auto it = this->_addresses.find(address);
            ELLE_TRACE("paxos::remove, known=%s", it != this->_addresses.end());
            if (it != this->_addresses.end())
            {
              auto& decision = this->_addresses.at(address);
              auto& paxos = decision.paxos;
              if (auto highest = paxos.highest_accepted_value())
              {
                auto& v = highest->value.get<std::shared_ptr<blocks::Block>>();
                auto valres = v->validate_remove(rs);
                ELLE_TRACE("mutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), v->clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
              else
                ELLE_WARN("No paxos accepted, cannot validate removal");
            }
            else
            { // immutable block
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              elle::SafeFinally cleanup([&] {
                  delete stored.block;
                  delete stored.paxos;
              });
              if (!stored.block)
                ELLE_WARN("No paxos and no block, cannot validate removal");
              else
              {
                auto previous = stored.block;
                auto valres = previous->validate_remove(rs);
                ELLE_TRACE("Immutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), previous->clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
            }
          }
          try
          {
            this->storage()->erase(address);
          }
          catch (storage::MissingKey const& k)
          {
            throw MissingBlock(k.key());
          }
          on_remove(address);
          this->_addresses.erase(address);
        }

        void
        Paxos::_store(std::unique_ptr<blocks::Block> inblock,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          ELLE_TRACE_SCOPE("%s: store %f", *this, *inblock);
          std::shared_ptr<blocks::Block> b(inblock.release());
          ELLE_ASSERT(b);
          overlay::Operation op;
          switch (mode)
          {
            case STORE_ANY:
              op = overlay::OP_INSERT_OR_UPDATE;
              break;
            case STORE_INSERT:
              op = overlay::OP_INSERT;
              break;
            case STORE_UPDATE:
              op = overlay::OP_UPDATE;
              break;
            default:
              elle::unreachable();
          }
          auto owners = this->_owners(b->address(), this->_factor, op);
          if (dynamic_cast<blocks::MutableBlock*>(b.get()))
          {
            // FIXME: this voids the whole "query on the fly" optimisation
            Paxos::PaxosClient::Peers peers;
            PaxosServer::Quorum peers_id;
            // FIXME: This void the "query on the fly" optimization as it
            // forces resolution of all peers to get their idea. Any other
            // way ?
            for (auto peer: owners)
            {
              peers_id.insert(peer->id());
              peers.push_back(
                elle::make_unique<Peer>(peer, b->address()));
            }
            if (peers.empty())
              throw elle::Error(
                elle::sprintf("No peer available for store %x", b->address()));
            // FIXME: client is persisted on conflict resolution, hence the
            // round number is kept and won't start at 0.
            while (true)
            {
              try
              {
                Paxos::PaxosClient client(
                  uid(this->doughnut().keys().K()), std::move(peers));
                while (true)
                {
                  auto version =
                    dynamic_cast<blocks::MutableBlock*>(b.get())->version();
                  boost::optional<Paxos::PaxosServer::Accepted> chosen;
                  ELLE_DEBUG("run Paxos for version %s", version)
                    chosen = client.choose(version, b);
                  if (chosen)
                  {
                    if (chosen->value.is<PaxosServer::Quorum>())
                    {
                      auto const& q = chosen->value.get<PaxosServer::Quorum>();
                      ELLE_DEBUG_SCOPE("Paxos elected another quorum: %s", q);
                      throw Paxos::PaxosServer::WrongQuorum(
                        q, peers_id, version, chosen->proposal);
                    }
                    else
                    {
                      auto block =
                        chosen->value.get<std::shared_ptr<blocks::Block>>();
                      if (*block == *b)
                      {
                        ELLE_DEBUG("Paxos chose another block version, which "
                                   "happens to be the same as ours");
                        break;
                      }
                      ELLE_DEBUG_SCOPE("Paxos chose another block value");
                      if (resolver)
                      {
                        ELLE_TRACE_SCOPE(
                          "chosen block differs, run conflict resolution");
                        auto resolved = (*resolver)(*b, *block, mode);
                        if (resolved)
                        {
                          ELLE_DEBUG_SCOPE("seal resolved block");
                          resolved->seal();
                          b.reset(resolved.release());
                        }
                        else
                        {
                          ELLE_TRACE("resolution failed");
                          // FIXME: useless clone, find a way to steal ownership
                          throw infinit::model::doughnut::Conflict(
                            "Paxos chose a different value", block->clone());
                        }
                      }
                      else
                      {
                        ELLE_TRACE("chosen block differs, signal conflict");
                        // FIXME: useless clone, find a way to steal ownership
                        throw infinit::model::doughnut::Conflict(
                          "Paxos chose a different value",
                          block->clone());
                      }
                    }
                  }
                  else
                    break;
                }
              }
              catch (Paxos::PaxosServer::WrongQuorum const& e)
              {
                ELLE_TRACE("%s: %s instead of %s",
                           e.what(), e.effective(), e.expected());
                peers = lookup_nodes(
                  this->doughnut(), e.expected(), b->address());
                peers_id.clear();
                for (auto const& peer: peers)
                  peers_id.insert(static_cast<Peer&>(*peer).id());
                continue;
              }
              break;
            }
          }
          else
          {
            elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
            {
              for (auto owner: owners)
                scope.run_background(
                  "store block",
                  [&, owner] { owner->store(*b, STORE_ANY); });
              reactor::wait(scope);
            };
          }
        }

        class Hit
        {
        public:
          Hit(Address node)
            : _node(node)
            , _quorum()
            , _accepted()
          {}

          Hit(Address node,
              std::pair<Paxos::PaxosServer::Quorum,
                        std::unique_ptr<Paxos::PaxosClient::Accepted>> data)
            : _node(node)
            , _quorum(std::move(data.first))
            , _accepted(data.second ?
                        std::move(*data.second) :
                        boost::optional<Paxos::PaxosClient::Accepted>())
          {}

          Hit()
          {}

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("quorum", this->_quorum);
            s.serialize("accepted", this->_accepted);
          }

          ELLE_ATTRIBUTE_R(model::Address, node);
          ELLE_ATTRIBUTE_R(boost::optional<Paxos::PaxosServer::Quorum>,
                           quorum);
          ELLE_ATTRIBUTE_R(boost::optional<Paxos::PaxosClient::Accepted>,
                           accepted);
        };

        struct Paxos::_Details
        {
          static
          std::vector<Hit>
          _multifetch_paxos(
            Paxos& self,
            Address address,
            std::function<bool (blocks::Block const&)> const& found =
              [] (blocks::Block const&) { return true; }
            )
          {
            PaxosServer::Quorum quorum;
            while (true)
            {
              auto peers = quorum.empty() ?
                self._owners(address, self._factor, overlay::OP_FETCH) :
                self.doughnut().overlay()->lookup_nodes(quorum);
              PaxosServer::Quorum my_quorum;
              std::vector<Hit> hits;
              for (auto peer: peers)
              {
                ELLE_DEBUG_SCOPE("contact %s", peer->id());
                my_quorum.emplace(peer->id());
                try
                {
                  Hit hit;
                  if (auto l = dynamic_cast<Paxos::LocalPeer*>(peer.get()))
                    hit = Hit(peer->id(), l->_fetch_paxos(address));
                  else if (auto r = dynamic_cast<Paxos::RemotePeer*>(peer.get()))
                    hit = Hit(peer->id(), r->_fetch_paxos(address));
                  else if (dynamic_cast<DummyPeer*>(peer.get()))
                    hit = Hit(peer->id());
                  else
                    ELLE_ABORT("invalid paxos peer: %s", *peer);
                  if (hit.accepted())
                  {
                    auto block = hit.accepted()->value.
                      get<std::shared_ptr<blocks::Block>>();
                    if (!found(*block))
                      return hits;
                    hits.push_back(std::move(hit));
                  }
                }
                catch (reactor::network::Exception const& e)
                {
                  ELLE_DEBUG("network exception on %s: %s", peer, e);
                }
              }
              ELLE_TRACE("got %s hits", hits.size());
              if (hits.empty())
                throw MissingBlock(address);
              // Reverse sort
              std::sort(
                hits.begin(), hits.end(),
                [] (Hit const& a, Hit const& b)
                {
                  if (!b.accepted())
                    return true;
                  if (!a.accepted())
                    return false;
                  return a.accepted()->proposal > b.accepted()->proposal;
                });
              if (!hits.front().quorum())
                // Nobody has any value
                return hits;
              quorum = hits.front().quorum().get();
              if (quorum == my_quorum)
                return hits;
              else
                ELLE_DEBUG("outdated quorum, most recent: %s", quorum);
            }
          }
        };

        std::unique_ptr<blocks::Block>
        Paxos::_fetch(Address address, boost::optional<int> local_version)
        {
          if (this->doughnut().version() < elle::Version(0, 5, 0))
          {
            auto peers =
              this->_owners(address, this->_factor, overlay::OP_FETCH);
            return fetch_from_members(peers, address, std::move(local_version));
          }
          std::unique_ptr<blocks::Block> immutable;
          auto found = [&] (blocks::Block const& b)
            {
              if (auto ib = dynamic_cast<blocks::ImmutableBlock const*>(&b))
              {
                immutable = ib->clone();
                return false;
              }
              else
                return true;
          };
          auto hits = _Details::_multifetch_paxos(*this, address, found);
          if (immutable)
            return immutable;
          auto quorum = hits.front().quorum().get();
          auto proposal = hits.front().accepted()->proposal;
          int count = 0;
          for (auto const& a: hits)
          {
            if (a.quorum().get() != quorum)
              throw elle::Error("different quorums in quorum"); // FIXME
            if (a.accepted()->proposal != proposal)
              throw elle::Error("different acceptations in quorum"); // FIXME
            if (++count > signed(quorum.size()) / 2)
            {
              auto block = a.accepted()->
                value.get<std::shared_ptr<blocks::Block>>();
              auto mblock = dynamic_cast<blocks::MutableBlock*>(block.get());
              if (mblock && local_version && mblock->version() == *local_version)
                return {};
              return block->clone();
            }
          }
          ELLE_TRACE("too few peers: %s", hits.size());
          throw athena::paxos::TooFewPeers(hits.size(), quorum.size());
        }

        void
        Paxos::_remove(Address address, blocks::RemoveSignature rs)
        {
          this->remove_many(address, std::move(rs), _factor);
        }

        Paxos::LocalPeer::Decision::Decision(PaxosServer paxos)
          : chosen(-1)
          , paxos(std::move(paxos))
        {}

        Paxos::LocalPeer::Decision::Decision(
          elle::serialization::SerializerIn& s)
          : chosen(s.deserialize<int>("chosen"))
          , paxos(s.deserialize<PaxosServer>("paxos"))
        {}

        void
        Paxos::LocalPeer::Decision::serialize(
          elle::serialization::Serializer& s)
        {
          s.serialize("chosen", this->chosen);
          s.serialize("paxos", this->paxos);
        }

        /*-----.
        | Stat |
        `-----*/

        typedef std::unordered_map<std::string, boost::optional<Hit>> Hits;

        class PaxosStat
          : public Consensus::Stat
        {
        public:
          PaxosStat(Hits hits)
            : _hits(std::move(hits))
          {}

          virtual
          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("hits", this->_hits);
          }

          ELLE_ATTRIBUTE_R(Hits, hits);
        };

        std::unique_ptr<Consensus::Stat>
        Paxos::stat(Address const& address)
        {
          ELLE_TRACE_SCOPE("%s: stat %s", *this, address);
          // ELLE_ASSERT_GTE(this->doughnut().version(), elle::Version(0, 5, 0));
          auto hits = _Details::_multifetch_paxos(*this, address);
          auto peers = this->_owners(address, this->_factor, overlay::OP_FETCH);
          Hits stat_hits;
          for (auto& hit: hits)
          {
            auto node = elle::sprintf("%s", hit.node());
            stat_hits.emplace(node, std::move(hit));
          }
          return elle::make_unique<PaxosStat>(std::move(stat_hits));
        }

        /*--------------.
        | Configuration |
        `--------------*/

        Paxos::Configuration::Configuration(int replication_factor)
          : consensus::Configuration()
          , _replication_factor(replication_factor)
        {}

        std::unique_ptr<Consensus>
        Paxos::Configuration::make(model::doughnut::Doughnut& dht)
        {
          return elle::make_unique<Paxos>(dht, this->_replication_factor);
        }

        Paxos::Configuration::Configuration(
          elle::serialization::SerializerIn& s)
        {
          this->serialize(s);
        }

        void
        Paxos::Configuration::serialize(elle::serialization::Serializer& s)
        {
          consensus::Configuration::serialize(s);
          s.serialize("replication-factor", this->_replication_factor);
        }

        static const elle::serialization::Hierarchy<Configuration>::
        Register<Paxos::Configuration> _register_Configuration("paxos");
      }
    }
  }
}

namespace athena
{
  namespace paxos
  {
    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<TooFewPeers> _register_serialization;
  }
}
