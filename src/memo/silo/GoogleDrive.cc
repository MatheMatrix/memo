#include <sstream>
#include <string>

#include <elle/bench.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>
#include <elle/meta.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>

#include <elle/das/Symbol.hh>
#include <elle/das/model.hh>
#include <elle/das/serializer.hh>

#include <memo/silo/Collision.hh>
#include <memo/silo/GoogleDrive.hh>
#include <memo/silo/MissingKey.hh>
#include <memo/symbols.hh>

using namespace std::literals;

ELLE_LOG_COMPONENT("memo.storage.GoogleDrive");

#define BENCH(name)                                                     \
  static auto bench = elle::Bench("bench.gdrive." name, 10000s);        \
  auto bs = elle::Bench::BenchScope(bench)

namespace memo
{
  namespace silo
  {
    struct Parent
    {
      std::string id;
      using Model = elle::das::Model<Parent,
                               decltype(elle::meta::list(symbols::id))>;
    };

    struct Directory
    {
      std::string title;
      std::vector<Parent> parents;
      std::string mimeType;

      using Model = elle::das::Model<
        Directory,
        decltype(elle::meta::list(
                   symbols::title,
                   symbols::parents,
                   symbols::mimeType))>;
    };

    struct Metadata
    {
      std::string title;
      std::vector<Parent> parents;
      using Model = elle::das::Model<
        Metadata,
        decltype(elle::meta::list(
                   symbols::title,
                   symbols::parents))>;
    };
  }
}

ELLE_DAS_SERIALIZE(memo::silo::Parent);
ELLE_DAS_SERIALIZE(memo::silo::Directory);
ELLE_DAS_SERIALIZE(memo::silo::Metadata);

namespace memo
{
  namespace silo
  {

    /*
     * GoogleDrive
     */

    static elle::reactor::Duration delay(int attempt)
    {
      if (attempt > 8)
        attempt = 8;
      unsigned int factor = pow(2, attempt);
      return boost::posix_time::milliseconds(factor * 100);
    }

    boost::filesystem::path
    GoogleDrive::_path(Key key) const
    {
      return this->_root / elle::sprintf("%x", key);
    }

    GoogleDrive::GoogleDrive(std::string refresh_token,
                             std::string name)
      : GoogleDrive{".infinit",
                    std::move(refresh_token),
                    std::move(name)}
    {}

    GoogleDrive::GoogleDrive(boost::filesystem::path root,
                             std::string refresh_token,
                             std::string name)
      : GoogleAPI(name, refresh_token)
      , _root{std::move(root)}
    {
      std::string id = this->_exists(this->_root.string());
      if (id == "")
      {
        auto r = this->_mkdir(this->_root.string());
        auto json = boost::any_cast<elle::json::Object>(elle::json::read(r));
        id = boost::any_cast<std::string>(json["id"]);
      }

      this->_dir_id = id;
    }

    elle::Buffer
    GoogleDrive::_get(Key key) const
    {
      BENCH("get");
      ELLE_DEBUG("get %x", key);

      using StatusCode = elle::reactor::http::StatusCode;
      std::string id = _exists(elle::sprintf("%x", key));
      if (id == "")
        throw MissingKey(key);

      auto url = elle::sprintf("https://www.googleapis.com/drive/v2/files/%s",
                               id);
      auto conf = elle::reactor::http::Request::Configuration();
      auto r = this->_request(url,
                              elle::reactor::http::Method::GET,
                              elle::reactor::http::Request::QueryDict{{"alt", "media"}},
                              conf,
                              {StatusCode::Not_Found});

      if (r.status() == StatusCode::Not_Found)
        throw MissingKey(key);
      else if (r.status() == StatusCode::OK)
      {
        auto res = r.response();
        ELLE_TRACE("%s: got %s bytes for %x", *this, res.size(), key);
        return res;
      }

      ELLE_TRACE("The impossible happened: %s", r.status());
      elle::unreachable();
    }

