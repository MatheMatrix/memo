#include <elle/IOStream.hh>
#include <elle/With.hh>
#include <elle/Buffer.hh>
#include <elle/cast.hh>
#include <elle/test.hh>

#include <elle/protocol/exceptions.hh>
#include <elle/protocol/Serializer.hh>
#include <elle/protocol/ChanneledStream.hh>
#include <elle/protocol/Channel.hh>

#include <elle/cryptography/random.hh>

#include <elle/reactor/asio.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/network/Error.hh>
#include <elle/reactor/network/TCPServer.hh>
#include <elle/reactor/network/TCPSocket.hh>
#include <elle/reactor/scheduler.hh>


ELLE_LOG_COMPONENT("elle.protocol.test");

static const std::vector<uint32_t> to_test = {
  std::numeric_limits<uint32_t>::min(),
  std::numeric_limits<uint32_t>::max(),
  std::numeric_limits<uint8_t>::max() + 1,
  std::numeric_limits<uint8_t>::max() - 1,
  std::numeric_limits<uint16_t>::max() + 1,
  std::numeric_limits<uint16_t>::max() - 1
};

static
void
_buffer(elle::Version const& version)
{
  elle::Buffer x;
  for (auto i: to_test)
    elle::protocol::Stream::uint32_put(x, i, version);
  for (auto i: to_test)
    BOOST_CHECK_EQUAL(elle::protocol::Stream::uint32_get(x, version), i);
  BOOST_CHECK_EQUAL(x.size(), 0);
}

ELLE_TEST_SCHEDULED(buffer)
{
  for (auto version: {elle::Version{0, 2, 0}, elle::Version{0, 3, 0}})
    _buffer(version);
}

static
void
_stream(elle::Version const& version)
{
  elle::Buffer x;
  {
    elle::IOStream output(x.ostreambuf());
    for (auto i: to_test)
      elle::protocol::Stream::uint32_put(output, i, version);
  }
  // XXX.
  {
    elle::IOStream input(x.istreambuf());
    for (auto i: to_test)
      BOOST_CHECK_EQUAL(elle::protocol::Stream::uint32_get(input, version), i);
  }
  // XXX.
  {
    {
      elle::IOStream input(x.istreambuf());
      BOOST_CHECK_EQUAL(
        elle::protocol::Stream::uint32_get(input, version), to_test[0]);
      BOOST_CHECK_EQUAL(
        elle::protocol::Stream::uint32_get(input, version), to_test[1]);
    }
    {
      elle::IOStream input(x.istreambuf());
      BOOST_CHECK_EQUAL(
        elle::protocol::Stream::uint32_get(input, version), to_test[0]);
      BOOST_CHECK_EQUAL(
        elle::protocol::Stream::uint32_get(input, version), to_test[1]);
    }
  }
}

ELLE_TEST_SCHEDULED(stream)
{
  for (auto version: {elle::Version{0, 2, 0}, elle::Version{0, 3, 0}})
    _stream(version);
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(buffer), 0, valgrind(6, 10));
  suite.add(BOOST_TEST_CASE(stream), 0, valgrind(6, 10));
}
