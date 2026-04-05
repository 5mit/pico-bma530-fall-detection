#include <lwip/err.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp.h"
#include "tcp.h"
#include "common.h"
#include "hardware/watchdog.h"


bool timed_out = false;

// tcp.c

static err_t close_connection_locked(connectionState *cs) {
    err_t err = ERR_OK;

    // Always force state to NOT_CONNECTED when caller requests close
    if (!cs) return ERR_VAL;

    if (cs->pcb != NULL) {
        altcp_arg(cs->pcb, NULL);
        altcp_poll(cs->pcb, NULL, 0);
        altcp_sent(cs->pcb, NULL);
        altcp_recv(cs->pcb, NULL);
        altcp_err(cs->pcb, NULL);

        if (altcp_close(cs->pcb) != ERR_OK) {
            altcp_abort(cs->pcb);
            err = ERR_ABRT;
        }
        cs->pcb = NULL;
    }

    cs->state = NOT_CONNECTED;
    return err;
}


err_t close_connection(connectionState *cs) {
    // Use this ONLY from non-LWIP thread context (your main loop code)
    cyw43_arch_lwip_begin();
    err_t err = close_connection_locked(cs);
    cyw43_arch_lwip_end();
    return err;
}

err_t recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t lwip_err) {
    
    connectionState *cs = (connectionState*)arg;
    debug_printf("Recv called\tstate:%d\n", cs->state);
    if (!cs) {
        if (p) { altcp_recved(pcb, p->tot_len); pbuf_free(p); }
        return ERR_VAL;
    }

    // Remote closed
    if (!p) {
        debug_printf("Client closed connection\n");
        close_connection_locked(cs);
        return ERR_OK;
    }

    // lwIP error on receive
    if (lwip_err != ERR_OK) {
        debug_printf("recv lwip err=%d\n", lwip_err);
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        close_connection_locked(cs);
        cs->state = NOT_CONNECTED;
        return ERR_OK;
    }

    // If we've already got a complete message, ignore further data
    // (prevents overwriting cs->recv_buff while your main loop reads it)
    if (cs->state == DATA_READY) {
        debug_printf("Already a complete msg\n");
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    size_t have = cs->recv_len;
    size_t cap  = RECV_BUFF_SIZE - 1;  // keep room for '\0'

    // Copy as much as we can
    size_t can_take = (have < cap) ? (cap - have) : 0;
    size_t take = (p->tot_len < can_take) ? p->tot_len : can_take;

    if (take > 0) {
        pbuf_copy_partial(p, cs->recv_buff + have, (u16_t)take, 0);
        have += take;
        cs->recv_len = have;
        cs->recv_buff[have] = '\0'; // safe for debug prints
    }

    // Tell lwIP we've consumed the pbuf
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Overflow: buffer too small for whatever was received
    if (take < (size_t)p->tot_len) {
        debug_printf("recv overflow (tot=%u take=%u). closing\n",
                     (unsigned)p->tot_len, (unsigned)take);
        close_connection_locked(cs);
        cs->state = NOT_CONNECTED;
        return ERR_OK;
    }
    debug_printf("recv: %s\n", cs->recv_buff);
    // Look for a complete JSON object in the accumulated buffer: '{' ... '}'
    char *start = (char*)memchr(cs->recv_buff, '{', cs->recv_len);
    if (start) {
        char *end = (char*)memchr(start, '}', (cs->recv_buff + cs->recv_len) - start);
        if (end) {
            size_t json_len = (size_t)(end - start + 1);

            // If JSON doesn't start at buffer[0], shift it down
            if (start != cs->recv_buff) {
                memmove(cs->recv_buff, start, json_len);
            }

            cs->recv_buff[json_len] = '\0';
            cs->recv_len = json_len;

            cs->state = DATA_READY;
        }
    }
    debug_printf("recv end correctly: state: %d\n", cs->state);
    return ERR_OK;
}



err_t sent(void *arg, struct altcp_pcb *pcb, u16_t len) {
    connectionState *cs = (connectionState*)arg;
    debug_printf("data sent: %d\n", len);
    cs->sent_len += len;
    return ERR_OK;
}


void err(void *arg, err_t err) {
    connectionState *cs = (connectionState*)arg;
    debug_printf("tcp err: %d\n", err);
    close_connection_locked(cs);       // IMPORTANT: no lwip_begin/end here
}


static err_t connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    connectionState *cs = (connectionState*)arg;
    
    if (err != ERR_OK) {
        debug_printf("Connect failed err=%d\n", err);
        close_connection_locked(cs);
        cs->state = NOT_CONNECTED;
        return err;
    }

    debug_printf("Connected to server.\n");

    cs->state = CONNECTED;
    cs->lwip_poll_time_waited = 0;
    return ERR_OK;
}

