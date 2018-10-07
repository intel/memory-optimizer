#ifndef AEP_FORMATTER_H
#define AEP_FORMATTER_H

// source: https://stackoverflow.com/questions/12247646/c-delay-printf-until-needed

#include <cstdarg>
#include <cstdio>
#include <string>

class Formatter
{
  private:
    std::string buf;

  public:

    void clear() { buf.clear(); }
    bool empty() { return buf.empty(); }
    void reserve(size_t capacity) { buf.reserve(capacity); }
    std::string const & str() const { return buf; }

    void print(char const * fmt, ...)
    {
      int ret1;
      int ret2;
      std::size_t cur;
      std::va_list ap, aq;
      va_start(ap, fmt);
      va_copy(aq, ap);

      ret1 = std::vsnprintf(NULL, 0, fmt, ap);
      if (ret1 < 0)
        goto out;

      cur = buf.size();
      buf.resize(cur + ret1 + 1);

      ret2 = std::vsnprintf(&buf[cur], ret1 + 1, fmt, aq);
      if (ret2 < 0)
        goto out;
      buf.resize(cur + ret1);

out:
      va_end(aq);
      va_end(ap);
    }
};

#endif
// vim:set ts=2 sw=2 et:
