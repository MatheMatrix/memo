#define ELLE_TEST_MODULE Exception

#include <boost/algorithm/string/predicate.hpp>

#include <elle/test.hh>
#include <elle/Exception.hh>

// On linux, the function cannot be static
void
thrower();

void
thrower()
{
  throw elle::Exception("test message");
}

BOOST_AUTO_TEST_CASE(ExceptionBacktrace)
{
  try
  {
    thrower();
    BOOST_CHECK(!"Shouldn't be there");
  }
  catch (elle::Exception& e)
  {
    BOOST_TEST(e.what() == "test message");
#if ELLE_HAVE_BACKTRACE
    BOOST_TEST(e.backtrace().frames().front().symbol == "thrower()");
#endif
  }
  catch (...)
  {
    BOOST_CHECK(!"Shouldn't be there");
  }
}

BOOST_AUTO_TEST_CASE(err)
{
  using boost::starts_with;
#define CHECK_THROW(Statement, Exception, Message)                      \
  try                                                                   \
  {                                                                     \
    BOOST_CHECK_THROW(Statement, Exception);                            \
    Statement;                                                          \
  }                                                                     \
  catch (Exception const& e)                                            \
    {                                                                   \
    if (!starts_with(e.what(), Message))                                \
      BOOST_ERROR(elle::print(                                          \
        "%r does not start with %r", e.what(), Message));               \
  }

  CHECK_THROW(elle::err("foo"), elle::Error, "foo");
  CHECK_THROW(elle::err("foo %s", 3), elle::Error, "foo 3");
  CHECK_THROW(elle::err("%s bar", 3), elle::Error, "3 bar");
  CHECK_THROW(elle::err("%s bar %s", 3, 3), elle::Error, "3 bar 3");
  CHECK_THROW(elle::err("%s", 3, 3), elle::Exception, "too many arguments");
  CHECK_THROW(elle::err("%s %s", 3), elle::Exception, "too few arguments");
  CHECK_THROW(elle::err("%s"), elle::Error, "%s");
}
