
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/ip4_addr.h"
#include "lwip/apps/mdns.h"
#include "lwip/init.h"
#include "lwip/apps/httpd.h"

#include "webapp.h"
#include "trs80.h"
#include "settings.h"

/*

    What could this do:
        Select game to play.
        Control the game, or at least type stuff in the menus.
        Control brightness of display (via backlight).

 */

/**
 * Convert a TRS-80 graphics character between 128 and 191 inclusive to an
 * equivalent UCS-2 Unicode braille character.
 */
static uint16_t trs80ToBraille(uint8_t ch) {
    if (ch == 0 || ch == 32) {
        ch = 128;
    }

    // Remap bits.
    uint8_t ul = (ch >> 0) & 0x01;
    uint8_t ur = (ch >> 1) & 0x01;
    uint8_t cl = (ch >> 2) & 0x01;
    uint8_t cr = (ch >> 3) & 0x01;
    uint8_t ll = (ch >> 4) & 0x01;
    uint8_t lr = (ch >> 5) & 0x01;
    ch = (ul << 0) | (cl << 1) | (ll << 2) | (ur << 3) | (cr << 4) | (lr << 5);

    // Convert to braille.
    return 0x2800 + ch;
}

#if LWIP_MDNS_RESPONDER
static void srv_txt(struct mdns_service *service, void *txt_userdata)
{
  err_t res;
  LWIP_UNUSED_ARG(txt_userdata);

  res = mdns_resp_add_service_txtitem(service, "path=/", 6);
  LWIP_ERROR("mdns add service txt failed\n", (res == ERR_OK), return);
}
#endif

static const char *cgi_handler_test(int iIndex __attribute__((unused)),
        int iNumParams __attribute__((unused)),
        char *pcParam[] __attribute__((unused)),
        char *pcValue[] __attribute__((unused))) {

    return "/index.shtml";
}

static tCGI cgi_handlers[] = {
    { "/", cgi_handler_test },
};

// Note that the buffer size is limited by LWIP_HTTPD_MAX_TAG_INSERT_LEN, so
// use LWIP_HTTPD_SSI_MULTIPART to return larger amounts of data
u16_t ssi_example_ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
    , uint16_t current_tag_part, uint16_t *next_tag_part
#endif
) {
    size_t printed;

    switch (iIndex) {
        case 0: { // "screen"
            uint16_t address = 15360 + current_tag_part;
            uint8_t byte = readMemoryByte(address);
            uint16_t ch = trs80ToBraille(byte);
            const char *newline = current_tag_part % 64 == 63 ? "\n" : "";
            printed = snprintf(pcInsert, iInsertLen, "&#%u;%s", ch, newline);
            if (current_tag_part < 1023) {
                *next_tag_part = current_tag_part + 1;
            }
            break;
        }
        case 1: { // "clrw"
            printed = snprintf(pcInsert, iInsertLen,
                    getSettingsColor() == COLOR_WHITE ? "checked" : "");
            break;
        }
        case 2: { // "clrg"
            printed = snprintf(pcInsert, iInsertLen,
                    getSettingsColor() == COLOR_GREEN ? "checked" : "");
            break;
        }
        case 3: { // "clra"
            printed = snprintf(pcInsert, iInsertLen,
                    getSettingsColor() == COLOR_AMBER ? "checked" : "");
            break;
        }
        case 4: { // "sndoff"
            printed = snprintf(pcInsert, iInsertLen,
                    getSettingsSound() == SOUND_OFF ? "checked" : "");
            break;
        }
        case 5: { // "sndon"
            printed = snprintf(pcInsert, iInsertLen,
                    getSettingsSound() == SOUND_ON ? "checked" : "");
            break;
        }
        default: { // unknown tag
            printed = 0;
            break;
        }
    }

    return (u16_t) printed;
}

// Be aware of LWIP_HTTPD_MAX_TAG_NAME_LEN
static const char *ssi_tags[] = {
    "screen",
    "clrw",
    "clrg",
    "clra",
    "sndoff",
    "sndon",
};

#if LWIP_HTTPD_SUPPORT_POST
static void *current_connection;

