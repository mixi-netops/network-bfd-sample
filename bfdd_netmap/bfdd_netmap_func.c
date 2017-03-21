/******************************************************************************/
/*! @addtogroup bfd service on c with netmap
 @file       bfdd_netmap_func.c
 @brief      netmapオペレーション関数
 ******************************************************************************/
#include "bfdd_netmap.h"

int move(process_handle_ptr ph, PACKET_EVENT event_func){
    u_int m = 0;
    uint16_t rxringidx = ph->nmd->first_rx_ring;
    uint16_t txringidx = ph->nmd->first_tx_ring;
    //
    while (rxringidx <= ph->nmd->last_rx_ring &&
           txringidx <= ph->nmd->last_tx_ring) {
        ph->rxring = NETMAP_RXRING(ph->nmd->nifp, rxringidx);
        ph->txring = NETMAP_TXRING(ph->nmd->nifp, txringidx);
        if (nm_ring_empty(ph->rxring)) { rxringidx++; continue; }
        if (nm_ring_empty(ph->txring)) { txringidx++; continue; }
        m += process_rings(ph, event_func);
    }
    return (m);
}
//
int process_rings(process_handle_ptr ph, PACKET_EVENT event_func) {
    u_int ret = 0;
    uint32_t limit = ph->rxring->num_slots;
    uint32_t space,rx_cur,tx_cur;
    if (ph->rxring->flags || ph->txring->flags){
        ph->rxring->flags = 0;
        ph->txring->flags = 0;
    }
    rx_cur = ph->rxring->cur;                  // RX
    tx_cur = ph->txring->cur;                  // TX
    //
    limit = MIN(limit, nm_ring_space(ph->rxring));
    limit = MIN(limit, nm_ring_space(ph->txring));
    // 
    while (limit-- > 0) {
        ph->rs = &ph->rxring->slot[rx_cur];
        ph->ts = &ph->txring->slot[tx_cur];
        ph->req = NETMAP_BUF(ph->rxring, ph->rs->buf_idx);
        ph->res = NETMAP_BUF(ph->txring, ph->ts->buf_idx);
        // prefetch the buffer for next loop.
        __builtin_prefetch(ph->req);

        ph->reslen = ph->rs->len;
        ph->reqlen = ph->rs->len;

        if (ph->ts->buf_idx < 2 || ph->rs->buf_idx < 2) {
            sleep(2);
        }
        if (ph->rs->len > 2048) {
            ph->rs->len = 0;
        }
        // fire event.
        if (event_func != NULL){
            if (is_bfd_packet(ph, ph->req, ph->rs->len) == 0){
                event_func(0, ph);
            }
        }
        rx_cur = nm_ring_next(ph->rxring, rx_cur);
        tx_cur = nm_ring_next(ph->txring, tx_cur);
        ret ++;
    }
    ph->rxring->head = ph->rxring->cur = rx_cur;
    ph->txring->head = ph->txring->cur = tx_cur;
    return (ret);
}
void swap_bfd(bfd_ptr bfd){
    uint32_t tmp_discr = bfd->your_discr;
    bfd->your_discr = bfd->my_discr;
    bfd->my_discr = tmp_discr;
}
void swap_ip(struct ip* ip,uint8_t ttl){
    struct in_addr tmp_addr = ip->ip_dst;
    ip->ip_dst = ip->ip_src;
    ip->ip_src = tmp_addr;
    ip->ip_ttl = ttl;
}
void swap_mac(struct ether_header* eh){
    uint8_t tmp_mac[ETHER_ADDR_LEN];
    memcpy(tmp_mac, eh->ether_dhost, ETHER_ADDR_LEN);
    memcpy(eh->ether_dhost, eh->ether_shost, ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, tmp_mac, ETHER_ADDR_LEN);
}
//
int is_bfd_packet(process_handle_ptr ph, char *buf, int len) {
    struct ether_header	*eh = (struct ether_header*)buf;
    struct ip           *ip = (struct ip*)(buf + sizeof(struct ether_header) + LEN_VLAN(eh));
    struct udphdr       *udp = (struct udphdr*)(buf + sizeof(struct ether_header) + LEN_VLAN(eh) + (ip->ip_hl * 4));
    int                 udplen = (sizeof(struct ether_header) + LEN_VLAN(eh) + (ip->ip_hl * 4) + sizeof(struct udphdr));
    // ARP
    if (len >= sizeof(struct ether_header) && eh->ether_type == htons(ETHERTYPE_ARP)) {
        return(-1);
    }
    // ICMP
    if (len >= (sizeof(struct ether_header) + sizeof(struct ip)) && eh->ether_type == htons(ETHERTYPE_IP) &&
        ip->ip_p == IPPROTO_ICMP &&
        ip->ip_ttl != 0x00){
        return(-1);
    }
    // BFD
    if (len >= udplen && ntohs(udp->uh_ulen) == (sizeof(bfd_t)+sizeof(struct udphdr)) && eh->ether_type == ntohs(ETHERTYPE_IP) && ip->ip_p == IPPROTO_UDP){
        if (ntohs(udp->uh_dport) == BFDDFLT_UDPPORT){
            return(0);
        }
    }
    return(-1);
}
// swap index ts -> rs for zero copy.
void swap_index(process_handle_ptr ph){
    uint32_t pkt = ph->ts->buf_idx;
    ph->ts->buf_idx = ph->rs->buf_idx;
    ph->rs->buf_idx = pkt;
    //
    ph->ts->len = ph->reqlen;
    ph->ts->flags |= NS_BUF_CHANGED;
    ph->rs->flags |= NS_BUF_CHANGED;
}
