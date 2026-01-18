#pragma once

#include<cstddef>
#include <yaml-cpp/yaml.h>

struct net_config{
    std::size_t link_bw, flow_bw;
    float rtt;

    static net_config parse_from_node(YAML::Node& node){
        net_config conf{};
        conf.link_bw = node["linkbw"].as<std::size_t>();
        conf.flow_bw = node["flow_bw"].as<std::size_t>();
        conf.rtt = node["rtt"].as<float>();
        return conf;
    }
};