err_t httpd_post_begin(void *connection, const char *uri,
        const char *http_request __attribute__((unused)),
        u16_t http_request_len __attribute__((unused)),
        int content_len __attribute__((unused)),
        char *response_uri, u16_t response_uri_len, u8_t *post_auto_wnd) {

    if (memcmp(uri, "/", 1) == 0 && current_connection != connection) {
        current_connection = connection;
        snprintf(response_uri, response_uri_len, "/ledfail.shtml");
        *post_auto_wnd = 1;
        return ERR_OK;
    }

    return ERR_VAL;
}

// Return a value for a parameter
static char *httpd_param_value(struct pbuf *p, const char *param_name, char *value_buf, size_t value_buf_len) {
    size_t param_len = strlen(param_name);
    u16_t param_pos = pbuf_memfind(p, param_name, param_len, 0);
    if (param_pos != 0xFFFF) {
        u16_t param_value_pos = param_pos + param_len;
        u16_t param_value_len = 0;
        u16_t tmp = pbuf_memfind(p, "&", 1, param_value_pos);
        if (tmp != 0xFFFF) {
            param_value_len = tmp - param_value_pos;
        } else {
            param_value_len = p->tot_len - param_value_pos;
        }
        if (param_value_len > 0 && param_value_len < value_buf_len) {
            char *result = (char *)pbuf_get_contiguous(p, value_buf, value_buf_len, param_value_len, param_value_pos);
            if (result) {
                result[param_value_len] = 0;
                return result;
            }
        }
    }
    return NULL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    err_t ret = ERR_VAL;
    LWIP_ASSERT("NULL pbuf", p != NULL);
    if (current_connection == connection) {
        char buf[10];
        char *val = httpd_param_value(p, "color=", buf, sizeof(buf));
        if (val) {
            if (strcmp(val, "white") == 0) {
                setSettingsColor(COLOR_WHITE);
            } else if (strcmp(val, "green") == 0) {
                setSettingsColor(COLOR_GREEN);
            } else if (strcmp(val, "amber") == 0) {
                setSettingsColor(COLOR_AMBER);
            }
            ret = ERR_OK;
        }
        val = httpd_param_value(p, "sound=", buf, sizeof(buf));
        if (val) {
            if (strcmp(val, "off") == 0) {
                setSettingsSound(SOUND_OFF);
            } else if (strcmp(val, "on") == 0) {
                setSettingsSound(SOUND_ON);
            }
            ret = ERR_OK;
        }
    }
    pbuf_free(p);
    return ret;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    if (current_connection == connection) {
        snprintf(response_uri, response_uri_len, "/");
    } else {
        snprintf(response_uri, response_uri_len, "/ledfail.shtml");
    }
    current_connection = NULL;
}
#endif

bool initWebapp() {
    if (cyw43_arch_init()) {
        printf("Failed to initialise\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();

    const char *hostname = "model3";
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], hostname);

    printf("Connecting to WiFi...\n");
    int err = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (err != 0) {
        printf("Failed to connect to Wi-Fi.\n");
        switch (err) {
            case PICO_ERROR_TIMEOUT:
                printf("Connection timeout.\n");
                break;
            case PICO_ERROR_BADAUTH:
                printf("Invalid Wi-Fi password.\n");
                break;
            case PICO_ERROR_CONNECT_FAILED:
                printf("Unknown reason.\n");
                break;
            default:
                printf("Unexpected reason (%d).\n", err);
                break;
        }
        return false;
    } else {
        printf("Connected.\n");
    }
    printf("Ready, running httpd at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

#if LWIP_MDNS_RESPONDER
    // Setup mdns
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    printf("MDNS host name: %s.local\n", hostname);
#if LWIP_VERSION_MAJOR > 2 || (LWIP_VERSION_MAJOR == 2 && LWIP_VERSION_MINOR >= 2)
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname);
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP, 80, srv_txt, NULL);
#else
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname, 60);
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP, 80, 60, srv_txt, NULL);
#endif
    cyw43_arch_lwip_end();
#endif

    // setup http server
    cyw43_arch_lwip_begin();
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    http_set_ssi_handler(ssi_example_ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
    cyw43_arch_lwip_end();

    return true;
}

void pollWebapp() {
    cyw43_arch_poll();
}

void exitWebapp() {
#if LWIP_MDNS_RESPONDER
    mdns_resp_remove_netif(&cyw43_state.netif[CYW43_ITF_STA]);
#endif
    cyw43_arch_deinit();
}
