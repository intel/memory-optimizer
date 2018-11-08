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
        
    } catch (...){
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
         ++iter)
    {            
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


