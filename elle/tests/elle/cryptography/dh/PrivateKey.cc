#include "../cryptography.hh"

#include <elle/cryptography/dh/PrivateKey.hh>
#include <elle/cryptography/dh/PublicKey.hh>
#include <elle/cryptography/dh/KeyPair.hh>

#include <elle/printf.hh>

/*---------.
| Generate |
`---------*/

static
elle::cryptography::dh::PrivateKey
_test_generate()
{
  elle::cryptography::dh::KeyPair keypair =
    elle::cryptography::dh::keypair::generate();

  elle::cryptography::dh::PrivateKey k(keypair.k());

  return (k);
}

static
void
test_generate()
{
  _test_generate();
}

/*----------.
| Construct |
`----------*/

static
void
test_construct()
{
  elle::cryptography::dh::PrivateKey k1 = _test_generate();

  // PrivateKey copy.
  elle::cryptography::dh::PrivateKey k2(k1);

  BOOST_CHECK_EQUAL(k1, k2);

  // PrivateKey move.
  elle::cryptography::dh::PrivateKey k3(std::move(k1));

  BOOST_CHECK_EQUAL(k2, k3);
}

/*--------.
| Compare |
`--------*/

static
void
test_compare()
{
  elle::cryptography::dh::PrivateKey k1 = _test_generate();
  elle::cryptography::dh::PrivateKey k2 = _test_generate();

  // With high probabilituy, this should not be the case. Otherwise,
  // the random generator is probably broken.
  BOOST_CHECK(k1 != k2);
  BOOST_CHECK(!(k1 == k2));
}

/*-----.
| Main |
`-----*/

ELLE_TEST_SUITE()
{
  boost::unit_test::test_suite* suite = BOOST_TEST_SUITE("dh/PrivateKey");

  suite->add(BOOST_TEST_CASE(test_generate));
  suite->add(BOOST_TEST_CASE(test_construct));
  suite->add(BOOST_TEST_CASE(test_compare));

  boost::unit_test::framework::master_test_suite().add(suite);
}
