
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/ip4_addr.h"
#include "lwip/apps/mdns.h"
#include "lwip/init.h"
#include "lwip/apps/httpd.h"

#include "webapp.h"
#include "trs80.h"

/*

    To do:

    Clean up this file.
    What could this do:
        Show the screen.
        Select game to play.
        Control the game, or at least type stuff in the menus.
        Set the screen color.
            Show HTML with choices, use SSI to set current choice.
            POST to change color and return same HTML.
        Mute audio.

 */

#define COLOR_WHITE 0
#define COLOR_GREEN 1
#define COLOR_AMBER 2
#define SOUND_OFF 0
#define SOUND_ON 1

static absolute_time_t gWifiConnectedTime;
static bool led_on = false;
static int gColor = COLOR_WHITE;
static int gSound = SOUND_OFF;

/**
 * Convert a TRS-80 graphics character between 128 and 191 inclusive to an
 * equivalent Unicode braille character.
 */
static uint16_t trs80ToBraille(uint8_t ch) {
    // Remap to 0-63.
    ch -= 128;

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

static const char *cgi_handler_test(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    if (iNumParams > 0) {
        if (strcmp(pcParam[0], "test") == 0) {
            return "/test.shtml";
        }
    }
    return "/index.shtml";
    // return "/screen.shtml";
}

static tCGI cgi_handlers[] = {
    { "/", cgi_handler_test },
};

// Note that the buffer size is limited by LWIP_HTTPD_MAX_TAG_INSERT_LEN, so use LWIP_HTTPD_SSI_MULTIPART to return larger amounts of data
u16_t ssi_example_ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
    , uint16_t current_tag_part, uint16_t *next_tag_part
#endif
) {
    size_t printed;
    switch (iIndex) {
        case 0: { // "status"
            printed = snprintf(pcInsert, iInsertLen, "Pass");
            break;
        }
        case 1: { // "welcome"
            printed = snprintf(pcInsert, iInsertLen, "Hello from Pico");
            break;
        }
        case 2: { // "uptime"
            uint64_t uptime_s = absolute_time_diff_us(gWifiConnectedTime, get_absolute_time()) / 1e6;
            printed = snprintf(pcInsert, iInsertLen, "%" PRIu64, uptime_s);
            break;
        }
        case 3: { // "ledstate"
            printed = snprintf(pcInsert, iInsertLen, "%s", led_on ? "ON" : "OFF");
            break;
        }
        case 4: { // "ledinv"
            printed = snprintf(pcInsert, iInsertLen, "%s", led_on ? "OFF" : "ON");
            break;
        }
        case 5: { // "screen"
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
        case 6: { // "clrw"
            printed = snprintf(pcInsert, iInsertLen,
                    gColor == COLOR_WHITE ? "checked" : "");
            break;
        }
        case 7: { // "clrg"
            printed = snprintf(pcInsert, iInsertLen,
                    gColor == COLOR_GREEN ? "checked" : "");
            break;
        }
        case 8: { // "clra"
            printed = snprintf(pcInsert, iInsertLen,
                    gColor == COLOR_AMBER ? "checked" : "");
            break;
        }
        case 9: { // "sndoff"
            printed = snprintf(pcInsert, iInsertLen,
                    gSound == SOUND_OFF ? "checked" : "");
            break;
        }
        case 10: { // "sndon"
            printed = snprintf(pcInsert, iInsertLen,
                    gSound == SOUND_ON ? "checked" : "");
            break;
        }
#if LWIP_HTTPD_SSI_MULTIPART
        case 11: { /* "table" */
            printed = snprintf(pcInsert, iInsertLen, "<tr><td>This is table row number %d</td></tr>", current_tag_part + 1);
            // Leave "next_tag_part" unchanged to indicate that all data has been returned for this tag
            if (current_tag_part < 9) {
                *next_tag_part = current_tag_part + 1;
            }
            break;
        }
#endif
        default: { // unknown tag
            printed = 0;
            break;
        }
    }
  return (u16_t)printed;
}

// Be aware of LWIP_HTTPD_MAX_TAG_NAME_LEN
static const char *ssi_tags[] = {
    "status",
    "welcome",
    "uptime",
    "ledstate",
    "ledinv",
    "screen",
    "clrw",
    "clrg",
    "clra",
    "sndoff",
    "sndon",
    "table",
};

#if LWIP_HTTPD_SUPPORT_POST
#define LED_STATE_BUFSIZE 10
static void *current_connection;

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
        u16_t http_request_len, int content_len, char *response_uri,
        u16_t response_uri_len, u8_t *post_auto_wnd) {
    if (memcmp(uri, "/led.cgi", 8) == 0 && current_connection != connection) {
        current_connection = connection;
        snprintf(response_uri, response_uri_len, "/ledfail.shtml");
        *post_auto_wnd = 1;
        return ERR_OK;
    }
    if (memcmp(uri, "/", 1) == 0 && current_connection != connection) {
        current_connection = connection;
        snprintf(response_uri, response_uri_len, "/ledfail.shtml");
        *post_auto_wnd = 1;
        return ERR_OK;
    }
    return ERR_VAL;
}

// Return a value for a parameter
char *httpd_param_value(struct pbuf *p, const char *param_name, char *value_buf, size_t value_buf_len) {
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
        char buf[LED_STATE_BUFSIZE];
        char *val = httpd_param_value(p, "led_state=", buf, sizeof(buf));
        if (val) {
            led_on = (strcmp(val, "ON") == 0) ? true : false;
            cyw43_gpio_set(&cyw43_state, 0, led_on);
            ret = ERR_OK;
        }
        val = httpd_param_value(p, "color=", buf, sizeof(buf));
        if (val) {
            printf("color=%s\n", val);
            if (strcmp(val, "white") == 0) {
                gColor = COLOR_WHITE;
            } else if (strcmp(val, "green") == 0) {
                gColor = COLOR_GREEN;
            } else if (strcmp(val, "amber") == 0) {
                gColor = COLOR_AMBER;
            }
            ret = ERR_OK;
        }
        val = httpd_param_value(p, "sound=", buf, sizeof(buf));
        if (val) {
            printf("sound=%s\n", val);
            if (strcmp(val, "off") == 0) {
                gSound = SOUND_OFF;
            } else if (strcmp(val, "on") == 0) {
                gSound = SOUND_ON;
            }
            ret = ERR_OK;
        }
    }
    pbuf_free(p);
    return ret;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    snprintf(response_uri, response_uri_len, "/ledfail.shtml");
    if (current_connection == connection) {
        snprintf(response_uri, response_uri_len, "/");
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
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                CYW43_AUTH_WPA2_AES_PSK, 30000)) {

        printf("Failed to connect.\n");
        exit(1);
    } else {
        printf("Connected.\n");
    }
    printf("Ready, running httpd at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    // start http server
    gWifiConnectedTime = get_absolute_time();

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
