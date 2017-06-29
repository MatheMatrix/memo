#include <elle/log.hh>
#include <elle/test.hh>
#include <elle/Version.hh>

#include <memo/model/MissingBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/faith/Faith.hh>
#include <memo/silo/Memory.hh>

ELLE_LOG_COMPONENT("memo.model.faith.test");

template <typename B>
static
void
copy_and_store(B const& block,
               memo::model::faith::Faith& d,
               bool insert)
{
  namespace blk = memo::model::blocks;
  auto ptr = block.clone();
  if (insert)
    d.insert(std::move(ptr));
  else
    d.update(std::move(ptr));
}

static
void
faith()
{
  std::unique_ptr<memo::silo::Silo> storage
    = std::make_unique<memo::silo::Memory>();
  memo::model::faith::Faith faith(std::move(storage));

  auto block1 = faith.make_block<memo::model::blocks::MutableBlock>();
  auto block2 = faith.make_block<memo::model::blocks::MutableBlock>();
  BOOST_CHECK_NE(block1->address(), block2->address());
  ELLE_LOG("store blocks")
  {
    copy_and_store(*block1, faith, true);
    BOOST_CHECK_EQUAL(*faith.fetch(block1->address()), *block1);
    copy_and_store(*block2, faith, true);
    BOOST_CHECK_EQUAL(*faith.fetch(block2->address()), *block2);
  }
  ELLE_LOG("update block")
  {
    std::string update("twerk is the new twist");
    block2->data(elle::Buffer(update.c_str(), update.length()));
    BOOST_CHECK_NE(*faith.fetch(block2->address()), *block2);
    ELLE_LOG("STORE %x", block2->data());
    copy_and_store(*block2, faith, false);
    ELLE_LOG("STORED %x", block2->data());
    BOOST_CHECK_EQUAL(*faith.fetch(block2->address()), *block2);
  }
  ELLE_LOG("fetch non-existent block")
  {
    auto block3 = faith.make_block<memo::model::blocks::MutableBlock>();
    BOOST_CHECK_THROW(faith.fetch(block3->address()),
                      memo::model::MissingBlock);
    BOOST_CHECK_THROW(faith.remove(block3->address()),
                      memo::model::MissingBlock);
  }
  ELLE_LOG("remove block")
  {
    faith.remove(block2->address());
    BOOST_CHECK_THROW(faith.fetch(block2->address()),
                      memo::model::MissingBlock);
    faith.fetch(block1->address());
  }
}


ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(faith));
}