err_t send_message(connectionState *cs) {
    debug_printf("Send msg called\n");
    if (!cs || !cs->pcb) {
        debug_printf("Data Send failed\n");
        cs->state = DATA_SEND_FAILED;
        return ERR_CONN;
    }
    static_assert(TCP_SND_BUF <= MEM_SIZE); 
    cs->state = DATA_SENDING;
    cs->sent_len = 0;
    err_t err = ERR_OK;
    int totalLen = TCP_MSG_SIZE; // How many bytes to send
    int remainingLen = totalLen; // How many bytes are left to write
    int segSize; // size of write
    char* ptr = cs->message;



    while (remainingLen > 0 && err == ERR_OK) {
        if (!cs->pcb) { err = ERR_CONN; break; }
        if (cs->sent_len == totalLen - remainingLen) {
            for (int i = 0; i < (TCP_SND_BUF / TCP_MSS) && remainingLen > 0 && err == ERR_OK; ++i) {
                cyw43_arch_lwip_begin();
                if (!cs->pcb) {
                    err = ERR_CONN;
                    break;
                }
                segSize = remainingLen > TCP_MSS? TCP_MSS: remainingLen; // cap size of segment to write to max segment size
                err = altcp_write(cs->pcb, ptr, segSize, TCP_WRITE_FLAG_COPY); // LWIP Copy buffer then write
                cyw43_arch_lwip_end();
                if (err != ERR_OK) {
                    debug_printf("Error %d: while writing message segment %d.\n", err, i);
                    break;
                }
                else {
                    debug_printf("Wrote segment: %d ptr: %p Remaining len: %d\n", i, ptr, remainingLen);
                    ptr += segSize;
                    remainingLen -= segSize;
                }
                sleep_ms(MS_BETWEEN_SEGMENTS);
            }
            if (err == ERR_OK) {
                if (!cs->pcb) {
                    err = ERR_CONN;
                    break;
                }
                else {
                    cyw43_arch_lwip_begin();
                    err = altcp_output(cs->pcb);
                    cyw43_arch_lwip_end();
                    if (err != ERR_OK) {
                        debug_printf("Error %d: while outputting message.\n", err);
                    }   
                }
            }

        }
        cyw43_arch_poll();
        sleep_ms(MS_BETWEEN_SEGMENTS);
    }
    // If data sent without error, update state to match, otherwise force a timeout
    cs->state = err == ERR_OK? DATA_SENT: DATA_SEND_FAILED;
    cs->lwip_poll_time_waited = 0;
    
    return err;
}

err_t poll(void *arg, struct altcp_pcb *pcb) {
    connectionState *cs = (connectionState*)arg;
    cs->lwip_poll_time_waited += cs->poll_interval_ms;
    watchdog_update();

    switch(cs->state) {
        case DATA_SENT:
        case CONNECTING:
            if (cs->lwip_poll_time_waited >= cs->tcp_timeout_ms) {
                debug_printf("TCP timed out.\n");
                timed_out = true; 
                close_connection_locked(cs);
                return ERR_TIMEOUT;
            }
            break;

        case DATA_SEND_FAILED:
            debug_printf("Send failed.\n");
            return close_connection_locked(cs); 

        default:
            break;
    }

    return ERR_OK;
}

