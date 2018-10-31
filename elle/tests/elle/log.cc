#include <elle/log.hh>

#include <memory>

ELLE_LOG_COMPONENT("test.log");

struct L
{
  L()
  {
    ELLE_LOG_COMPONENT("test.log.Lctor");

    ELLE_TRACE(__PRETTY_FUNCTION__);
    ELLE_ERR("BIET");
    ELLE_LOG("BIET");
    ELLE_TRACE("BIET");
    ELLE_DEBUG("BIET");
    ELLE_DUMP("BIET");

    foo();
  }

  virtual
  ~L()
  {
    ELLE_LOG_COMPONENT("test.log.Ldtor");

    ELLE_TRACE(__PRETTY_FUNCTION__);
    ELLE_ERR("BIET");
    ELLE_LOG("BIET");
    ELLE_TRACE("BIET");
    ELLE_DEBUG("BIET");
    ELLE_DUMP("BIET");

    foo();
  }

public:
  virtual void foo() const { ELLE_TRACE("foooo"); }
  virtual void bar() const = 0;
};

void L::bar() const {}

struct LL: public L
{
  LL()
  {
    ELLE_LOG_COMPONENT("test.log.LLctor");

    ELLE_TRACE(__PRETTY_FUNCTION__);
    ELLE_ERR("BIET");
    ELLE_LOG("BIET");
    ELLE_TRACE("BIET");
    ELLE_DEBUG("BIET");
    ELLE_DUMP("BIET");

    foo();
    bar();
  }

  ~LL()
  {
    ELLE_LOG_COMPONENT("test.log.LLdtor");

    ELLE_TRACE(__PRETTY_FUNCTION__);
    ELLE_ERR("BIET");
    ELLE_LOG("BIET");
    ELLE_TRACE("BIET");
    ELLE_DEBUG("BIET");
    ELLE_DUMP("BIET");

    foo();
    L::foo();
    bar();
  }

  void foo() const override { ELLE_TRACE("foo"); }
  void bar() const override { ELLE_TRACE("bar"); }
};

static
LL const&
static_from_function()
{
  static LL ll{};

  return ll;
}

static
void
g()
{
  ELLE_WARN("g Me too!");
  ELLE_TRACE("g Me too!");
}

static
void
f()
{
  ELLE_WARN("f This is useful, yellow first!");
  ELLE_TRACE("f This is useful!");
  {
    ELLE_ERR("f This is useful inner, red first");
    ELLE_TRACE("f This is useful inner");
    g();
  }
}

static
void
s()
{
  ELLE_TRACE("s This is useful!");
  ELLE_WARN("s This is useful, yellow second!");
  {
    ELLE_TRACE("s This is useful inner");
    ELLE_ERR("s This is useful inner, red second");
    g();
  }
}

static
void
nested()
{
  ELLE_LOG_COMPONENT("foo");
  ELLE_LOG("foo.1")
  {
    ELLE_LOG_COMPONENT("bar");
    ELLE_LOG("bar.1")
    {
      ELLE_LOG_COMPONENT("baz");
      ELLE_LOG("baz.1");
      ELLE_LOG("baz.2");
    }
    ELLE_LOG("bar.2");
  }
  ELLE_LOG("foo.2")
  {
    ELLE_LOG_COMPONENT("baz");
    ELLE_LOG("baz.3");
    ELLE_LOG("baz.4");
  }
  ELLE_LOG("foo.3");
}

static LL static_global{};
static std::unique_ptr<LL> static_uptr{new LL{}};
LL global{};

int main()
{
  static LL static_local{};
  static_from_function().foo();

  static_local.foo();
  static_global.foo();
  static_uptr->foo();

  ELLE_ERR("err BIET main");
  ELLE_LOG("log BIET main");
  ELLE_TRACE("trace BIET main");
  ELLE_DEBUG("debug BIET main");
  ELLE_DUMP("dump BIET main");
  f();
  s();

  {
    LL* localptr = new LL{};
    localptr->foo();

    delete localptr;
  }

  ELLE_LOG_COMPONENT("test_log.biet");
  ELLE_ERR("err BIET main");
  ELLE_LOG("log BIET main");
  ELLE_TRACE("trace BIET main");
  ELLE_DEBUG("debug BIET main");
  ELLE_DUMP("dump BIET main");

  static_from_function().foo();

  static_local.foo();
  static_global.foo();
  static_uptr->foo();

  global.foo();

  ELLE_LOG("Multi\nline comment\nOh yeah\n\n\nPIF\n\n\n");

  nested();
}
