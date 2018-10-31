#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <boost/filesystem/fstream.hpp>

#include <elle/finally.hh>
#include <elle/os/environ.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/filesystem.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/signal.hh>
#include <elle/test.hh>

using namespace std::literals;

ELLE_LOG_COMPONENT("Test");

bool sandbox = elle::os::getenv("SANDBOX", false);

namespace bfs = boost::filesystem;

namespace sum
{
  class Handle: public elle::reactor::filesystem::Handle
  {
  public:
    Handle(int num)
      : num(num)
    {}

    int
    read(elle::WeakBuffer buffer, size_t size, off_t offset) override
    {
      ELLE_TRACE("read %s/%s", offset, size);
      auto const val = std::to_string(num) + "\n";
      if (offset >= signed(val.length()))
        return 0;
      size_t sz = std::min(val.length()+1, size);
      strncpy((char*)buffer.contents(), val.c_str(), sz);
      return sz;
    }

    int
    write(elle::ConstWeakBuffer buffer, size_t size, off_t offset) override
    {
      throw elle::reactor::filesystem::Error(EPERM, "Write access denied");
    }

    void
    close() override
    {}

    int num;
  };

  class Path: public elle::reactor::filesystem::Path
  {
  public:
    Path(int num, bool isSum = false)
      : num(num)
      , isDir(!isSum)
    {}

    void stat(struct stat* stbuf) override
    {
      ELLE_DEBUG("%s.stat(%s)", this, stbuf);
      memset(stbuf, 0, sizeof(struct stat));
      if (isDir)
        stbuf->st_mode = S_IFDIR | 0755;
      else
      {
        stbuf->st_mode = S_IFREG | 0444;
        // We have to report a dummy non-empty size
        stbuf->st_size = 4096;
      }
    }

    void list_directory(elle::reactor::filesystem::OnDirectoryEntry cb) override
    {
      if (!isDir)
        throw elle::reactor::filesystem::Error(ENOTDIR, "Not a directory");
      for (unsigned i=0; i<100; ++i)
        cb(std::to_string(i), nullptr);
      cb("sum", nullptr);
    }

    std::shared_ptr<elle::reactor::filesystem::Path> child(std::string const& name) override
    {
      ELLE_LOG("child %s", name);
      if (!isDir)
        throw elle::reactor::filesystem::Error(ENOTDIR, "Not a directory");
      if (name == "sum")
        return std::make_unique<Path>(num, true);
      std::size_t pos;
      int n;
      try
      {
        n = std::stol(name, &pos, 0);
      }
      catch(...)
      {
        throw elle::reactor::filesystem::Error(ENOENT, "No such file or directory");
      }
      if (pos < name.size())
        throw elle::reactor::filesystem::Error(ENOENT, "No such file or directory");
      return std::make_shared<Path>(n + num);
    }

    std::unique_ptr<elle::reactor::filesystem::Handle> open(int flags, mode_t mode) override
    {
      if ((flags & 3) != O_RDONLY)
        throw elle::reactor::filesystem::Error(EACCES, "Write access denied");
      return std::make_unique<Handle>(num);
    }

    int num;
    bool isDir;
  };

  class Operations: public elle::reactor::filesystem::Operations
  {
  public:
    std::shared_ptr<elle::reactor::filesystem::Path> path(std::string const& path) override
    {
      ELLE_ASSERT(path == "/");
      return std::make_shared<Path>(0);
    }
  };
}

static int directory_count(bfs::path const& p)
{
  bfs::directory_iterator d(p);
  int s=0;
  while (d!= bfs::directory_iterator())
  {
    ++s; ++d;
  }
  return s;
}

static void
run_filesystem(elle::reactor::filesystem::FileSystem &fs,
               bfs::path tmp,
               elle::reactor::Barrier** b,
               elle::reactor::Scheduler* & sched_ptr)
{
  elle::reactor::Scheduler sched;
  sched_ptr = &sched;
  elle::reactor::Thread t(sched, "mount", [&] {
    elle::reactor::Barrier barrier;
    *b = &barrier;
    ELLE_TRACE("mounting");
    fs.mount(tmp, {});
    ELLE_TRACE("waiting on stop barrier");
    barrier.wait();
    ELLE_TRACE("exiting thread");
  });
  sched.run();
}

