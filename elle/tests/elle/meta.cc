#include <elle/meta.hh>
#include <elle/test.hh>

ELLE_LOG_COMPONENT("elle.meta.test");

using namespace elle::meta;

namespace list
{
  namespace size
  {
    using l1 = List<int, float, double>;
    using l2 = List<int, float, double, l1>;
    static_assert(l1::size == 3, "l1::size should be 3");
    static_assert(l1::size - l2::size == -1,
                  "Difference of sizes should be -1");
  }

  namespace apply
  {
    template <typename ... Args>
    struct res
    {};

    using l = List<int, float, double>;
    static_assert(std::is_same<l::apply<res>,
                               res<int, float, double>>::value,
                  "list::apply yielded the wrong type");
    static_assert(std::is_same<l::apply<res, void>,
                               res<void, int, float, double>>::value,
                  "list::apply yielded the wrong type");
  }

  namespace filter
  {
    using l = List<int, std::string, void, float*>;
    using res = l::filter<std::is_scalar>;
    static_assert(std::is_same<res, List<int, float*>>::value,
                  "list::filter yielded the wrong type");
  }

  namespace index_of
  {
    using l = List<int, float, double>;
    static_assert(l::index_of<int>::value == 0,
                  "list::index_of yielded the wrong index");
    static_assert(l::index_of<float>::value == 1,
                  "list::index_of yielded the wrong index");
    static_assert(l::index_of<double>::value == 2,
                  "list::index_of yielded the wrong index");
  }

  namespace map
  {
    using l = List<int, float, std::string>;
    template <typename T, typename Check>
    struct intify
    {
      static_assert(std::is_same<Check, double>::value, "");
      using type = int;
    };
    static_assert(
      std::is_same<typename l::map<intify, double>::type,
                   List<int, int, int>>::value,
      "list::map yielded the wrong type");
    template <typename T>
    struct pointerize
    {
      using type = T*;
    };
    static_assert(
      std::is_same<l::map<pointerize>::type,
                   List<int*, float*, std::string*>>::value,
      "list::map yielded the wrong type");
  }

  namespace prepend
  {
    using l = List<int, void>;
    static_assert(std::is_same<l::prepend<float>::type,
                               List<float, int, void>>::value,
                  "list::prepend yielded the wrong type");
    static_assert(std::is_same<l::prepend<float, float, int>::type,
                  // List<float, float, int, int, void>>::value,
                  List<float, float, int, int, void>>::value,
                  "list::prepend yielded the wrong type");
  }

  namespace prepend_list
  {
    using l1 = List<int, void>;
    using l2 = List<float, int>;
    static_assert(std::is_same<l1::prepend_list<l2>::type,
                  List<float, int, int, void>>::value,
                  "list::prepend_list yielded the wrong type");
  }

  namespace append
  {
    using l = List<int, void>;
    static_assert(std::is_same<l::append<float>::type,
                  List<int, void, float>>::value,
                  "list::append yielded the wrong type");
    static_assert(std::is_same<l::append<float, int>::type,
                  List<int, void, float, int>>::value,
                  "list::append yielded the wrong type");
  }

  namespace append_list
  {
    using l1 = List<int, void>;
    using l2 = List<float, int>;
    static_assert(std::is_same<l1::append_list<l2>::type,
                  List<int, void, float, int>>::value,
                  "list::prepend_list yielded the wrong type");
  }

  namespace tail
  {
    using l = List<int, float, void>;
    static_assert(
      std::is_same<l::tail<>::type, List<float, void>>::value,
      "list::tail yielded the wrong type");
  }

  namespace head
  {
    using l = List<int, float, void>;
    static_assert(
      std::is_same<typename l::head<>::type, int>::value,
      "list::head yielded the wrong type");
  }
}

template <typename T, typename Token>
struct print_type
{
  using type = std::string;
  static
  type
  value(std::string const& prefix, std::string const& suffix)
  {
    return prefix + elle::type_info<Token>().name() + ":" +
      elle::type_info<T>().name() + suffix;
  }
};

template <typename T>
struct get_foo
{
  using type = decltype(T::foo)&;
  static
  type
  value()
  {
    return T::foo;
  }
};

struct Bar
{
  static int foo;
};
int Bar::foo = 1;

struct Baz
{
  static char foo;
};
char Baz::foo = 'a';


static bool bar = false;
static bool baz = false;

template <typename T>
struct order;

template <>
struct order<Bar>
{
  using type = int;
  static
  type
  value()
  {
    BOOST_CHECK(!bar);
    BOOST_CHECK(!baz);
    bar = true;
    return 0;
  }
};

template <>
struct order<Baz>
{
  using type = int;
  static
  type
  value()
  {
    BOOST_CHECK(bar);
    BOOST_CHECK(!baz);
    baz = true;
    return 0;
  }
};

static
void
map()
{
  {
    using l = List<int, float, char>;
    using map = l::map<print_type, int>;
    static_assert(
      std::is_same<map::type,
                   List<std::string, std::string, std::string>>::value,
      "list::map yielded the wrong type");
    BOOST_CHECK_EQUAL(
      map::value("<", ">"),
      std::make_tuple("<int:int>", "<int:float>", "<int:char>"));
  }
  {
    using l = List<Bar, Baz>;
    using map = l::map<get_foo>;
    static_assert(
      std::is_same<map::type, List<int&, char&>>::value,
      "list::map yielded the wrong type");
    auto res = map::value();
    BOOST_CHECK_EQUAL(std::get<0>(res), 1);
    BOOST_CHECK_EQUAL(std::get<1>(res), 'a');
    Bar::foo++;
    Baz::foo = 'b';
    BOOST_CHECK_EQUAL(std::get<0>(res), 2);
    BOOST_CHECK_EQUAL(std::get<1>(res), 'b');
  }
  ELLE_LOG("check evaluation order")
  {
    using l = List<Bar, Baz>;
    using map = l::map<order>;
    map::value();
  }
}

ELLE_TEST_SUITE()
{
  auto& master = boost::unit_test::framework::master_test_suite();
  master.add(BOOST_TEST_CASE(map));
}
