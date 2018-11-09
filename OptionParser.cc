#include <errno.h>
#include "OptionParser.h"


OptionParser::OptionParser()
{

}


OptionParser::~OptionParser()
{

}


int OptionParser::Parse(std::string &filename, Option &option)
{
  int ret_val;

  try {

    YAML::Node config = YAML::LoadFile(filename);
        
    ret_val = parse_option(config["options"], option);
    if (ret_val >= 0) {
      ret_val = parse_policies(config["policies"], option);
    }

  } catch (...) {
    ret_val = -1;
  }

  return ret_val;
}

int OptionParser::parse_option(YAML::Node &&option_node,
                               Option &option)
{
    if (!option_node)
      return -1;
    
    for (YAML::const_iterator iter = option_node.begin();
         iter != option_node.end();
         ++iter) {
      if (get_value(iter, "interval", option.interval))
        continue;
      if (get_value(iter, "sleep", option.sleep_secs))
        continue;
      if (get_value(iter, "loop", option.nr_loops))
        continue;
      if (get_value(iter, "bandwidth_mbps", option.bandwidth_mbps))
        continue;
      if (get_value(iter, "dram_percent", option.dram_percent))
        continue;
      if (get_value(iter, "output", option.output_file))
        continue;
    }

    return 0;
}


int OptionParser::parse_policies(YAML::Node &&policies_node, Option &option)
{
    if (!policies_node)
      return -1;

    if (!policies_node.IsSequence())
      return -1;

    for (std::size_t i = 0; i < policies_node.size(); ++i) {
      if (!policies_node[i].IsMap())
        continue;

      parse_one_policy(policies_node[i], option);
    }

    return 0;
}


void OptionParser::parse_one_policy(YAML::Node &&policy_node, Option &option)
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

    option.policies.push_back(new_policy);
}
