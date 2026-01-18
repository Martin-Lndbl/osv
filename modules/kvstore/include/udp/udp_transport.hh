#pragma once
#include "../transport.hh"
#include "modules/kvstore/header.hh"
#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <bypass/net.hh>
#include <cstddef>
#include <cstdint>

namespace transport{
class udp_transport : transport<udp_transport>{
    public:
        udp_transport(rte_eth_dev* dev, uint16_t tx_qid, uint16_t rx_qid): transport(), dev(dev), tx_qid(tx_qid), rx_qid(rx_qid) {}
        ~udp_transport() override = default;
    private:
        void tx_burst(){
            auto sent = dev->tx_burst(tx_qid, tx_buf.buf.data(), tx_buf.ptr);
            tx_buf.tx_cb(sent);
        }

        void rx_burst(){
            auto rcvd = dev->rx_burst(rx_qid, rx_buf.buf.data(), rx_buf.ptr);
            rx_buf.rx_cb(rcvd);
        }

        static std::pair<rte_mbuf*, char*> data(rte_mbuf* pkt) {
            return {pkt, pkt->buf + sizeof(rte_eth_header) + sizeof(ipv4_header) + sizeof(udp_header)};
        }

        rte_eth_dev* dev; 
        uint16_t tx_qid, rx_qid;
};

};