    int
    GoogleDrive::_set(Key key,
                      elle::Buffer const& value,
                      bool insert,
                      bool update)
    {
      BENCH("set");
      using StatusCode = elle::reactor::http::StatusCode;
      ELLE_DEBUG("set %x", key);
      if (insert)
      {
        try
        {
          this->_erase(key);
          ELLE_DUMP("replace");
        }
        catch(MissingKey const& e)
        {
          ELLE_DUMP("new");
        }

        auto r = this->_insert(key, value);
        if (r.status() == StatusCode::Not_Found && !update)
          throw Collision(key);
      }
      else if (update)
      {
        // Non-sense. If a file exists->remove it first then write.
      }
      else
        elle::err("neither inserting, nor updating");

      // FIXME: impl.
      return 0;
    }

    int
    GoogleDrive::_erase(Key k)
    {
      BENCH("erase");
      ELLE_DUMP("_erase");
      using Request = elle::reactor::http::Request;
      using Method = elle::reactor::http::Method;
      using StatusCode = elle::reactor::http::StatusCode;

      Request::Configuration conf;

      std::string id = this->_exists(elle::sprintf("%x", k));
      if (id == "")
        throw MissingKey(k);

      auto url = elle::sprintf("https://www.googleapis.com/drive/v2/files/%s",
                               id);
      auto r = this->_request(url,
                              Method::DELETE,
                              Request::QueryDict{},
                              conf,
                              std::vector<StatusCode>{StatusCode::Not_Found,
                                                      StatusCode::No_Content});

      if (r.status() == StatusCode::Not_Found)
        elle::err("File %s not found", id);

      // FIXME: impl.
      return 0;
    }

    std::vector<Key>
    GoogleDrive::_list()
    {
      ELLE_DUMP("_list (Not used)");
      elle::err("Not implemented yet.");
    }

    BlockStatus
    GoogleDrive::_status(Key k)
    {
      std::string id = this->_exists(elle::sprintf("%x", k));

      if (id == "")
        return BlockStatus::missing;
      else
        return BlockStatus::exists;
    }

    elle::reactor::http::Request
    GoogleDrive::_mkdir(std::string const& path) const
    {
      ELLE_DUMP("_mkdir");
      using Request = elle::reactor::http::Request;
      using Method = elle::reactor::http::Method;
      using Configuration = elle::reactor::http::Request::Configuration;
      using StatusCode = elle::reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(elle::reactor::DurationOpt());

      Directory dir{path,
                    {Parent{"root"}},
                    "application/vnd.google-apps.folder"};

      unsigned attempt = 0;
      while (true)
      {
        conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));
        Request r("https://www.googleapis.com/drive/v2/files",
                  Method::POST,
                  "application/json",
                  conf);

        elle::serialization::json::serialize(dir, r, false);
        r.finalize();

        if (r.status() == StatusCode::OK)
          return r;

        ELLE_WARN("Unexpected google HTTP status on POST: %s, attempt %s",
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        std::cout << r;

        elle::reactor::sleep(delay(attempt++));
      }
    }

