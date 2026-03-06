// Copyright 2024-2026 coRAN LABS Private Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "ocudu_l1_interface.h"

#ifdef OCUDU_OCUDU

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../main/app_context.h"
#include "../core/OCUDU_OCUDU/pair0_master_pool.h"
#include "unified_logger.h"

#include <rte_ethdev.h>
#include <rte_ether.h>


static int ocudu_parse_mac(const char *s, uint8_t mac[6])
{
    unsigned int b[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 ||
        sscanf(s, "%x-%x-%x-%x-%x-%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; ++i) {
            if (b[i] > 0xff) return -1;
            mac[i] = (uint8_t)b[i];
        }
        return 0;
    }
    return -1;
}
#define OCUDU_SPLIT_NIC_RING_SIZE 4096
static int ocudu_split_setup_port(const char *pci_bdf,
                                  struct rte_mempool *rx_pool,
                                  uint16_t requested_mtu,
                                  int       attach_existing,
                                  uint16_t *port_id_out)
{
    uint16_t port_id = 0;
    int rc = rte_eth_dev_get_port_by_name(pci_bdf, &port_id);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_dev_get_port_by_name('%s') failed: %d",
                pci_bdf, rc);
        return -1;
    }


    if (attach_existing) {
        (void)rx_pool;  /* PRIMARY set the queue's pool; ours is unused. */
        *port_id_out = port_id;
        SM_Logs(LOG_INFO, _XFAPI_,
                "[OCUDU_SPLIT] NIC %s attached as port_id=%u "
                "(PRIMARY owns configure/queue/start; mtu requested=%u).",
                pci_bdf, port_id, requested_mtu);
        return 0;
    }

    struct rte_eth_conf eth_conf;
    memset(&eth_conf, 0, sizeof(eth_conf));

    rc = rte_eth_dev_configure(port_id, 1, 1, &eth_conf);
    if (rc < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_dev_configure(port=%u) failed: %d",
                port_id, rc);
        return -1;
    }


    if (requested_mtu > 0) {
        rc = rte_eth_dev_set_mtu(port_id, requested_mtu);
        if (rc < 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT] rte_eth_dev_set_mtu(port=%u, mtu=%u) failed: %d. "
                    "Frames may be truncated if the NIC's default MTU is below %u.",
                    port_id, requested_mtu, rc, requested_mtu);

        }
    }

    int socket_id = rte_eth_dev_socket_id(port_id);
    if (socket_id < 0) socket_id = SOCKET_ID_ANY;


    struct rte_eth_rxconf rx_conf;
    memset(&rx_conf, 0, sizeof(rx_conf));
    rx_conf.rx_thresh.wthresh = 0;
    rx_conf.rx_free_thresh    = 32;

    struct rte_eth_txconf tx_conf;
    memset(&tx_conf, 0, sizeof(tx_conf));
    tx_conf.tx_thresh.wthresh = 0;
    tx_conf.tx_rs_thresh      = 32;
    tx_conf.tx_free_thresh    = 32;

    rc = rte_eth_rx_queue_setup(port_id, 0,
                                OCUDU_SPLIT_NIC_RING_SIZE,
                                (unsigned)socket_id,
                                &rx_conf, rx_pool);
    if (rc < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_rx_queue_setup(port=%u) failed: %d",
                port_id, rc);
        return -1;
    }

    rc = rte_eth_tx_queue_setup(port_id, 0,
                                OCUDU_SPLIT_NIC_RING_SIZE,
                                (unsigned)socket_id, &tx_conf);
    if (rc < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_tx_queue_setup(port=%u) failed: %d",
                port_id, rc);
        return -1;
    }

    rc = rte_eth_dev_start(port_id);
    if (rc < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_dev_start(port=%u) failed: %d",
                port_id, rc);
        return -1;
    }

    /* Promiscuous keeps the NIC delivering our custom-ethertype frames
     * even if MAC filtering is otherwise strict. Failure is non-fatal. */
    (void)rte_eth_promiscuous_enable(port_id);

    *port_id_out = port_id;
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_SPLIT] NIC %s up as port_id=%u (rx=tx=%d desc, mtu=%u).",
            pci_bdf, port_id, OCUDU_SPLIT_NIC_RING_SIZE, requested_mtu);
    return 0;
}


