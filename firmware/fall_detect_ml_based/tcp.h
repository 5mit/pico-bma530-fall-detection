#ifndef WIFI_TAG_TCP_H
#define WIFI_TAG_TCP_H

#define TCP_TIMEOUT 10 // Seconds until Wi-Fi Tag TCP message exchange is abandoned if no response is obtained.
#define POLL_INTERVAL 1 // half seconds between polling Wi-Fi Tag's TCP connection with server.
#define MS_BETWEEN_SEGMENTS 10 // Milliseconds between sending segments of Wi-Fi Tag outgoing JSON message.


// Enum for connection states
enum states {
    CONNECTING,
    CONNECTED,
    DATA_SEND_INIT,
    DATA_SENDING,
    DATA_SENT,
    DATA_SEND_FAILED,  
    // States which end tcp exchange loop 
    DATA_READY,        // Data has been received, and it is ready for processing.
    NOT_CONNECTED
};

struct connectionState {
    enum states state;
    struct altcp_pcb *pcb;
    bool init;
    char *message;
    char *recv_buff;
    int sent_len;
    int recv_len;
    unsigned tcp_timeout_ms;       // A constant value which defines the number of milliseconds to wait until timing out connection
    unsigned poll_interval_ms;     // A constant value which defines the number of milliseconds between polling the server connectioon
    unsigned lwip_poll_time_waited;     // number ms waited during poll intervals. Resets when server is connected and is used to determine when to time out connection
};
typedef struct connectionState connectionState;

err_t open_connection(connectionState *cs, const ip_addr_t *serverIP, u16_t port, char* message, char* recv_buff);
static err_t connected(void *arg, struct altcp_pcb *pcb, err_t err);
err_t close_connect(connectionState *cs);
err_t sent(void *arg, struct altcp_pcb *pcb, u16_t len);
err_t recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err);
void err(void *arg, err_t err);
err_t send_message(connectionState *cs);
err_t poll(void *arg, struct altcp_pcb *pcb);
err_t tcp_message_exchange(connectionState *cs, const ip_addr_t *serverIP, u16_t port, char*  message, char*  recv_buff);


#endif //WIFI_TAG_TCP_H
