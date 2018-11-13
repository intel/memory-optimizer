#ifndef __OPTIONPARSER_H__
#define __OPTIONPARSER_H__

#include <string>
#include <yaml-cpp/yaml.h>
#include "Option.h"

class OptionParser
{
  public:
    OptionParser();
    ~OptionParser();
    
    int parse(std::string &filename, Option &option);

  private:
    int parse_option(YAML::Node &&option_node, Option &option);
    int parse_policies(YAML::Node &&policies_node, Option &option);
    void parse_one_policy(YAML::Node &&policy_node, Option &option);

    template<typename Tval>
    int get_value(const YAML::const_iterator  &iter,
                  const char* key_name, Tval &value)
    {
        std::string key = iter->first.as<std::string>();
        if (!key.compare(key_name))
        {
            value = iter->second.as<Tval>();
            return 1;
        }

        return 0;
    }
};



#endif