static void ocudu_wait_peer_ready(xsm_handle_t *handle,
                                  const char *label,
                                  uint32_t budget_ms)
{
    uint32_t waited_ms = 0;
    const uint32_t poll_ms = 100;
    const uint32_t log_every_ms = 5000;
    uint32_t since_log_ms = 0;
    int warned = 0;
    while (XSM_IsPeerReady(handle) != XSM_OK) {
        usleep(poll_ms * 1000);
        waited_ms += poll_ms;
        since_log_ms += poll_ms;
        if (since_log_ms >= log_every_ms) {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[OCUDU_SPLIT] still waiting for %s peer (%us elapsed)…",
                    label, waited_ms / 1000);
            since_log_ms = 0;
        }
        if (!warned && waited_ms >= budget_ms) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT] %s peer not ready after %us; continuing to poll.",
                    label, budget_ms / 1000);
            warned = 1;
        }
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_SPLIT] %s peer ready after ~%us.",
            label, waited_ms / 1000);
}


static int ocudu_l1_init_none(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    const ocudu_xsm_endpoint_config_t* ep_l1 = &ctx->config.ocudu_xsm_l1;
    const ocudu_xsm_endpoint_config_t* ep_l2 = &ctx->config.ocudu_xsm_l2;
    OCUDUContext* oc = &ctx->ocudu_ctx;


    if (strncmp(ep_l1->device_name, ep_l2->device_name,
                XSM_DEVICE_NAME_MAX) != 0) {
        SM_Logs(LOG_WARN, _XFAPI_,
                "[OCUDU_BRIDGE] ocudu_xsm_l1.device_name='%s' != "
                "ocudu_xsm_l2.device_name='%s'. The translator topology "
                "expects a single shared memzone; using L1's name.",
                ep_l1->device_name, ep_l2->device_name);
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_BRIDGE] Opening xSM dual on memzone '%s' "
            "(pair 0 = master/toward L1, pair 1 = slave/toward L2)",
            ep_l1->device_name);

    xsm_config_t cfg_l1;
    memset(&cfg_l1, 0, sizeof(cfg_l1));
    strncpy(cfg_l1.device_name, ep_l1->device_name, XSM_DEVICE_NAME_MAX - 1);
    cfg_l1.role                  = XSM_ROLE_MASTER;
    cfg_l1.memory_size            = ep_l1->memory_size;  /* ignored for master */
    cfg_l1.queue_capacity         = ep_l1->queue_capacity;
    cfg_l1.return_queue_capacity  = ep_l1->return_queue_capacity;
    cfg_l1.wakeup_mode            = XSM_WAKEUP_POSIX_SEM;
    cfg_l1.pair_index             = 0;

    xsm_config_t cfg_l2;
    memset(&cfg_l2, 0, sizeof(cfg_l2));
    strncpy(cfg_l2.device_name, ep_l1->device_name, XSM_DEVICE_NAME_MAX - 1);
    cfg_l2.role                  = XSM_ROLE_SLAVE;
    cfg_l2.memory_size            = ep_l2->memory_size;
    cfg_l2.queue_capacity         = ep_l2->queue_capacity;
    cfg_l2.return_queue_capacity  = ep_l2->return_queue_capacity;
    cfg_l2.wakeup_mode            = XSM_WAKEUP_POSIX_SEM;
    cfg_l2.pair_index             = 1;

    xsm_status_t st = XSM_OpenDual(&cfg_l1, &cfg_l2, &oc->h_l1, &oc->h_l2);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] XSM_OpenDual failed: %s",
                xsm_strerror(st));
        return -1;
    }


    st = XSM_Alloc(oc->h_l1, /*size ignored on master*/0, &oc->region_l1);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] XSM_Alloc on h_l1 (pair 0 master) failed: %s",
                xsm_strerror(st));
        XSM_Close(oc->h_l1);
        XSM_Close(oc->h_l2);
        oc->h_l1 = NULL;
        oc->h_l2 = NULL;
        return -1;
    }


    oc->pair0_master_pool = pair0_master_pool_create(oc->region_l1, ep_l1->memory_size);
    if (oc->pair0_master_pool == NULL) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] pair0_master_pool_create failed");
        XSM_Free(oc->h_l1, oc->region_l1);
        XSM_Close(oc->h_l1);
        XSM_Close(oc->h_l2);
        oc->h_l1 = NULL;
        oc->h_l2 = NULL;
        return -1;
    }

    st = XSM_Alloc(oc->h_l2, ep_l2->memory_size, &oc->region_l2);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] XSM_Alloc on h_l2 (pair 1 slave) failed: %s",
                xsm_strerror(st));
        XSM_Free(oc->h_l1, oc->region_l1);
        XSM_Close(oc->h_l1);
        XSM_Close(oc->h_l2);
        oc->h_l1 = NULL;
        oc->h_l2 = NULL;
        return -1;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_BRIDGE] xSM dual ready. region_l1=%p region_l2=%p. "
            "Waiting for OCUDU-L1 (pair 0 slave already up) and OCUDU-L2 "
            "(pair 1 master) to attach.",
            oc->region_l1, oc->region_l2);


    while (XSM_IsPeerReady(oc->h_l1) != XSM_OK) {
        usleep(100000);  /* 100 ms */
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_BRIDGE] OCUDU-L1 peer attached on pair 0.");

    while (XSM_IsPeerReady(oc->h_l2) != XSM_OK) {
        usleep(100000);
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_BRIDGE] OCUDU-L2 peer attached on pair 1.");

    return 0;
}

