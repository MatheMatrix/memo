#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/serialization/json.hh>
#include <elle/test.hh>

#include <memo/silo/Collision.hh>
#include <memo/silo/Filesystem.hh>
#include <memo/silo/Memory.hh>
#include <memo/silo/MissingKey.hh>
#include <memo/silo/S3.hh>
#include <memo/silo/Silo.hh>

ELLE_LOG_COMPONENT("tests.storage");

static
void
tests(memo::silo::Silo& storage)
{
  memo::silo::Key::Value v1 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
  };
  memo::silo::Key k1(&v1[0]);
  char const* data1 = "the grey";
  storage.set(k1, elle::Buffer(data1, strlen(data1)));
  BOOST_CHECK_EQUAL(storage.get(k1), data1);
  char const* data2 = "the white";
  storage.set(k1, elle::Buffer(data2, strlen(data2)), false, true);
  BOOST_CHECK_EQUAL(storage.get(k1), data2);
  BOOST_CHECK_THROW(storage.set(k1, elle::Buffer()),
                    memo::silo::Collision);
  BOOST_CHECK_EQUAL(storage.get(k1), data2);
  memo::silo::Key::Value v2 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
  };
  memo::silo::Key k2(&v2[0]);
  BOOST_CHECK_THROW(storage.get(k2), memo::silo::MissingKey);
  BOOST_CHECK_THROW(storage.set(k2, elle::Buffer(), false, true),
                    memo::silo::MissingKey);
  BOOST_CHECK_THROW(storage.erase(k2), memo::silo::MissingKey);
  storage.erase(k1);
  BOOST_CHECK_THROW(storage.get(k1), memo::silo::MissingKey);
}

static
void
tests_capacity(memo::silo::Silo& storage, int64_t capacity)
{
  memo::silo::Key::Value v1 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
  };
  memo::silo::Key k1(&v1[0]);
  BOOST_CHECK_EQUAL(storage.capacity(), capacity);
  BOOST_CHECK_EQUAL(storage.usage(), 0);

  char* data = new char[capacity / 2];
  for (int i = 0; i < capacity / 2; ++i)
    data[i] = 'n';
  data[capacity / 2 - 1] = '\0';
  ELLE_LOG("set data to storage");
  storage.set(k1, elle::Buffer(data, capacity / 2));
  BOOST_CHECK_EQUAL(storage.usage(), capacity / 2);
  ELLE_LOG("erase data from storage");
  storage.erase(k1);
  BOOST_CHECK_EQUAL(storage.usage(), 0);
  delete[] data;
  data = new char[capacity];
  for (int i = 0; i < capacity; ++i)
    data[i] = 'u';
  data[capacity - 1] = '\0';
  ELLE_LOG("set data to storage");
  storage.set(k1, elle::Buffer(data, capacity));
  BOOST_CHECK_EQUAL(storage.usage(), capacity);
  ELLE_LOG("erase data from storage");
  storage.erase(k1);
  BOOST_CHECK_EQUAL(storage.usage(), 0);
  delete[] data;
}

static
void
memory()
{
  memo::silo::Memory storage;
  tests(storage);
}


static
void
filesystem()
{
  elle::filesystem::TemporaryDirectory d;
  memo::silo::Filesystem storage(d.path());
  tests(storage);
}

static
void
filesystem_small_capacity()
{
  ELLE_TRACE("starting filesystem_small_capacity");
  elle::filesystem::TemporaryDirectory d;
  int64_t size = 2 << 16;
  memo::silo::Filesystem storage(d.path(), size);
  tests_capacity(storage, size);
}

static
void
filesystem_large_capacity()
{
  ELLE_TRACE("starting filesystem_large_capacity");
  elle::filesystem::TemporaryDirectory d;
  int64_t size = 2 << 18;
  memo::silo::Filesystem storage(d.path(), size);
  tests_capacity(storage, size);
}

extern const std::string zero_five_four_s3_storage_reduced;
extern const std::string zero_five_four_s3_storage_default;

static
void
s3_storage_class_backward(bool reduced)
{
  ELLE_TRACE("starting s3_storage_class_backward_%s",
             reduced ? "reduced" : "default");
  std::stringstream ss(reduced ? zero_five_four_s3_storage_reduced
                               : zero_five_four_s3_storage_default);
  using elle::serialization::json::deserialize;
  auto config = deserialize<memo::silo::S3SiloConfig>(ss, false);
  BOOST_CHECK_EQUAL(
    static_cast<int>(config.storage_class),
    static_cast<int>(reduced ? elle::service::aws::S3::StorageClass::ReducedRedundancy
                             : elle::service::aws::S3::StorageClass::Default));
}

static
void
s3_storage_class_backward_reduced()
{
  s3_storage_class_backward(true);
}

static
void
s3_storage_class_backward_default()
{
  s3_storage_class_backward(false);
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(filesystem));
  suite.add(BOOST_TEST_CASE(filesystem_small_capacity));
  suite.add(BOOST_TEST_CASE(filesystem_large_capacity));
  suite.add(BOOST_TEST_CASE(memory));
  suite.add(BOOST_TEST_CASE(s3_storage_class_backward_reduced));
  suite.add(BOOST_TEST_CASE(s3_storage_class_backward_default));
}

const std::string zero_five_four_s3_storage_reduced =
"{"
"    \"aws_credentials\" : {"
"        \"access_key_id\" : \"AKIAIOSFODNN7EXAMPLE\","
"        \"bucket\" : \"some-bucket\","
"        \"folder\" : \"some-folder\","
"        \"region\" : \"eu-central-1\","
"        \"secret_access_key\" : \"wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY\""
"    },"
"    \"name\" : \"s3\","
"    \"reduced_redundancy\" : true,"
"    \"type\" : \"s3\""
"}";

const std::string zero_five_four_s3_storage_default =
"{"
"    \"aws_credentials\" : {"
"        \"access_key_id\" : \"AKIAIOSFODNN7EXAMPLE\","
"        \"bucket\" : \"some-bucket\","
"        \"folder\" : \"some-folder\","
"        \"region\" : \"eu-central-1\","
"        \"secret_access_key\" : \"wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY\""
"    },"
"    \"name\" : \"s3\","
"    \"reduced_redundancy\" : false,"
"    \"type\" : \"s3\""
"}";
