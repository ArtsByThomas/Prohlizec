#include "../browser.h"
#include "../DOM/DOM.h"
#include "../css/css.h"
#include "../utils/string/str_utils.h"
#include "../utils/syscall/syscalls.h"
#include "../js/headers/js_runtime.h"
#include "/navigation.h"
#include <stdint.h>




void resolve_relative_url(const char* href, char* out, int out_size) {
    if (str_starts_with(href, "http://") || str_starts_with(href, "https://")) {
        str_copy(out, href, out_size);
        return;
    }

    char rel_path[256];
    if (href[0] == '/') {
        str_copy(rel_path, href, sizeof(rel_path));
    } else {
   
        int last_slash = 0;
        for (int i = 0; current_path[i]; i++) {
            if (current_path[i] == '/') last_slash = i;
        }
        int pos = 0;
        for (int i = 0; i <= last_slash && pos < (int)sizeof(rel_path) - 1; i++) rel_path[pos++] = current_path[i];
        for (int i = 0; href[i] && pos < (int)sizeof(rel_path) - 1; i++) rel_path[pos++] = href[i];
        rel_path[pos] = '\0';
    }

    // URL: schema://domain[:port]rel_path
    int pos = 0;
    const char* scheme = current_is_https ? "https://" : "http://";
    for (int i = 0; scheme[i] && pos < out_size - 1; i++) out[pos++] = scheme[i];
    for (int i = 0; current_domain[i] && pos < out_size - 1; i++) out[pos++] = current_domain[i];
    if (current_port != 0) {
        char port_buf[8]; int pi = 0; int p = current_port;
        char tmp[8]; int ti = 0;
        if (p == 0) tmp[ti++] = '0';
        while (p > 0) { tmp[ti++] = '0' + (p % 10); p /= 10; }
        port_buf[pi++] = ':';
        while (ti > 0) port_buf[pi++] = tmp[--ti];
        port_buf[pi] = '\0';
        for (int i = 0; port_buf[i] && pos < out_size - 1; i++) out[pos++] = port_buf[i];
    }
    for (int i = 0; rel_path[i] && pos < out_size - 1; i++) out[pos++] = rel_path[i];
    out[pos] = '\0';
}



int sys_https_get(uint32_t ip, const char *domain, uint16_t port, const char *path, char *buf, int max_len) {

    return syscall6(1011, (long)ip, (long)domain, (long)port, (long)path, (long)buf, (long)max_len);
}

// Plain HTTP (bez TLS) 
int sys_http_get(uint32_t ip, uint16_t port, const char *path, char *buf, int max_len) {
    return syscall5(1015, (long)ip, (long)port, (long)path, (long)buf, (long)max_len);
}


// --- POST varianty (formuláře) ---

// sys_http_post: HTTP POST s tělem requestu 
int sys_http_post(uint32_t ip, uint16_t port, const char *path,
                   const char *body, int body_len, char *buf, int max_len) {
    HttpPostArgs args;
    args.body_len = body_len;
    args.resp_buf = buf;
    args.resp_max = max_len;
    return syscall5(1016, (long)ip, (long)port, (long)path, (long)body, (long)&args);
}

// sys_https_post: HTTPS POST s tělem requestu
int sys_https_post(uint32_t ip, const char *domain, uint16_t port, const char *path,
                    const char *body, int body_len, char *buf, int max_len) {
    HttpsPostArgs args;
    args.body = body;
    args.body_len = body_len;
    args.resp_buf = buf;
    args.resp_max = max_len;
    return syscall5(1017, (long)ip, (long)domain, (long)port, (long)path, (long)&args);
}


void sys_cookie_get(const char *domain, int is_https, char *out, int out_size) {
    syscall5(1018, (long)domain, (long)is_https, (long)out, (long)out_size, 0);
}

// sys_cookie_set: document.cookie zápis. 'cookie_str' je syrový JS string
// jako "name=value; Secure" - parsování (a ignorování HttpOnly atributu
// při JS zápisu) se děje kernel-side v js_cookie_set_for_domain.
void sys_cookie_set(const char *domain, const char *cookie_str) {
    syscall5(1019, (long)domain, (long)cookie_str, 0, 0, 0);
}


// Přidá URL na konec historie 
void nav_history_push(const char* url) {
    if (nav_history_pos >= 0 && str_eq(nav_history[nav_history_pos], url)) return;
    nav_history_pos++;
    if (nav_history_pos >= MAX_HISTORY) {
        // historie plná - posuneme celý zásobník o 1 (zahodíme nejstarší)
        for (int i = 1; i < MAX_HISTORY; i++) {
            for (int j = 0; j < 128; j++) nav_history[i-1][j] = nav_history[i][j];
        }
        nav_history_pos = MAX_HISTORY - 1;
    }
    str_copy(nav_history[nav_history_pos], url, sizeof(nav_history[nav_history_pos]));
    nav_history_count = nav_history_pos + 1; // vše "za" aktuální pozicí je teď zahozené (forward historie)
}


void navigate_to(const char* url);

void nav_go_back(void) {
    if (nav_history_pos <= 0) return; 
    nav_history_pos--;
    nav_history_navigating = 1;
    navigate_to(nav_history[nav_history_pos]);
    nav_history_navigating = 0;
}
void nav_go_forward(void) {
    if (nav_history_pos < 0 || nav_history_pos + 1 >= nav_history_count) return; 
    nav_history_pos++;
    nav_history_navigating = 1;
    navigate_to(nav_history[nav_history_pos]);
    nav_history_navigating = 0;
}

