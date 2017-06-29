#pragma once

#include <memo/filesystem/umbrella.hh>
#include <memo/filesystem/Node.hh>
#include <memo/filesystem/Directory.hh>

namespace memo
{
  namespace filesystem
  {
    namespace bfs = boost::filesystem;

    class Unknown
      : public Node
      , public rfs::Path
    {
    public:
      Unknown(FileSystem& owner, std::shared_ptr<DirectoryData> parent,
              std::string const& name);
      void
      stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override { THROW_NOENT(); }
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override { THROW_NOENT(); }
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override { THROW_NOENT(); }
      void mkdir(mode_t mode) override;
      void rmdir() override { THROW_NOENT(); }
      void rename(bfs::path const& where) override { THROW_NOENT(); }
      bfs::path readlink() override { THROW_NOENT(); }
      void symlink(bfs::path const& where) override;
      void link(bfs::path const& where) override;
      void chmod(mode_t mode) override { THROW_NOENT(); }
      void chown(int uid, int gid) override { THROW_NOENT(); }
      void statfs(struct statvfs *) override { THROW_NOENT(); }
      void utimens(const struct timespec tv[2]) override { THROW_NOENT(); }
      void truncate(off_t new_size) override { THROW_NOENT(); }
      std::shared_ptr<Path> child(std::string const& name) override { THROW_NOENT(); }
      bool allow_cache() override { return false;}
      std::string getxattr(std::string const& k) override {{ THROW_NODATA();} }
      void _fetch() override {}
      void _commit(WriteTarget target) override {}
      model::blocks::ACLBlock* _header_block(bool) override { return nullptr;}
      FileHeader& _header() override { THROW_NOENT(); }

      void
      print(std::ostream& stream) const override;
      std::unique_ptr<rfs::Handle> create_0_7(int flags, mode_t mode);
    private:
    };
  }
}

