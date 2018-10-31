#include <elle/log.hh>
#include <elle/test.hh>

#include <elle/reactor/Generator.hh>

ELLE_LOG_COMPONENT("elle.reactor.generator.test");

ELLE_TEST_SCHEDULED(empty)
{
  auto f = [] (elle::reactor::yielder<int> const&) {};
  for (int i: elle::reactor::generator<int>(f))
    BOOST_FAIL(elle::sprintf("empty generator yielded a value: %s", i));
}

ELLE_TEST_SCHEDULED(simple)
{
  std::vector<int> results({0, 1, 3});
  auto f = [&] (elle::reactor::yielder<int> const& yield)
    {
      for (auto i: results)
        yield(i);
    };
  auto it = begin(results);
  for (int i: elle::reactor::generator<int>(f))
    BOOST_CHECK_EQUAL(i, *(it++));
  BOOST_CHECK(it == results.end());
}

ELLE_TEST_SCHEDULED(move)
{
  auto f = [] (elle::reactor::yielder<std::unique_ptr<int>> const& yield)
    {
      yield(std::make_unique<int>(42));
    };
  bool seen = false;
  for (auto i: elle::reactor::generator<std::unique_ptr<int>>(f))
  {
    BOOST_CHECK(!seen);
    seen = true;
    BOOST_CHECK_EQUAL(*i, 42);
  }
  BOOST_CHECK(seen);
}

ELLE_TEST_SCHEDULED(interleave)
{
  using std::begin;
  using std::end;
  elle::reactor::Barrier sync;
  bool beacon = false;
  auto f = [&] (elle::reactor::yielder<bool> const& yield)
    {
      yield(false);
      elle::reactor::wait(sync);
      beacon = true;
      yield(true);
    };
  auto g = elle::reactor::generator<bool>(f);
  auto it = begin(g);
  BOOST_CHECK(it != end(g));
  BOOST_CHECK(!*it);
  BOOST_CHECK(!beacon);
  sync.open();
  ++it;
  BOOST_CHECK(it != end(g));
  BOOST_CHECK(*it);
  ++it;
  BOOST_CHECK(!(it != end(g)));
}

class Beacon
  : public elle::Error
{
public:
  Beacon()
    : elle::Error("beacon exception escaped")
  {}
};

ELLE_TEST_SCHEDULED(exception)
{
  auto f = [&] (elle::reactor::yielder<int> const& yield)
    {
      yield(0);
      yield(1);
      throw Beacon();
      yield(2);
    };
  int expected = 0;
  try
  {
    for (auto i: elle::reactor::generator<int>(f))
      BOOST_CHECK_EQUAL(i, expected++);
  }
  catch (Beacon const&)
  {
    BOOST_CHECK_EQUAL(expected, 2);
    return;
  }
  BOOST_FAIL("generator din't throw");
}

ELLE_TEST_SCHEDULED(destruct)
{
  auto f = [&] (elle::reactor::yielder<int> const& yield)
    {
      elle::reactor::yield();
      yield(0);
      elle::reactor::yield();
      yield(1);
      elle::reactor::yield();
      yield(2);
      elle::reactor::yield();
    };
  auto g = elle::reactor::generator<int>(f);
  auto it = g.begin();
  BOOST_CHECK(it != g.end());
  BOOST_CHECK_EQUAL(*it, 0);
}

ELLE_TEST_SUITE()
{
  auto& master = boost::unit_test::framework::master_test_suite();
  master.add(BOOST_TEST_CASE(empty));
  master.add(BOOST_TEST_CASE(simple));
  master.add(BOOST_TEST_CASE(move));
  master.add(BOOST_TEST_CASE(interleave));
  master.add(BOOST_TEST_CASE(exception));
  master.add(BOOST_TEST_CASE(destruct));
}
