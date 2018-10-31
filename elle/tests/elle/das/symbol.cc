#include <string>

#include <elle/test.hh>

#include <elle/das/Symbol.hh>

namespace symbols
{
  ELLE_DAS_SYMBOL(foo);
  ELLE_DAS_SYMBOL(bar);
  ELLE_DAS_SYMBOL(baz);
  ELLE_DAS_SYMBOL(quux);
}

struct S
{
  S()
    : foo(0)
  {}
  int foo;
  std::string bar;
};

struct Sub
  : public S
{};

template <typename S, typename T>
auto
no_such_attribute(T const& o, int i)
  -> std::enable_if_t<sizeof(S::attr_get(o)) >= 0, bool>
{
  return false;
}

template <typename S, typename T>
bool
no_such_attribute(T const& o, ...)
{
  return true;
}

static
void
attributes()
{
  S s;
  BOOST_CHECK_EQUAL(symbols::foo.attr_get(s), 0);
  static_assert(
    std::is_same<symbols::Symbol_foo::attr_type<S>, int>::value,
    "wrong attribute type");
  BOOST_CHECK_EQUAL(symbols::foo.attr_get(s)++, 0);
  BOOST_CHECK_EQUAL(symbols::foo.attr_get(s), 1);
  BOOST_CHECK_EQUAL(symbols::bar.attr_get(static_cast<const S&>(s)), "");
  static_assert(
    std::is_same<symbols::Symbol_bar::attr_type<S>, std::string>::value,
    "wrong attribute type");
  BOOST_CHECK(decltype(symbols::foo)::attr_has<S>());
  BOOST_CHECK(!no_such_attribute<decltype(symbols::foo)>(s, 0));
  BOOST_CHECK(!decltype(symbols::baz)::attr_has<S>());
  BOOST_CHECK(no_such_attribute<decltype(symbols::baz)>(s, 0));
  Sub sub;
  BOOST_CHECK_EQUAL(symbols::foo.attr_get(sub), 0);
}

struct M
{
  int
  foo(int i = 41)
  {
    return i + 1;
  }

  bool
  bar(char)
  {
    return true;
  }

  bool
  bar(double)
  {
    return false;
  }
};

static
void
methods()
{
  static_assert(symbols::foo.method_has<M>(), "method_has error");
  static_assert(symbols::foo.method_has<M, int>(), "method_has error");
  static_assert(!symbols::foo.method_has<M, int, int>(), "method_has error");
  static_assert(
    std::is_same<decltype(symbols::foo)::method_type<M>, int>::value,
    "method_type error");
  static_assert(
    std::is_same<decltype(symbols::foo)::method_type<M, int>, int>::value,
    "method_type error");
  M m;
  BOOST_CHECK_EQUAL(symbols::foo.method_call(m), 42);
  BOOST_CHECK_EQUAL(symbols::foo.method_call(m, 5), 6);
  BOOST_CHECK(symbols::bar.method_call(m, 'c'));
  BOOST_CHECK(!symbols::bar.method_call(m, 0.));
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(attributes), 0, valgrind(1));
  suite.add(BOOST_TEST_CASE(methods), 0, valgrind(1));
}