void nav_reload(void) {
    if (address_bar_len <= 0) return;
    navigate_to(address_bar_text);
}
// Přejde na domovskou stránku - normální navigace (přidá se do historie).
void nav_go_home(void) {
    navigate_to(HOME_URL);
}
void append_num(int num, char** buf) {
    char temp[4];
    int i = 0;
    if (num == 0) { temp[i++] = '0'; }
    while (num > 0) {
        temp[i++] = (num % 10) + '0';
        num /= 10;
    }
    // Obrácení a zápis do bufferu
    while (i > 0) {
        *(*buf)++ = temp[--i];
    }
}

// Funkce na formátování IP
void ip_to_string(uint32_t ip, char* buffer) {
    uint8_t* bytes = (uint8_t*)&ip;
    char* ptr = buffer;
    
    for (int i = 0; i < 4; i++) {
        append_num(bytes[i], &ptr);
        if (i < 3) *ptr++ = '.'; // Přidat tečku mezi oktety
    }
    *ptr = '\0'; // Ukončení stringu
}
void debug_print_int(int num) {
    char buf[16];
    char temp[16];
    char* ptr = buf;
    int i = 0;
    
    if (num == 0) {
        temp[i++] = '0';
    } else {
        if (num < 0) { 
            *ptr++ = '-'; 
            num = -num; 
        }
        while (num > 0) {
            temp[i++] = (num % 10) + '0';
            num /= 10;
        }
    }
    
    while (i > 0) { *ptr++ = temp[--i]; }
    *ptr = '\0';
    
    debug_print(buf);
}

// Sdílená "po stažení" logika pro navigate_to (GET) i navigate_to_post

void process_navigation_response(const char* full_url, const char* path, int bytes) {
    bytes_global = bytes;
    debug_print("Prijato bytu: ");
    debug_print_int(bytes);
    debug_print("\n");
    debug_print(html_buffer);
    if (bytes > 0) {
        str_copy(current_path, path, sizeof(current_path)); // pro resolve_relative_url dalších odkazů NA TÉTO stránce
        debug_print("Spoustim HTML/CSS parser (build_dom_tree)...\n");
        root_node = build_dom_tree(html_buffer);
        layout_dirty = 1; // nová stránka = nový DOM strom, layout musí proběhnout od nuly
        debug_print("DOM strom uspesne sestaven.\n");
        debug_print("[DIAG] nodes_allocated po parsovani: ");
        debug_print_int(nodes_allocated);
        debug_print("\n[DIAG] css_rule_count po parsovani: ");
        debug_print_int(css_rule_count);
        debug_print("\n[DIAG] html_buffer skutecna delka (bytes prijato): ");
        debug_print_int(bytes);
        debug_print("\n");

        debug_print("Spoustim JS engine (js_run_page_scripts)...\n");
        js_run_page_scripts(root_node);
        debug_print("JS skripty dokoncily beh.\n");

        scroll_y = 0;

        str_copy(address_bar_text, full_url, sizeof(address_bar_text));
        address_bar_len = str_len(address_bar_text);

      
        if (!nav_history_navigating) nav_history_push(address_bar_text);

        debug_print("Adresni radek aktualizovan. Stranka nactena.\n");
    } else {
        debug_print("CHYBA: HTTP(S) pozadavek selhal nebo vratil 0 bytu!\n");
    }
}

// Metoda pro vyhledávání url (IP i doména) 
void navigate_to(const char* url) {
    js_timers_reset();
    char domain[128];
    char path[128];
    int is_https = 1;  
    int explicit_port = 0;
    int is_ip = 0;

    int is_absolute = parse_url(url, domain, path, &is_https, &explicit_port, &is_ip);

    uint32_t new_ip = current_server_ip;

    if (is_absolute) {
        
        str_copy(current_domain, domain, sizeof(current_domain));
        current_is_https = is_https;
        current_port = explicit_port;
    }

    if (is_absolute) {
        debug_print(is_ip ? "Cilova IP adresa: " : "DNS dotaz na: ");
        debug_print(domain);
        debug_print("\n");
        draw_text_user(50, 30, domain, str_len(domain), 0xFF000000);

        if (is_ip) {
         
            new_ip = parse_ipv4(domain);
            debug_print("IP pouzita primo (bez DNS).\n");
        } else {
            new_ip = sys_dns_resolve(domain);
            if (new_ip != 0) {
                char ip_buffer[16];
                ip_to_string(new_ip, ip_buffer);
                debug_print("DNS uspesne: ");
                debug_print(ip_buffer);
                debug_print("\n");
                draw_text_user(50, 45, ip_buffer, str_len(ip_buffer), 0xFF0000FF);
            } else {
                debug_print("DNS FAIL!\n");
            }
        }

        if (new_ip != 0) current_server_ip = new_ip;
    }

    int port = explicit_port ? explicit_port : (is_https ? 443 : 80);

    debug_print(is_https ? "--- Zahajuji HTTPS GET ---\n" : "--- Zahajuji HTTP GET ---\n");
    debug_print("Cesta (path): ");
    debug_print(path);
    debug_print("\n");

    for(int i=0; i<HTML_BUF_SIZE; i++) html_buffer[i] = 0;

    debug_print("Cekam na odpoved od serveru...\n");
    int bytes = is_https
        ? sys_https_get(new_ip, domain, (uint16_t)port, path, html_buffer, HTML_BUF_SIZE)
        : sys_http_get(new_ip, (uint16_t)port, path, html_buffer, HTML_BUF_SIZE);

    process_navigation_response(url, path, bytes);
}