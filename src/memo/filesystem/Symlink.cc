#include <memo/filesystem/Symlink.hh>
#include <memo/filesystem/Unknown.hh>

#include <elle/serialization/binary.hh>
#include <elle/cast.hh>

#include <fcntl.h>

#include <sys/stat.h> // S_IMFT...

#ifdef MEMO_WINDOWS
# undef stat
#endif

ELLE_LOG_COMPONENT("memo.filesystem.Symlink");

namespace memo
{
  namespace filesystem
  {
    Symlink::Symlink(FileSystem& owner,
                     Address address,
                     std::shared_ptr<DirectoryData> parent,
                     std::string const& name)
      : Node(owner, address, parent, name)
    {}

    void
    Symlink::_fetch()
    {
      this->_block = std::dynamic_pointer_cast<MutableBlock>(
        this->_owner.fetch_or_die(this->_address, {}, this->full_path()));
      umbrella([&] {
          this->_h = elle::serialization::binary::deserialize<FileHeader>(
            this->_block->data());
      });
    }

    FileHeader&
    Symlink::_header()
    {
      if (!this->_block)
        this->_fetch();
      return this->_h;
    }

    model::blocks::ACLBlock*
    Symlink::_header_block(bool force)
    {
      if (force && !this->_block)
        this->_fetch();
      return dynamic_cast<model::blocks::ACLBlock*>(this->_block.get());
    }

    void
    Symlink::_commit(WriteTarget)
    {
      auto data = elle::serialization::binary::serialize(this->_h,
        this->_owner.block_store()->version(), true);
      this->_block->data(data);
      this->_owner.store_or_die(std::move(this->_block), false);
    }

    void
    Symlink::stat(struct stat* st)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      try
      {
        this->_fetch();
        this->Node::stat(st);
        if (this->_h.symlink_target)
          st->st_size = this->_h.symlink_target->size();
      }
      catch (memo::model::doughnut::ValidationFailed const& e)
      {
        ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
      }
      catch (rfs::Error const& e)
      {
        ELLE_DEBUG("%s: filesystem exception: %s", *this, e.what());
        if (e.error_code() != EACCES)
          throw;
      }
      st->st_mode |= S_IFLNK;
      st->st_mode |= 0777; // Set rxwrwxrwx, to mimic Posix behavior.
    }

    void
    Symlink::unlink()
    {
      this->_parent->_files.erase(this->_name);
      this->_parent->write(this->_owner,
                           {OperationType::remove, this->_name},
                           DirectoryData::null_block,
                           true);
    }

    void
    Symlink::rename(bfs::path const& where)
    {
      Node::rename(where);
    }

    bfs::path
    Symlink::readlink()
    {
      this->_fetch();
      return *this->_h.symlink_target;
    }

    void
    Symlink::link(bfs::path const& where)
    {
      auto p = this->_owner.filesystem()->path(where.string());
      Unknown* unk = dynamic_cast<Unknown*>(p.get());
      if (!unk)
        THROW_EXIST();
      unk->symlink(readlink());
    }

    void
    Symlink::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Symlink(\"%s\")", this->_name);
    }

    void
    Symlink::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    Symlink::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    std::string
    Symlink::getxattr(std::string const& key)
    {
      return Node::getxattr(key);
    }

    std::vector<std::string>
    Symlink::listxattr()
    {
      this->_fetch();
      std::vector<std::string> res;
      for (auto const& a: this->_h.xattrs)
        res.push_back(a.first);
      return res;
    }

    void
    Symlink::setxattr(std::string const& name,
                      std::string const& value,
                      int flags)
    {
      Node::setxattr(name, value, flags);
    }

    void
    Symlink::removexattr(std::string const& name)
    {
      Node::removexattr(name);
    }

    std::unique_ptr<rfs::Handle>
    Symlink::open(int flags, mode_t mode)
    {
#ifdef MEMO_MACOSX
# define O_PATH O_SYMLINK
#endif
#ifdef O_PATH
      if (!(flags & O_PATH))
#endif
        THROW_NOSYS();
      return {};
    }
    void Symlink::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }
  }
}
