#pragma once

#include <memo/overlay/Overlay.hh>

namespace memo
{
  namespace overlay
  {
    namespace koordinate
    {
      /// An overlay that aggregates several underlying overlays.
      ///
      /// Koordinate lets a node serve several overlays for others to query,
      /// and forwards local requets to the first one.  Future evolutions may
      /// enable to leverage several backend overlays depending on policies.
      class Koordinate
        : public Overlay
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourselves.
        using Self = Koordinate;
        /// Parent class.
        using Super = memo::overlay::Overlay;
        /// Underlying overlay to run and forward requests to.
        using Backend = std::unique_ptr<Overlay>;
        /// Set of underlying overlays.
        using Backends = std::vector<Backend>;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /// Construct a Koordinate with the given backends.
        ///
        /// @arg   dht      The owning Doughnut.
        /// @arg   local    The local server, null if pure client.
        /// @arg   backends The underlying overlays.
        /// @throw elle::Error if @a backends is empty.
        Koordinate(model::doughnut::Doughnut* dht,
                   std::shared_ptr<memo::model::doughnut::Local> local,
                   Backends backends);
        /// Destruct a Koordinate.

        ~Koordinate() override;
        /// The underlying overlays.
        ELLE_ATTRIBUTE(Backends, backends);
      protected:
        void
        _cleanup() override;
      private:
        /// Check invariants.
        void
        _validate() const;

      /*------.
      | Peers |
      `------*/
      protected:

        void
        _discover(NodeLocations const& peers) override;
        bool
        _discovered(model::Address id) override;

      /*-------.
      | Lookup |
      `-------*/
      protected:
        MemberGenerator
        _allocate(model::Address address, int n) const override;

        LocationGenerator
        _lookup(std::vector<model::Address> const& addrs, int n) const override;

        MemberGenerator
        _lookup(model::Address address, int n, bool fast) const override;

        WeakMember
        _lookup_node(model::Address address) const override;

      /*-----------.
      | Monitoring |
      `-----------*/
      public:
        std::string
        type_name() const override;
        elle::json::Array
        peer_list() const override;
        elle::json::Object
        stats() const override;
      };
    }
  }
}