static int ocudu_l1_init_split(struct AppContext* ctx)
{
    OCUDUContext* oc = &ctx->ocudu_ctx;
    const split_config_t* sp = &ctx->config.split;
    int is_l1 = (sp->role == SPLIT_L1);

    /* ---- 1. Memzone handle to the local OCUDU side ------------------- */
    const ocudu_xsm_endpoint_config_t* ep =
        is_l1 ? &ctx->config.ocudu_xsm_l1 : &ctx->config.ocudu_xsm_l2;

    xsm_config_t cfg_mz;
    memset(&cfg_mz, 0, sizeof(cfg_mz));
    strncpy(cfg_mz.device_name, ep->device_name, XSM_DEVICE_NAME_MAX - 1);
    cfg_mz.role                 = is_l1 ? XSM_ROLE_MASTER : XSM_ROLE_SLAVE;
    cfg_mz.memory_size          = ep->memory_size;
    cfg_mz.queue_capacity       = ep->queue_capacity;
    cfg_mz.return_queue_capacity= ep->return_queue_capacity;
    cfg_mz.wakeup_mode          = XSM_WAKEUP_POSIX_SEM;

    cfg_mz.pair_index           = 0u;
    cfg_mz.num_pairs            = 1u;
    cfg_mz.transport            = XSM_TRANSPORT_MEMZONE;

    xsm_handle_t* h_mz = NULL;
    xsm_status_t st = XSM_Open(&cfg_mz, &h_mz);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] XSM_Open (memzone, role=%s) failed: %s",
                is_l1 ? "master" : "slave", xsm_strerror(st));
        return -1;
    }
    if (is_l1) {
        oc->h_l1 = h_mz;
    } else {
        oc->h_l2 = h_mz;
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_SPLIT] Memzone xSM handle open (device='%s', pair=%u).",
            ep->device_name, cfg_mz.pair_index);

    xsm_config_t cfg_eth;
    memset(&cfg_eth, 0, sizeof(cfg_eth));

    strncpy(cfg_eth.device_name, ep->device_name, XSM_DEVICE_NAME_MAX - 1);
    cfg_eth.role                 = cfg_mz.role;   /* logical role mirrors */
    cfg_eth.queue_capacity       = ep->queue_capacity;
    cfg_eth.return_queue_capacity= ep->return_queue_capacity;
    cfg_eth.wakeup_mode          = XSM_WAKEUP_POSIX_SEM;
    cfg_eth.transport            = XSM_TRANSPORT_DPDK_ETH;
    cfg_eth.eth.mtu              = sp->mtu;
    cfg_eth.eth.rx_lcore_id      = sp->rx_lcore_id;
    strncpy(cfg_eth.eth.local_memzone_name,
            sp->local_memzone_name,
            XSM_DEVICE_NAME_MAX - 1);
    cfg_eth.eth.local_memzone_size = sp->local_memzone_size;

    cfg_eth.eth.attach_existing = (sp->role == SPLIT_L1) ? 1u : 0u;

    if (ocudu_parse_mac(sp->peer_mac, cfg_eth.eth.peer_mac) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] Bad peer_mac '%s'; expected AA:BB:CC:DD:EE:FF",
                sp->peer_mac);
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }


    uint16_t port_id_pre = 0;
    int rc = rte_eth_dev_get_port_by_name(sp->local_pci, &port_id_pre);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] rte_eth_dev_get_port_by_name('%s') failed: %d",
                sp->local_pci, rc);
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }
    cfg_eth.eth.local_port_id = port_id_pre;

    xsm_handle_t* h_eth = NULL;
    st = XSM_Open(&cfg_eth, &h_eth);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] XSM_Open (DPDK-Eth) failed: %s",
                xsm_strerror(st));
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }
    oc->h_eth = h_eth;

    {
        struct rte_ether_addr local_addr;
        if (rte_eth_macaddr_get(port_id_pre, &local_addr) == 0) {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[OCUDU_SPLIT] DPDK-Eth xSM handle open (port_id=%u, "
                    "local_mac=%02x:%02x:%02x:%02x:%02x:%02x, peer_mac=%s).",
                    port_id_pre,
                    local_addr.addr_bytes[0], local_addr.addr_bytes[1],
                    local_addr.addr_bytes[2], local_addr.addr_bytes[3],
                    local_addr.addr_bytes[4], local_addr.addr_bytes[5],
                    sp->peer_mac);
        } else {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[OCUDU_SPLIT] DPDK-Eth xSM handle open (port_id=%u, "
                    "peer_mac=%s; local_mac read failed).",
                    port_id_pre, sp->peer_mac);
        }
    }

    struct rte_mempool* rx_pool = XSM_GetRxMempool(h_eth);
    if (!rx_pool) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] XSM_GetRxMempool returned NULL");
        XSM_Close(h_eth); oc->h_eth = NULL;
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }
    uint16_t port_id = 0;

    uint16_t nic_mtu = (sp->mtu != 0) ? sp->mtu : 8192;
    int attach_existing = (sp->role == SPLIT_L1) ? 1 : 0;
    if (ocudu_split_setup_port(sp->local_pci, rx_pool, nic_mtu,
                               attach_existing, &port_id) != 0) {
        XSM_Close(h_eth); oc->h_eth = NULL;
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }
    oc->split_port_id = port_id;

    void* region_mz = NULL;
    st = XSM_Alloc(h_mz, ep->memory_size, &region_mz);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] XSM_Alloc on memzone handle failed: %s",
                xsm_strerror(st));
        rte_eth_dev_stop(port_id);
        XSM_Close(h_eth); oc->h_eth = NULL;
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }
    if (is_l1) {
        oc->region_l1 = region_mz;

        oc->pair0_master_pool =
            pair0_master_pool_create(oc->region_l1, ep->memory_size);
        if (oc->pair0_master_pool == NULL) {
            SM_Logs(LOG_CRTERR, _XFAPI_,
                    "[OCUDU_SPLIT] pair0_master_pool_create failed");
            XSM_Free(h_mz, oc->region_l1);
            rte_eth_dev_stop(port_id);
            XSM_Close(h_eth); oc->h_eth = NULL;
            XSM_Close(h_mz); oc->h_l1 = NULL;
            return -1;
        }
    } else {
        oc->region_l2 = region_mz;
    }

    st = XSM_Alloc(h_eth, sp->local_memzone_size, &oc->region_eth_va);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT] XSM_Alloc on DPDK-Eth handle failed: %s",
                xsm_strerror(st));
        if (is_l1 && oc->pair0_master_pool) {
            pair0_master_pool_destroy(oc->pair0_master_pool);
            oc->pair0_master_pool = NULL;
        }
        XSM_Free(h_mz, region_mz);
        rte_eth_dev_stop(port_id);
        XSM_Close(h_eth); oc->h_eth = NULL;
        XSM_Close(h_mz);
        if (is_l1) oc->h_l1 = NULL; else oc->h_l2 = NULL;
        return -1;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_SPLIT] Both handles allocated. Waiting for peers "
            "(memzone OCUDU side + remote XFAPI via NIC %s).",
            sp->local_pci);

    ocudu_wait_peer_ready(h_mz,  is_l1 ? "OCUDU-L1" : "OCUDU-L2",
                          120u * 1000u);
    ocudu_wait_peer_ready(h_eth, "split-XFAPI", 120u * 1000u);

    return 0;
}

static void ocudu_l1_destroy(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    OCUDUContext* oc = &ctx->ocudu_ctx;

    if (oc->pair0_master_pool != NULL) {
        pair0_master_pool_destroy(oc->pair0_master_pool);
        oc->pair0_master_pool = NULL;
    }


    if (oc->h_eth != NULL) {
        if (oc->region_eth_va != NULL) {
            XSM_Free(oc->h_eth, oc->region_eth_va);
            oc->region_eth_va = NULL;
        }
        XSM_Close(oc->h_eth);
        oc->h_eth = NULL;
    }
    if (ctx->config.split.role != SPLIT_NONE) {
        (void)rte_eth_dev_stop(oc->split_port_id);
    }


    if (oc->h_l1 != NULL) {
        if (oc->region_l1 != NULL) {
            XSM_Free(oc->h_l1, oc->region_l1);
            oc->region_l1 = NULL;
        }
        XSM_Close(oc->h_l1);
        oc->h_l1 = NULL;
    }
    if (oc->h_l2 != NULL) {
        if (oc->region_l2 != NULL) {
            XSM_Free(oc->h_l2, oc->region_l2);
            oc->region_l2 = NULL;
        }
        XSM_Close(oc->h_l2);
        oc->h_l2 = NULL;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_BRIDGE] xSM endpoints closed.");
}


static int ocudu_l1_init(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    switch (ctx->config.split.role) {
        case SPLIT_NONE:
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[OCUDU_BRIDGE] split.role=none -> single-host bridge init.");
            return ocudu_l1_init_none(ctx);
        case SPLIT_L1:
        case SPLIT_L2:
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[OCUDU_BRIDGE] split.role=%s -> split-mode init.",
                    ctx->config.split.role == SPLIT_L1 ? "L1" : "L2");
            return ocudu_l1_init_split(ctx);
        default:
            SM_Logs(LOG_CRTERR, _XFAPI_,
                    "[OCUDU_BRIDGE] unknown split.role=%d",
                    (int)ctx->config.split.role);
            return -1;
    }
}

static const OCUDU_L1_Interface g_ocudu_l1_interface = {
    .init     = ocudu_l1_init,
    .send_msg = NULL,
    .destroy  = ocudu_l1_destroy,
};

const OCUDU_L1_Interface* get_ocudu_l1_interface(void)
{
    return &g_ocudu_l1_interface;
}

#endif /* OCUDU_OCUDU */
