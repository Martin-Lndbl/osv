#include "../include/net.hh"


rte_eth_conf net_if::conf = {
    .rxmode = {.offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM},
    .txmode = {.offloads = RTE_ETH_TX_OFFLOAD_UDP_CKSUM |
                           RTE_ETH_TX_OFFLOAD_IPV4_CKSUM}};
