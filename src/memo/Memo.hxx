#pragma once

namespace memo
{
  namespace
  {
    inline
    std::string
    plural(std::string const& type)
    {
      return elle::sprintf("%s%s", type, type.back() == 's' ? "" : "s");
    }
  }

  template <typename T>
  std::vector<std::unique_ptr<T, std::default_delete<Credentials>>>
  Memo::credentials(std::string const& name) const
  {
    auto res
      = std::vector<std::unique_ptr<T, std::default_delete<Credentials>>>{};
    for (auto const& p
           : bfs::directory_iterator(this->_credentials_path(name)))
      if (is_visible_file(p))
      {
        bfs::ifstream f;
        this->_open_read(f, p, name, "credential");
        res.emplace_back(
          std::dynamic_pointer_cast<T>(load<std::unique_ptr<Credentials>>(f)));
      }
    return res;
  }

  template <typename T>
  T
  Memo::load(std::ifstream& input)
  {
    ELLE_ASSERT(input.is_open());
    return elle::serialization::json::deserialize<T>(
      input,
      false /* versioned */
      );
  }

  template <typename T>
  void
  Memo::save(std::ostream& output, T const& resource, bool pretty)
  {
    ELLE_ASSERT(output.good());
    elle::serialization::json::serialize(
      resource, output,
      false, /* versioned */
      pretty);
  }

  // Beyond

  template <typename T>
  T
  Memo::hub_fetch(std::string const& where,
                  std::string const& type,
                  std::string const& name,
                  boost::optional<memo::User const&> self,
                  memo::Headers const& extra_headers) const
  {
    auto json = hub_fetch_json(where, type, name, self, extra_headers);
    elle::serialization::json::SerializerIn input(json, false);
    this->report_local_action()("fetched", type, name);
    return input.deserialize<T>();
  }

  template <typename T>
  T
  Memo::hub_fetch(std::string const& type,
                  std::string const& name) const
  {
    return hub_fetch<T>(elle::sprintf("%s/%s", plural(type), name),
                        type, name);
  }

  template <typename Serializer, typename T>
  void
  Memo::hub_push(std::string const& where,
                 std::string const& type,
                 std::string const& name,
                 T const& o,
                 memo::User const& self,
                 bool hub_error,
                 bool update) const
  {
    ELLE_LOG_COMPONENT("memo");
    auto payload_ = [&] {
      std::stringstream stream;
      elle::serialization::json::serialize<Serializer>(o, stream, false);
      return stream.str();
    }();
    ELLE_TRACE("pushing %s/%s with payload %s", type, name, payload_);
    elle::ConstWeakBuffer payload{payload_.data(), payload_.size()};
    hub_push_data(
      where, type, name, payload, "application/json", self, hub_error,
      update);
  }

  template <typename Serializer, typename T>
  void
  Memo::hub_push(std::string const& type,
                 std::string const& name,
                 T const& o,
                 memo::User const& self,
                 bool hub_error,
                 bool update) const
  {
    hub_push<Serializer>(elle::sprintf("%s/%s", plural(type), name),
                         type, name, o, self, hub_error, update);
  }
}
