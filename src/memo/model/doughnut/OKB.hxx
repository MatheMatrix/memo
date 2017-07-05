namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      template <typename Block>
      template <typename T>
      blocks::ValidationResult
      BaseOKB<Block>::_validate_version(
        blocks::Block const& other_,
        int T::*member,
        int version) const
      {
        ELLE_LOG_COMPONENT("memo.model.doughnut.OKB");
        auto other = dynamic_cast<T const*>(&other_);
        if (!other)
        {
          auto reason = elle::sprintf(
            "writing over a different block type (%s)",
            elle::demangle(typeid(other_).name()));
          ELLE_TRACE("%s: %s", *this, reason);
          return blocks::ValidationResult::failure(reason);
        }
        auto other_version = other->*member;
        if (version <= other_version)
        {
          auto reason = elle::sprintf(
            "version (%s) is not newer than stored version (%s)",
            version, other_version);
          ELLE_TRACE("%s: %s", *this, reason);
          return blocks::ValidationResult::conflict(reason);
        }
        else
        {
          ELLE_DUMP(
            "%s: version (%s) is newer than stored version (%s)",
            *this, version, other_version);
          return blocks::ValidationResult::success();
        }
      }
    }
  }
}
