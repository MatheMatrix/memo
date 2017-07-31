#include <elle/test.hh>
#include <elle/filesystem/TemporaryDirectory.hh>

#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/Cache.hh>

#include "DummyDoughnut.hh"
#include "InstrumentedConsensus.hh"

ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Cache.test");

namespace dht = memo::model::doughnut;

struct Recipe
{
  template <typename ... Args>
  Recipe(Args&& ... args)
    : Recipe(std::make_unique<InstrumentedConsensus>(dht),
             std::forward<Args>(args)...)
  {}

  template <typename ... Args>
  Recipe(std::unique_ptr<InstrumentedConsensus> c, Args&& ... args)
    : dht()
    , instrument(*c)
    , cache(std::move(c), std::forward<Args>(args)...)
  {}

  DummyDoughnut dht;
  InstrumentedConsensus& instrument;
  dht::consensus::Cache cache;
};

ELLE_TEST_SCHEDULED(memory)
{
  Recipe r;
  auto addr = memo::model::Address::random();
  BOOST_CHECK_THROW(r.cache.fetch(addr), memo::model::MissingBlock);
  auto okb = r.dht.make_block<memo::model::blocks::MutableBlock>(
    elle::Buffer("data", 4));
  okb->seal(1);
  r.instrument.add(*okb);
  BOOST_CHECK_EQUAL(r.cache.fetch(okb->address())->data(), okb->data());
  r.instrument.fetched().connect(
    [] (memo::model::Address const& addr)
    {
      BOOST_FAIL(elle::sprintf("block %f should have been cached", addr));
    });
  BOOST_CHECK_EQUAL(r.cache.fetch(okb->address())->data(), okb->data());
  BOOST_CHECK(!r.cache.fetch(okb->address(), 1));
  BOOST_CHECK(r.cache.fetch(okb->address(), 0));
}

ELLE_TEST_SCHEDULED(disk)
{
  elle::filesystem::TemporaryDirectory tmp;
  std::unique_ptr<memo::model::blocks::Block> chb;
  ELLE_LOG("create and fetch CHB")
  {
    Recipe r(boost::optional<int>(),
             boost::optional<std::chrono::seconds>(),
             boost::optional<std::chrono::seconds>(),
             tmp.path());
    chb = r.dht.make_block<memo::model::blocks::ImmutableBlock>(
      elle::Buffer("data", 4));
    r.instrument.add(*chb);
    BOOST_CHECK_EQUAL(r.cache.fetch(chb->address())->data(), chb->data());
    r.instrument.fetched().connect(
      [] (memo::model::Address const& addr)
      {
        BOOST_FAIL(elle::sprintf("block %f should have been cached", addr));
    });
    BOOST_CHECK_EQUAL(r.cache.fetch(chb->address())->data(), chb->data());
  }
  ELLE_LOG("reload CHB from disk cache")
  {
    Recipe r(boost::optional<int>(),
             boost::optional<std::chrono::seconds>(),
             boost::optional<std::chrono::seconds>(),
             tmp.path());
    r.instrument.fetched().connect(
      [] (memo::model::Address const& addr)
      {
        BOOST_FAIL(elle::sprintf("block %f should have been cached", addr));
    });
    BOOST_CHECK_EQUAL(r.cache.fetch(chb->address())->data(), chb->data());
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(memory), 0, valgrind(1));
  suite.add(BOOST_TEST_CASE(disk), 0, valgrind(1));
}