    elle::reactor::http::Request
    GoogleDrive::_insert(Key key, elle::Buffer const& value) const
    {
      ELLE_DUMP("_insert");
      using Configuration = elle::reactor::http::Request::Configuration;
      using Method = elle::reactor::http::Method;
      using Request = elle::reactor::http::Request;
      using StatusCode = elle::reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(elle::reactor::DurationOpt());
      unsigned attempt = 0;

      // https://developers.google.com/drive/web/manage-uploads#multipart

      Request::QueryDict query;
      query["uploadType"] = "multipart";

      std::string delim_value = "galibobro";
      std::string delim = "--" + delim_value;
      std::string mime_meta = "Content-Type: application/json; charset=UTF-8";
      std::string mime = "Content-Type: application/octet-stream";

      conf.header_add("Content-Type",
                      elle::sprintf("multipart/related; boundary=\"%s\"",
                                    delim_value));


      Metadata metadata{elle::sprintf("%x", key), {Parent{this->_dir_id}}};

      while (true)
      {
        conf.header_add("Authorization",
                        elle::sprintf("Bearer %s", this->_token));
        Request r{"https://www.googleapis.com/upload/drive/v2/files",
                  Method::POST,
                  conf};
        r.query_string(query);

        r.write(delim.c_str(), delim.size());
        r.write("\n", 1);
        r.write(mime_meta.c_str(), mime_meta.size());
        r.write("\n\n", 2);
        elle::serialization::json::serialize(metadata, r, false);
        r.write("\n\n", 2);
        r.write(delim.c_str(), delim.size());
        r.write("\n", 1);
        r.write(mime.c_str(), mime.size());
        r.write("\n\n", 2);
        r.write(value.string().c_str(), value.size());
        r.write("\n\n", 2);
        r.write(delim.c_str(), delim.size());
        r.write("--", 2);

        r.finalize();

        if (r.status() == StatusCode::OK)
          return r;
        else if (r.status() == StatusCode::Forbidden
                 || r.status() == StatusCode::Unauthorized)
        {
          const_cast<GoogleDrive*>(this)->_refresh();
        };

        ELLE_WARN("Unexpected google HTTP status (insert): %s, attempt %s",
            r.status(),
            attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        elle::reactor::sleep(delay(attempt++));
      }
    }

    std::string
    GoogleDrive::_exists(std::string file_name) const
    {
      ELLE_DUMP("_exists");
      using Configuration = elle::reactor::http::Request::Configuration;
      using Method = elle::reactor::http::Method;
      using Request = elle::reactor::http::Request;
      using StatusCode = elle::reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(elle::reactor::DurationOpt());
      unsigned attempt = 0;
      elle::reactor::http::Request::QueryDict query;

      while (true)
      {
        query["access_token"] = this->_token;
        query["q"] = elle::sprintf("title = '%s' and trashed = false",
                                   file_name);
        Request r{"https://www.googleapis.com/drive/v2/files/",
                  Method::GET,
                  conf};
        r.query_string(query);

        r.finalize();

        if (r.status() == StatusCode::OK)
        {
          auto json = boost::any_cast<elle::json::Object>(
              elle::json::read(r));
          auto items = boost::any_cast<elle::json::Array>(json["items"]);
          for (auto const& item: items)
          {
            auto f = boost::any_cast<elle::json::Object>(item);
            std::string id = boost::any_cast<std::string>(f["id"]);
            ELLE_DEBUG("Resolved %s to %s", file_name, id);
            return id;
          }
          return "";
        }
        else if (r.status() == StatusCode::Unauthorized
                 || r.status() == StatusCode::Forbidden)
          const_cast<GoogleDrive*>(this)->_refresh();
        else
        {
          ELLE_WARN("Unexpected google HTTP status (check): %s, attempt %s",
              r.status(),
              attempt + 1);
          ELLE_DUMP("body: %s", r.response());
        }

        elle::reactor::sleep(delay(attempt++));
      }
    }

    /*
     *  GoogleDriveSiloConfig
     */

    GoogleDriveSiloConfig::GoogleDriveSiloConfig(
        std::string name,
        boost::optional<std::string> root,
        std::string refresh_token,
        std::string user_name,
        boost::optional<int64_t> capacity,
        boost::optional<std::string> description)
      : SiloConfig(
          std::move(name), std::move(capacity), std::move(description))
      , root{std::move(root)}
      , refresh_token{std::move(refresh_token)}
      , user_name{std::move(user_name)}
    {}

    GoogleDriveSiloConfig::GoogleDriveSiloConfig(
        elle::serialization::SerializerIn& s)
      : SiloConfig(s)
      , root(s.deserialize<std::string>("root"))
      , refresh_token(s.deserialize<std::string>("refresh_token"))
      , user_name(s.deserialize<std::string>("user_name"))
    {}

    void
    GoogleDriveSiloConfig::serialize(elle::serialization::Serializer& s)
    {
      SiloConfig::serialize(s);
      s.serialize("root", this->root);
      s.serialize("refresh_token", this->refresh_token);
      s.serialize("user_name", this->user_name);
    }

    std::unique_ptr<memo::silo::Silo>
    GoogleDriveSiloConfig::make()
    {
      if (this->root)
        return std::make_unique<memo::silo::GoogleDrive>(
            this->root.get(), this->refresh_token, this->user_name);
      else
        return std::make_unique<memo::silo::GoogleDrive>(
            this->refresh_token, this->user_name);
    }

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<GoogleDriveSiloConfig> _register_GoogleDriveSiloConfig(
      "google");
  }
}
