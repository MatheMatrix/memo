#pragma once

#include <elle/Printable.hh>
#include <elle/athena/paxos/Server.hh>
#include <elle/attribute.hh>
#include <elle/reactor/Barrier.hh>

#include <elle/das/Symbol.hh>
#include <elle/das/tuple.hh>

#include <elle/athena/paxos/symbols.hh>

namespace elle
{
  namespace athena
  {
    namespace paxos
    {
      /// A client of the Paxos consensus algorithm.
      ///
      /// For more details, on both the terminology and the algorithm:
      /// https://en.wikipedia.org/wiki/Paxos_(computer_science)
      template <typename T, typename Version, typename ClientId>
      class Client
        : public elle::Printable
      {
        /*------.
        | Types |
        `------*/
      public:
        using Self = Client;
        using Server = paxos::Server<T, Version, ClientId>;
        using Accepted = typename Server::Accepted;
        using Proposal = typename Server::Proposal;
        using Quorum = typename Server::Quorum;
        using Response = typename Server::Response;
        using Value = typename Server::Value;

        /*-----.
        | Peer |
        `-----*/
      public:
        /// The default interface of a Peer.
        class Peer
          : public elle::Printable
        {
        public:
          /// Create a peer with \a given id.
          ///
          /// @param id The peer id.
          Peer(ClientId id);
          virtual
          ~Peer() = default;
          /// Send \a Proposal to \a Quorum.
          ///
          /// @param q The quorum the proposal is sent to.
          /// @param p The proposal.
          /// @returns An optional acceptation.
          virtual
          Response
          propose(Quorum const& q, Proposal const& p) = 0;
          /// Tell the Quorum you accept \a proposal.
          ///
          /// @param q The quorum the proposal is sent to.
          /// @param p The proposal.
          /// @param value The value you agreed for.
          virtual
          Proposal
          accept(Quorum const& q, Proposal const& p, Value const& value) = 0;
          /// Confirm \a proposal.
          ///
          /// @param q The quorum the proposal is sent to.
          /// @param p The proposal.
          virtual
          void
          confirm(Quorum const& q, Proposal const& p) = 0;
          /// Get the Accepted proposal.
          ///
          /// @param q The quorum.
          virtual
          boost::optional<Accepted>
          get(Quorum const& q) = 0;
          ELLE_ATTRIBUTE_R(ClientId, id);
          /*----------.
          | Printable |
          `----------*/
        public:
          void
          print(std::ostream& output) const override;
        };
        using Peers = std::vector<std::unique_ptr<Peer>>;

        /*-------------.
        | Construction |
        `-------------*/
      public:
        template <typename Peers>
        Client(ClientId id, Peers&& peers);
        /// Change the peers.
        template <typename Peers>
        void
        peers(Peers&& peers);
        ELLE_ATTRIBUTE_R(ClientId, id);
        // FIXME: the W is there only for unit tests
        ELLE_ATTRIBUTE_RX(Peers, peers);
        ELLE_ATTRIBUTE_RW(bool, conflict_backoff);

        /*----------.
        | Consensus |
        `----------*/
      public:
        class Choice
        {
        public:
          Choice(Proposal proposal);
          Choice(Proposal proposal, Value value);
          explicit
          operator bool() const;
          Value const*
          operator ->() const;
          ELLE_ATTRIBUTE_R(Proposal, proposal);
          ELLE_ATTRIBUTE_R(bool, conflicted);
          ELLE_ATTRIBUTE_R((boost::optional<Value>), value);
        };

        /// Submit \a value as the chosen value.
        ///
        /// @param value The submitted value
        /// @returns The value that was chosen if not the one we submitted
        Choice
        choose(Value const& value);
        /// Submit \a value as the chosen value.
        ///
        /// @param value The submitted value
        /// @param version The version of the proposal.
        /// @returns The value that was chosen if not the one we submitted
        Choice
        choose(elle::_detail::attribute_r_t<Version> version,
               Value const& value);
        /// Get the latest chosen value.
        boost::optional<T>
        get();
        using State = das::tuple<
          decltype(value)::Formal<boost::optional<T>>,
          decltype(quorum)::Formal<Quorum>,
          decltype(proposal)::Formal<boost::optional<Proposal>>>;
        State
        state();
        ELLE_ATTRIBUTE(int, round);

      private:
        /// Check a majority of members where reached.
        ///
        /// @param q The quorum we consult.
        /// @param reached The number of member successfully consulted.
        /// @param raise The optional error to throw.
        /// @param reading Whether majority is floor or ceil of `n / 2`.
        /// @throws TooFewPeers or \a raise if a majority was not reached.
        void
        _check_headcount(Quorum const& q,
                         int reached,
                         std::exception_ptr weak_error,
                         bool reading) const;

        /*----------.
        | Printable |
        `----------*/
      public:
        void
        print(std::ostream& output) const override;
      };
    }
  }
}

#include <elle/athena/paxos/Client.hxx>