static void test_sum()
{
  auto&& fs =
    elle::reactor::filesystem::FileSystem(std::make_unique<sum::Operations>(),
                                          true);
#ifdef ELLE_WINDOWS
  auto const tmp = bfs::path("K:");
#else
  auto const tmp = bfs::temp_directory_path() / bfs::unique_path();
  elle::SafeFinally remover([&] {
      bfs::remove(tmp);
  });
  bfs::create_directories(tmp);
#endif

  elle::reactor::Barrier* barrier;
  elle::reactor::Scheduler* sched;
  std::thread t([&] { run_filesystem(fs, tmp, &barrier, sched); });
  ELLE_LOG("Mounted on %s", tmp);
  if (sandbox)
  {
    t.join();
    return;
  }
  std::this_thread::sleep_for(500ms);
  int s = -1;
  bfs::ifstream(tmp/"1"/"sum") >> s;
  BOOST_REQUIRE(s == 1);
  bfs::ifstream(tmp/"10"/"12"/"sum") >> s;
  BOOST_CHECK_EQUAL(s, 22);
  s=0;
  bfs::ifstream(tmp/"10"/"12"/"sum") >> s;
  BOOST_CHECK_EQUAL(s, 22);
  BOOST_CHECK_EQUAL(directory_count(tmp), 101);
  ELLE_DEBUG("teardown");
  sched->mt_run<void>("stop", [&] {fs.unmount();});
  sched->mt_run<void>("stop", [&] {barrier->open();});
  ELLE_DEBUG("joining");
  t.join();
  BOOST_CHECK_EQUAL(directory_count(tmp), 0);
}

namespace xorfs
{
  namespace rfs = elle::reactor::filesystem;
  class Encrypt: public rfs::BindOperations
  {
  public:
    Encrypt(bfs::path const& source)
      : rfs::BindOperations(source)
    {}

    std::shared_ptr<rfs::Path> path(std::string const&) override;
  };

  class Handle: public rfs::BindHandle
  {
  public:
    Handle(int fd, bfs::path const& where)
      : rfs::BindHandle(fd, where)
    {}

    int
    read(elle::WeakBuffer buffer, size_t size, off_t offset) override
    {
      lseek(_fd, offset, SEEK_SET);
      int len = ::read(_fd, (void*)buffer.contents(), size);
      if (len <=0)
        return len;
      for (int i=0; i<len; ++i)
        buffer[i] = buffer[i] ^ 0xFF;
      return len;
    }

    int
    write(elle::ConstWeakBuffer buffer, size_t size, off_t offset) override
    {
      lseek(_fd, offset, SEEK_SET);
      for (uint64_t i = 0; i < buffer.size(); ++i)
        const_cast<elle::Buffer::Byte&>(buffer.contents()[i]) ^= 0xFF;
      return ::write(_fd, buffer.contents(), size);
    }
  };

  class Path: public rfs::BindPath
  {
  public:
    Path(bfs::path where, Encrypt& ctx)
      : rfs::BindPath(where, ctx)
    {}

    std::unique_ptr<rfs::BindHandle>
    make_handle(bfs::path& where, int fd) override
    {
      return std::make_unique<Handle>(fd, where);
    }
  };

  std::shared_ptr<rfs::Path>
  Encrypt::path(std::string const& p)
  {
    return std::make_shared<Path>(p, *this);
  }
}