err_t new_connection_locked(connectionState *cs, char *message, char *buff) {
    if (cs->init) {
        close_connection_locked(cs);   // NOT close_connection()
    }
    cs->state = NOT_CONNECTED;
    cs->pcb = altcp_new(NULL);
    if (!cs->pcb) return ERR_VAL;

    cs->message = message;
    cs->recv_buff = buff;
    cs->recv_len = 0;
    cs->tcp_timeout_ms = TCP_TIMEOUT * 1000;
    cs->poll_interval_ms = POLL_INTERVAL * 500;
    cs->lwip_poll_time_waited = 0;
    

    altcp_recv(cs->pcb, recv);
    altcp_sent(cs->pcb, sent);
    altcp_err(cs->pcb, err);
    altcp_poll(cs->pcb, poll, POLL_INTERVAL);
    altcp_arg(cs->pcb, cs);
    
    cs->init = true;
    timed_out = false;
    return ERR_OK;
}

err_t open_connection(connectionState *cs, const ip_addr_t *serverIP, u16_t port, char *message, char *recv_buff) {
    cyw43_arch_lwip_begin();

    err_t err = ERR_OK;
    if (cs->state != DATA_READY) {
        err = new_connection_locked(cs, message, recv_buff);
        if (err == ERR_OK) {
            cs->state = CONNECTING;
            err = altcp_connect(cs->pcb, serverIP, port, connected);
            if (err != ERR_OK) {
                close_connection_locked(cs);
            }
        }
    } else {
        cs->state = CONNECTED;
    }

    cyw43_arch_lwip_end();
    return err;
}


err_t tcp_message_exchange(connectionState *cs, const ip_addr_t *serverIP, u16_t port, char*  message, char* recv_buff) {
    // Ensure connection is open
    if (open_connection(cs, serverIP, port, message, recv_buff) != ERR_OK) {
        return ERR_ABRT;
    }
#ifndef NDEBUG
    enum states prev_state = cs->state;
#endif

    // Ensure poll time and total time trackers are reset
    unsigned int poll_time_waited = 0;
    while (cs->state < DATA_READY) { // Until the tag disconnects or completes message exchange from the server
        cyw43_arch_poll();
        watchdog_update();
        
        // Redundant state polling (same functionality as lwip poll callback: lwip_poll_time_waited) -> used in case lwip ever hangs
        switch(cs->state) {
            case DATA_SEND_FAILED:
                close_connection(cs);     // non-lwip context wrapper
                return ERR_ABRT;
                break;

            case NOT_CONNECTED:
                if (timed_out) {
                    return ERR_TIMEOUT;
                }
                return ERR_CONN;
                break;
            
            case CONNECTED :
                cyw43_arch_lwip_begin();
                cs->state = DATA_SEND_INIT;
                poll_time_waited = 0;
                cs->lwip_poll_time_waited = 0;
                cyw43_arch_lwip_end();
                break;

            case CONNECTING:
            case DATA_SENT:
                debug_printf("%s: %lu\n", cs->state == CONNECTING? "Waiting for connection to server": "Waiting for response from server", cs->lwip_poll_time_waited);
                sleep_ms(100);
                poll_time_waited += 100;
                if (poll_time_waited >= cs->tcp_timeout_ms) {
                    close_connection(cs);
                    timed_out = true;
                    return ERR_TIMEOUT;
                }
                break;

            case DATA_SEND_INIT:
                send_message(cs);
                poll_time_waited = 0;
                break;
            case DATA_SENDING:
                debug_printf("Invalid state in main loop");
                close_connection(cs);
                return ERR_CONN;
            case DATA_READY:
                break;
            default:
                tight_loop_contents();
        }
    debug_printf("state: %d\n", cs->state);
    #ifndef NDEBUG
        if (cs->state != prev_state) {
            debug_printf("state: %d\n", cs->state);
            prev_state = cs->state;
        }
    #endif
    }
    
    debug_printf("final state: %d\n", cs->state);
    if (cs->state == NOT_CONNECTED) {
        if (timed_out) {
            return ERR_TIMEOUT;
        }
        return ERR_CONN;
    }
    if (cs->state == DATA_READY) {
        debug_printf("Data transfer complete.\nResponse Buffer = %s\n", cs->recv_buff);
    }
    return ERR_OK;

}