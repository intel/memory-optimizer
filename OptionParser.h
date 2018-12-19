#ifndef __OPTION_PARSER_H__
#define __OPTION_PARSER_H__

#include <string>
#include <yaml-cpp/yaml.h>
#include "Option.h"

// OptionParser only adds parsing functions.
//
// All data members and accessing functions are in Option,
// for easy access by other classes.
//
class OptionParser: public Option
{
  public:
    OptionParser();
    ~OptionParser();

    int parse_file(std::string filename);
    int reparse();

  private:
    int parse_option(YAML::Node &&option_node);
    int parse_policies(YAML::Node &&policies_node);
    void parse_one_policy(YAML::Node &&policy_node);
    void parse_common_policy(const YAML::const_iterator& iter, Policy& policy);
    void parse_numa_nodes(YAML::Node &numa_nodes);
    void parse_one_numa_node(YAML::Node &one_numa_node, NumaHWConfigEntry &one_entry);

    template<typename Tval>
    int get_value(const YAML::const_iterator  &iter,
                  const char* key_name, Tval &value);
    //specialize for YAML::Node as value type
    int get_value(const YAML::const_iterator  &iter,
                  const char* key_name, YAML::Node &value);

};

#endif
// vim:set ts=2 sw=2 et:
