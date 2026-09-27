#include "modbus_tcp_server.h"
#include "lwip/tcp.h"
#include "protocol_router.h"

#define MODBUS_TCP_PORT 502

static err_t modbus_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t modbus_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  modbus_tcp_err_cb(void *arg, err_t err);

void modbus_tcp_server_init(void)
{
    struct tcp_pcb *server_pcb = tcp_new();
    if(server_pcb != NULL){
        if(tcp_bind(server_pcb,IP_ADDR_ANY,MODBUS_TCP_PORT) == ERR_OK){
            server_pcb = tcp_listen(server_pcb);
            tcp_accept(server_pcb,modbus_tcp_accept_cb);
        }
    }
}

static err_t modbus_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, modbus_tcp_recv_cb);
    tcp_err(newpcb, modbus_tcp_err_cb);
    return ERR_OK;
}

static err_t modbus_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if(p == NULL){
        tcp_close(tpcb);
        return ERR_OK;
    }

    uint8_t resp_buf[256];
    size_t  resp_len = 0;

    int32_t status = protocol_router_dispatch((const uint8_t *)p->payload, p->len, resp_buf, &resp_len);

    if(status == ROUTER_OK && resp_len > 0){
        tcp_write(tpcb, resp_buf, (u16_t)resp_len, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    
    return ERR_OK;
}

static void modbus_tcp_err_cb(void *arg, err_t err)
{
  /* 处理连接异常中断（如拔线） */
}