static
void
test_xor()
{
  auto const tmpmount = bfs::temp_directory_path() / bfs::unique_path();
  auto const tmpsource = bfs::temp_directory_path() / bfs::unique_path();

  elle::SafeFinally remover([&] {
      boost::system::error_code erc;
      bfs::remove(tmpmount, erc);
      bfs::remove(tmpsource, erc);
  });
  elle::reactor::filesystem::FileSystem fs(
    std::make_unique<xorfs::Encrypt>(tmpsource),
    false);
  bfs::create_directories(tmpmount);
  bfs::create_directories(tmpsource);
  ELLE_LOG("mount: %s   source: %s", tmpmount, tmpsource);
  elle::reactor::Barrier* barrier;
  elle::reactor::Scheduler* sched;
  std::thread t([&] { run_filesystem(fs, tmpmount, &barrier, sched);});
  if (sandbox)
  {
    t.join();
    return;
  }
  std::string text = "coincoin";

  for (int i=0;
       i<10
       && (directory_count(tmpmount) != 1 || directory_count(tmpsource) != 1);
       ++i)
  {
    std::this_thread::sleep_for(200ms);
    bfs::ofstream ofs(tmpmount / "test");
    ofs << text;
  }
  BOOST_CHECK_EQUAL(directory_count(tmpmount), 1);
  BOOST_CHECK_EQUAL(directory_count(tmpsource), 1);
  {
    bfs::ifstream ifs(tmpmount / "test");
    ifs >> text;
  }
  BOOST_CHECK_EQUAL(text, "coincoin");
  {
    bfs::ifstream ifs(tmpsource / "test");
    unsigned char xored[10];
    ifs.read((char*)xored, 10);
    BOOST_CHECK_EQUAL(ifs.gcount(), 8);
    for (int i=0; i<8; ++i)
    {
      BOOST_CHECK_EQUAL(text[i], xored[i] ^ 0xFF);
    }
  }
  bfs::remove(tmpmount / "test");
  BOOST_CHECK_EQUAL(directory_count(tmpmount), 0);
  BOOST_CHECK_EQUAL(directory_count(tmpsource), 0);

  bfs::create_directory(tmpmount / "dir");
  BOOST_CHECK_EQUAL(directory_count(tmpmount), 1);
  BOOST_CHECK_EQUAL(directory_count(tmpsource), 1);
  BOOST_CHECK_EQUAL(directory_count(tmpmount / "dir"), 0);
  BOOST_CHECK_EQUAL(directory_count(tmpsource / "dir"), 0);
  {
    bfs::ofstream ofs(tmpmount / "dir"/ "test");
    ofs << text;
  }
  BOOST_CHECK_EQUAL(directory_count(tmpmount / "dir"), 1);
  BOOST_CHECK_EQUAL(directory_count(tmpsource / "dir"), 1);
  boost::system::error_code erc;
  bfs::remove(tmpmount / "dir", erc); // dir not empty
  BOOST_CHECK_EQUAL(!!erc, true);
  struct stat st;
  ::stat((tmpmount / "dir" / "test").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 8);
  int fd = ::open((tmpmount / "dir" / "test").string().c_str(),
                   O_WRONLY | O_APPEND);
  BOOST_CHECK_GT(fd, 0);
  auto num_written = ::write(fd, "foo", 3);
  ::close(fd);
  BOOST_CHECK_EQUAL(num_written, 3);
  BOOST_CHECK_EQUAL(bfs::file_size(tmpmount / "dir" / "test"), 11);
  fd = ::open((tmpmount / "dir" / "test").string().c_str(),
    O_WRONLY | O_CREAT | O_EXCL, 0644);
  BOOST_CHECK_LT(fd, 0);
  fd = ::open((tmpmount / "dir" / "test").string().c_str(),
    O_WRONLY | O_CREAT | O_TRUNC, 0644);
  BOOST_CHECK_GT(fd, 0);
  num_written = ::write(fd, "foo", 3);
  BOOST_CHECK_EQUAL(num_written, 3);
  ::close(fd);
  BOOST_CHECK_EQUAL(bfs::file_size(tmpmount / "dir" / "test"), 3);
  bfs::rename(tmpmount / "dir" / "test", tmpmount / "dir" / "test2", erc);
  if (erc)
    ELLE_ERR("move: %s", erc.message());
  BOOST_CHECK_EQUAL(!!erc, false);
  BOOST_CHECK_EQUAL(directory_count(tmpmount / "dir"), 1);
  BOOST_CHECK_EQUAL(directory_count(tmpsource / "dir"), 1);
  BOOST_CHECK_EQUAL(bfs::file_size(tmpmount / "dir" / "test2"), 3);

  int res = ::chmod((tmpmount / "dir" / "test2").string().c_str(), 0600);
  if (res)
    ELLE_ERR("chmod failed: %s", strerror(errno));
  ::stat((tmpmount / "dir" / "test2").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_mode&0777, 0600);

  bfs::remove(tmpmount / "dir" / "test2");
  BOOST_CHECK_EQUAL(directory_count(tmpmount / "dir"), 0);
  BOOST_CHECK_EQUAL(directory_count(tmpsource / "dir"), 0);
  bfs::remove(tmpmount / "dir");
  ELLE_TRACE("unmounting...");
  sched->mt_run<void>("stop", [&] {fs.unmount();});
  sched->mt_run<void>("stop", [&] {barrier->open();});
  ELLE_TRACE("joining...");
  t.join();
  ELLE_TRACE("finished");
}

ELLE_TEST_SUITE()
{
  boost::unit_test::test_suite* filesystem = BOOST_TEST_SUITE("filesystem");
  boost::unit_test::framework::master_test_suite().add(filesystem);
  filesystem->add(BOOST_TEST_CASE(test_sum), 0, sandbox ? 0 : 20);
  filesystem->add(BOOST_TEST_CASE(test_xor), 0, sandbox ? 0 : 20);
}
