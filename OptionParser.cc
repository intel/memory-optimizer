#include <errno.h>
#include "OptionParser.h"


OptionParser::OptionParser()
  : Option()
{
}

OptionParser::~OptionParser()
{
}

int OptionParser::parse_file(std::string filename)
{
  int ret_val;

  config_file = filename;

  try {

    YAML::Node config = YAML::LoadFile(config_file);

    ret_val = parse_option(config["options"]);
    if (ret_val < 0)
      return ret_val;

    ret_val = parse_policies(config["policies"]);

  } catch (...) {
    ret_val = -1;
  }

  return ret_val;
}

int OptionParser::parse_option(YAML::Node &&option_node)
{
    if (!option_node)
      return -1;

    if (!option_node.IsMap())
      return -1;

    for (YAML::const_iterator iter = option_node.begin();
         iter != option_node.end();
         ++iter) {
#define OP_GET_VALUE(name, member) if (get_value(iter, name, member)) continue;
      OP_GET_VALUE("interval",        interval);
      OP_GET_VALUE("sleep",           sleep_secs);
      OP_GET_VALUE("loop",            nr_loops);
      OP_GET_VALUE("max_threads",     max_threads);
      OP_GET_VALUE("split_rss_size",  split_rss_size);
      OP_GET_VALUE("bandwidth_mbps",  bandwidth_mbps);
      OP_GET_VALUE("dram_percent",    dram_percent);
      OP_GET_VALUE("output",          output_file);
#undef OP_GET_VALUE
    }

    return 0;
}

int OptionParser::parse_policies(YAML::Node &&policies_node)
{
    if (!policies_node)
      return -1;

    if (!policies_node.IsSequence())
      return -1;

    for (std::size_t i = 0; i < policies_node.size(); ++i) {
      if (!policies_node[i].IsMap())
        continue;

      parse_one_policy(policies_node[i]);
    }

    return 0;
}

void OptionParser::parse_one_policy(YAML::Node &&policy_node)
{
    struct Policy new_policy;
    std::string str_val;

    for (auto iter = policy_node.begin();
         iter != policy_node.end();
         ++iter) {
      if (get_value(iter, "pid", new_policy.pid))
        continue;
      if (get_value(iter, "name", new_policy.name))
        continue;

      if (get_value(iter, "migration", str_val)) {
          new_policy.migrate_what
              = Option::parse_migrate_name(str_val);
          continue;
      }

      if (get_value(iter, "placement", str_val)) {
          new_policy.place_what
              = Option::parse_placement_name(str_val);
          continue;
      }
    }

    add_policy(new_policy);
}
