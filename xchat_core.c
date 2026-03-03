#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef APP_ROLE_SERVER
#define APP_ROLE_SERVER 0
#endif

#ifndef APP_TITLE
#define APP_TITLE "XChat"
#endif

#ifndef APP_ID
#define APP_ID "com.example.xchat"
#endif

#ifndef APP_DEFAULT_PORT
#define APP_DEFAULT_PORT 8080
#endif

#define MAX_NICK 64
#define MAX_NAME 64
#define MAX_USERNAME 64
#define MAX_BIO 256
#define MAX_ALIAS 64
#define MAX_PREVIEW 128
#define MAX_MSG 1024

typedef struct {
    char first_name[MAX_NAME];
    char last_name[MAX_NAME];
    char username[MAX_USERNAME];
    char nickname[MAX_NICK];
    int age;
    char bio[MAX_BIO];
} Profile;

typedef struct {
    gboolean outgoing;
    gboolean system;
    char text[MAX_MSG];
    char time_hhmm[16];
} ChatMessage;

typedef struct Peer {
    int id;
    int fd;
    gboolean connected;
    gboolean inbound;
    gboolean hidden;

    char ip[INET_ADDRSTRLEN];
    int port;
    char alias[MAX_ALIAS];

    Profile profile;
    GPtrArray *messages; // ChatMessage*

    GtkWidget *row;
    GtkWidget *name_label;
    GtkWidget *preview_label;
    GtkWidget *meta_label;

    pthread_t thread;
    pthread_mutex_t io_lock;
} Peer;

typedef struct {
    GtkWidget *window;

    GtkWidget *status_label;

    GtkWidget *contacts_list;
    GtkWidget *search_entry;
    GtkWidget *new_chat_btn;
    GtkWidget *profile_btn;

    GtkWidget *right_stack;
    GtkWidget *empty_page;
    GtkWidget *chat_page;

    GtkWidget *chat_title;
    GtkWidget *chat_subtitle;

    GtkWidget *messages_scrolled;
    GtkWidget *messages_list;

    GtkWidget *message_entry;
    GtkWidget *send_button;
    GtkWidget *hide_button;
    GtkWidget *remote_profile_button;

    GtkWidget *me_name;
    GtkWidget *me_subtitle;

    GPtrArray *peers; // Peer*
    GMutex peers_mutex;

    Peer *active_peer;
    int next_peer_id;

    Profile me;
    gboolean running;

    int listen_fd;
    pthread_t accept_thread;
} AppState;

static AppState app;

static const char *XCHAT_CSS =
"window {"
"  background: #000000;"
"  color: #e7e9ea;"
"  font-family: 'SF Pro Text', 'Helvetica Neue', sans-serif;"
"}"
".root {"
"  background: #000000;"
"}"
".left-panel {"
"  background: #000000;"
"  border-right: 1px solid #2f3336;"
"}"
".left-header {"
"  padding: 14px 14px 8px 14px;"
"  background: #000000;"
"}"
"label.left-title {"
"  color: #f7f9f9;"
"  font-size: 34px;"
"  font-weight: 800;"
"}"
"button.icon-btn {"
"  background: transparent;"
"  color: #e7e9ea;"
"  border: 1px solid transparent;"
"  border-radius: 15px;"
"  padding: 5px;"
"  min-width: 28px;"
"  min-height: 28px;"
"}"
"button.icon-btn:hover {"
"  background: #16181c;"
"}"
".search-wrap {"
"  padding: 0 14px 10px 14px;"
"  background: #000000;"
"}"
"entry.search-entry {"
"  background: #16181c;"
"  color: #e7e9ea;"
"  border: 1px solid #202327;"
"  border-radius: 16px;"
"  padding: 10px 13px;"
"}"
"entry.search-entry:focus {"
"  border-color: #1d9bf0;"
"}"
".filters-row {"
"  padding: 0 14px 10px 14px;"
"  background: #000000;"
"}"
"button.filter-pill {"
"  background: #16181c;"
"  color: #8b98a5;"
"  border: 1px solid #2f3336;"
"  border-radius: 13px;"
"  padding: 6px 11px;"
"}"
"button.filter-pill.active {"
"  background: #eff3f4;"
"  color: #0f1419;"
"  border-color: #eff3f4;"
"  font-weight: 700;"
"}"
".contacts-list {"
"  background: #000000;"
"  border: none;"
"}"
".contacts-list row {"
"  background: #000000;"
"  border-bottom: 1px solid #1d1f23;"
"  padding: 0;"
"}"
".contacts-list row:selected {"
"  background: #16181c;"
"}"
".contact-row {"
"  padding: 13px 14px;"
"}"
"label.contact-name {"
"  color: #e7e9ea;"
"  font-size: 16px;"
"  font-weight: 700;"
"}"
"label.contact-preview {"
"  color: #71767b;"
"  font-size: 14px;"
"}"
"label.contact-meta {"
"  color: #71767b;"
"  font-size: 12px;"
"}"
".left-footer {"
"  padding: 11px 14px;"
"  border-top: 1px solid #2f3336;"
"}"
"button.new-chat-btn {"
"  background: #eff3f4;"
"  color: #0f1419;"
"  border: none;"
"  border-radius: 14px;"
"  font-weight: 800;"
"  padding: 8px 14px;"
"}"
"button.new-chat-btn:hover {"
"  background: #dfe5e7;"
"}"
"button.soft-btn {"
"  background: transparent;"
"  color: #e7e9ea;"
"  border: 1px solid #2f3336;"
"  border-radius: 13px;"
"  padding: 7px 12px;"
"  font-weight: 600;"
"}"
"button.soft-btn:hover {"
"  background: #16181c;"
"}"
".right-panel {"
"  background: #000000;"
"}"
".empty-wrap {"
"  background: #000000;"
"}"
".empty-icon-wrap {"
"  background: #16181c;"
"  border-radius: 36px;"
"  min-width: 72px;"
"  min-height: 72px;"
"}"
"label.empty-icon {"
"  color: #e7e9ea;"
"  font-size: 36px;"
"}"
"label.empty-title {"
"  color: #e7e9ea;"
"  font-size: 30px;"
"  font-weight: 800;"
"}"
"label.empty-subtitle {"
"  color: #71767b;"
"  font-size: 18px;"
"}"
".chat-topbar {"
"  background: #000000;"
"  border-bottom: 1px solid #2f3336;"
"  padding: 10px 14px;"
"}"
"label.chat-title {"
"  color: #e7e9ea;"
"  font-size: 20px;"
"  font-weight: 800;"
"}"
"label.chat-subtitle {"
"  color: #71767b;"
"  font-size: 13px;"
"}"
".chat-list {"
"  background: #000000;"
"  border: none;"
"}"
".chat-list row {"
"  background: transparent;"
"  border: none;"
"}"
".bubble-in {"
"  background: #16181c;"
"  border: 1px solid #2f3336;"
"  border-radius: 16px;"
"  padding: 8px 12px;"
"}"
".bubble-out {"
"  background: #1d9bf0;"
"  border: 1px solid #1d9bf0;"
"  border-radius: 16px;"
"  padding: 8px 12px;"
"}"
".bubble-system {"
"  background: #111214;"
"  border: 1px solid #2f3336;"
"  border-radius: 12px;"
"  padding: 6px 10px;"
"}"
"label.msg-in {"
"  color: #e7e9ea;"
"  font-size: 15px;"
"}"
"label.msg-out {"
"  color: #ffffff;"
"  font-size: 15px;"
"}"
"label.msg-system {"
"  color: #8b98a5;"
"  font-size: 13px;"
"}"
"label.msg-time {"
"  color: #8b98a5;"
"  font-size: 11px;"
"}"
".composer {"
"  border-top: 1px solid #2f3336;"
"  padding: 10px 14px 12px 14px;"
"}"
"entry.compose-entry {"
"  background: #16181c;"
"  color: #e7e9ea;"
"  border: 1px solid #202327;"
"  border-radius: 16px;"
"  padding: 10px 14px;"
"}"
"entry.compose-entry:focus {"
"  border-color: #1d9bf0;"
"}"
"button.send-btn {"
"  background: #eff3f4;"
"  color: #0f1419;"
"  border: none;"
"  border-radius: 14px;"
"  font-weight: 800;"
"  padding: 8px 12px;"
"}"
"button.send-btn:hover {"
"  background: #dfe5e7;"
"}"
".status-strip {"
"  border-top: 1px solid #2f3336;"
"  padding: 8px 14px;"
"}"
"label.status-label {"
"  color: #71767b;"
"  font-size: 12px;"
"}"
"menu, menuitem, menuitem label {"
"  background: #111214;"
"  color: #e7e9ea;"
"}"
"menu {"
"  border: 1px solid #2f3336;"
"  padding: 4px;"
"}"
"menuitem {"
"  padding: 7px 12px;"
"  border-radius: 8px;"
"}"
"menuitem * {"
"  color: #e7e9ea;"
"}"
"menuitem:hover {"
"  background: #1d1f23;"
"}"
"popover, popover.background {"
"  background: #111214;"
"  color: #e7e9ea;"
"  border: 1px solid #2f3336;"
"}"
"modelbutton {"
"  color: #e7e9ea;"
"  padding: 6px 10px;"
"}"
"modelbutton * {"
"  color: #e7e9ea;"
"}"
"modelbutton:hover {"
"  background: #1d1f23;"
"}"
"button.dialog-primary,"
"button.dialog-secondary {"
" background: #ffffff;"
" color: #000000;"
" border: 2px solid #000000;"
" border-radius: 14px;"
" font-weight: 600;"
" padding: 10px 16px;"
"}"
"button.dialog-primary:hover,"
"button.dialog-secondary:hover {"
" background: #f3f3f3;"
"}"
"button.dialog-primary:active,"
"button.dialog-secondary:active {"
" background: #e6e6e6;"
"}"
".dialog-card {"
"  background: #0b0c0e;"
"  border: 1px solid #2f3336;"
"  border-radius: 14px;"
"  padding: 16px;"
"}"
"label.dialog-title {"
"  color: #e7e9ea;"
"  font-size: 22px;"
"  font-weight: 800;"
"}"
"label.dialog-hint {"
"  color: #8b98a5;"
"  font-size: 13px;"
"}"
"label.dialog-field {"
"  color: #e7e9ea;"
"  font-size: 13px;"
"  font-weight: 700;"
"}"
"entry.dialog-entry, spinbutton.dialog-entry {"
"  background: #16181c;"
"  color: #e7e9ea;"
"  border: 1px solid #2f3336;"
"  border-radius: 12px;"
"  padding: 11px 12px;"
"}"
"entry.dialog-entry:focus, spinbutton.dialog-entry:focus {"
"  border-color: #1d9bf0;"
"}"
"scrollbar slider {"
"  background: #2f3336;"
"  border-radius: 4px;"
"}";

static void set_status(const char *text) {
    gtk_label_set_text(GTK_LABEL(app.status_label), text);
}

static void sanitize_field(const char *src, char *dst, size_t dst_size) {
    size_t i;
    if (dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++) {
        char c = src[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            dst[i] = ' ';
        } else {
            dst[i] = c;
        }
    }
    dst[i] = '\0';
}

static const char *profile_file_path(void) {
    static gchar *cached = NULL;
    if (cached == NULL) {
        gchar *cwd = g_get_current_dir();
        cached = g_build_filename(cwd, "xchat_profile.ini", NULL);
        g_free(cwd);
    }
    return cached;
}

static void ensure_username_format(char *username, size_t size) {
    if (username[0] == '\0') {
        g_strlcpy(username, "@new_user", size);
        return;
    }
    if (username[0] != '@') {
        char temp[MAX_USERNAME];
        g_strlcpy(temp, username, sizeof(temp));
        g_snprintf(username, size, "@%s", temp);
    }

    for (size_t i = 0; username[i] != '\0'; i++) {
        if (username[i] == ' ') {
            username[i] = '_';
        }
    }
}

static void profile_refresh_public_fields(Profile *profile) {
    char full_name[MAX_NICK] = {0};

    ensure_username_format(profile->username, sizeof(profile->username));

    if (profile->first_name[0] != '\0' && profile->last_name[0] != '\0') {
        g_snprintf(full_name, sizeof(full_name), "%s %s", profile->first_name, profile->last_name);
    } else if (profile->first_name[0] != '\0') {
        g_strlcpy(full_name, profile->first_name, sizeof(full_name));
    } else if (profile->last_name[0] != '\0') {
        g_strlcpy(full_name, profile->last_name, sizeof(full_name));
    } else if (profile->username[0] != '\0') {
        g_strlcpy(full_name, profile->username, sizeof(full_name));
    } else {
        g_strlcpy(full_name, "new_user", sizeof(full_name));
    }

    sanitize_field(full_name, profile->nickname, sizeof(profile->nickname));

    if (profile->bio[0] == '\0') {
        g_strlcpy(profile->bio, "без описания", sizeof(profile->bio));
    }
    if (profile->age <= 0) {
        profile->age = 20;
    }
}

static void save_profile_to_disk(void) {
    GKeyFile *kf = g_key_file_new();
    gchar *data;
    gsize len = 0;
    GError *err = NULL;

    g_key_file_set_string(kf, "profile", "first_name", app.me.first_name);
    g_key_file_set_string(kf, "profile", "last_name", app.me.last_name);
    g_key_file_set_string(kf, "profile", "username", app.me.username);
    g_key_file_set_string(kf, "profile", "bio", app.me.bio);
    g_key_file_set_integer(kf, "profile", "age", app.me.age);

    data = g_key_file_to_data(kf, &len, NULL);
    if (!g_file_set_contents(profile_file_path(), data, (gssize)len, &err)) {
        if (err != NULL) {
            g_warning("Failed to save profile: %s", err->message);
            g_clear_error(&err);
        }
    }

    g_free(data);
    g_key_file_free(kf);
}

static void load_profile_from_disk(void) {
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (!g_key_file_load_from_file(kf, profile_file_path(), G_KEY_FILE_NONE, &err)) {
        if (err != NULL) {
            g_clear_error(&err);
        }
        g_key_file_free(kf);
        return;
    }

    gchar *value = g_key_file_get_string(kf, "profile", "first_name", NULL);
    if (value != NULL) {
        sanitize_field(value, app.me.first_name, sizeof(app.me.first_name));
        g_free(value);
    }

    value = g_key_file_get_string(kf, "profile", "last_name", NULL);
    if (value != NULL) {
        sanitize_field(value, app.me.last_name, sizeof(app.me.last_name));
        g_free(value);
    }

    value = g_key_file_get_string(kf, "profile", "username", NULL);
    if (value != NULL) {
        sanitize_field(value, app.me.username, sizeof(app.me.username));
        g_free(value);
    }

    value = g_key_file_get_string(kf, "profile", "bio", NULL);
    if (value != NULL) {
        sanitize_field(value, app.me.bio, sizeof(app.me.bio));
        g_free(value);
    }

    if (g_key_file_has_key(kf, "profile", "age", NULL)) {
        app.me.age = g_key_file_get_integer(kf, "profile", "age", NULL);
    }

    profile_refresh_public_fields(&app.me);
    g_key_file_free(kf);
}

static void now_hhmm(char *out, size_t out_size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    if (tm_info == NULL) {
        g_strlcpy(out, "--:--", out_size);
        return;
    }
    strftime(out, out_size, "%H:%M", tm_info);
}

static void now_day_month(char *out, size_t out_size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    if (tm_info == NULL) {
        g_strlcpy(out, "", out_size);
        return;
    }
    strftime(out, out_size, "%d.%m.%Y", tm_info);
}

static const char *peer_display_name(const Peer *peer) {
    if (peer->alias[0] != '\0') {
        return peer->alias;
    }
    if (peer->profile.nickname[0] != '\0') {
        return peer->profile.nickname;
    }
    return peer->ip;
}

static void update_me_panel(void) {
    char title[140];
    char subtitle[300];

    profile_refresh_public_fields(&app.me);

    if (app.me.first_name[0] != '\0' && app.me.last_name[0] != '\0') {
        g_snprintf(title, sizeof(title), "%s %s", app.me.first_name, app.me.last_name);
    } else if (app.me.first_name[0] != '\0') {
        g_strlcpy(title, app.me.first_name, sizeof(title));
    } else if (app.me.last_name[0] != '\0') {
        g_strlcpy(title, app.me.last_name, sizeof(title));
    } else {
        g_strlcpy(title, app.me.nickname, sizeof(title));
    }

    snprintf(subtitle, sizeof(subtitle), "%s • %d лет", app.me.username, app.me.age);

    gtk_label_set_text(GTK_LABEL(app.me_name), title);
    gtk_label_set_text(GTK_LABEL(app.me_subtitle), subtitle);
}

static void apply_my_profile_fields(const char *first_name,
                                    const char *last_name,
                                    const char *username,
                                    const char *bio,
                                    int age) {
    sanitize_field(first_name, app.me.first_name, sizeof(app.me.first_name));
    sanitize_field(last_name, app.me.last_name, sizeof(app.me.last_name));
    sanitize_field(username, app.me.username, sizeof(app.me.username));
    sanitize_field(bio, app.me.bio, sizeof(app.me.bio));
    app.me.age = age;
    profile_refresh_public_fields(&app.me);
    update_me_panel();
}

static gboolean send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return FALSE;
        }
        off += (size_t)n;
    }
    return TRUE;
}

static gboolean peer_send_line(Peer *peer, const char *line) {
    gboolean ok = FALSE;
    size_t len;

    pthread_mutex_lock(&peer->io_lock);
    if (!peer->connected || peer->fd < 0) {
        pthread_mutex_unlock(&peer->io_lock);
        return FALSE;
    }

    len = strlen(line);
    ok = send_all(peer->fd, line, len) && send_all(peer->fd, "\n", 1);

    if (!ok) {
        peer->connected = FALSE;
        if (peer->fd >= 0) {
            shutdown(peer->fd, SHUT_RDWR);
            close(peer->fd);
            peer->fd = -1;
        }
    }

    pthread_mutex_unlock(&peer->io_lock);
    return ok;
}

static void peer_disconnect_socket(Peer *peer) {
    pthread_mutex_lock(&peer->io_lock);
    if (peer->fd >= 0) {
        shutdown(peer->fd, SHUT_RDWR);
        close(peer->fd);
        peer->fd = -1;
    }
    peer->connected = FALSE;
    pthread_mutex_unlock(&peer->io_lock);
}

static void send_hello(Peer *peer) {
    char nick[MAX_NICK];
    char bio[MAX_BIO];
    char line[1200];

    sanitize_field(app.me.nickname, nick, sizeof(nick));
    sanitize_field(app.me.bio, bio, sizeof(bio));

    snprintf(line, sizeof(line), "HELLO\t%s\t%d\t%s", nick, app.me.age, bio);
    peer_send_line(peer, line);
}

static void broadcast_hello(void) {
    g_mutex_lock(&app.peers_mutex);
    for (guint i = 0; i < app.peers->len; i++) {
        Peer *peer = g_ptr_array_index(app.peers, i);
        if (peer->connected && !peer->hidden) {
            send_hello(peer);
        }
    }
    g_mutex_unlock(&app.peers_mutex);
}

static void set_peer_preview(Peer *peer, const char *text) {
    if (peer->preview_label == NULL || peer->meta_label == NULL) {
        return;
    }

    char preview[MAX_PREVIEW];
    sanitize_field(text, preview, sizeof(preview));
    gtk_label_set_text(GTK_LABEL(peer->preview_label), preview);

    char hhmm[16];
    now_hhmm(hhmm, sizeof(hhmm));
    gtk_label_set_text(GTK_LABEL(peer->meta_label), hhmm);
}

static void rebuild_chat_view(void);

static void set_active_peer(Peer *peer) {
    app.active_peer = peer;

    if (peer == NULL) {
        gtk_stack_set_visible_child(GTK_STACK(app.right_stack), app.empty_page);
        return;
    }

    gtk_label_set_text(GTK_LABEL(app.chat_title), peer_display_name(peer));
    if (peer->profile.nickname[0] != '\0') {
        char subtitle[350];
        snprintf(subtitle, sizeof(subtitle), "@%s • %d лет", peer->profile.nickname, peer->profile.age);
        gtk_label_set_text(GTK_LABEL(app.chat_subtitle), subtitle);
    } else {
        char subtitle[128];
        snprintf(subtitle, sizeof(subtitle), "%s:%d", peer->ip, peer->port);
        gtk_label_set_text(GTK_LABEL(app.chat_subtitle), subtitle);
    }

    rebuild_chat_view();
    gtk_stack_set_visible_child(GTK_STACK(app.right_stack), app.chat_page);
}

static void peer_add_message(Peer *peer, gboolean outgoing, gboolean system, const char *text) {
    ChatMessage *msg = g_new0(ChatMessage, 1);
    sanitize_field(text, msg->text, sizeof(msg->text));
    msg->outgoing = outgoing;
    msg->system = system;
    now_hhmm(msg->time_hhmm, sizeof(msg->time_hhmm));
    g_ptr_array_add(peer->messages, msg);

    if (!system) {
        set_peer_preview(peer, text);
    }

    if (app.active_peer == peer) {
        rebuild_chat_view();
    }
}

static void free_chat_message(gpointer data) {
    g_free(data);
}

static void peer_free(gpointer data) {
    Peer *peer = data;
    if (peer == NULL) {
        return;
    }

    peer_disconnect_socket(peer);
    if (peer->messages) {
        g_ptr_array_free(peer->messages, TRUE);
    }
    pthread_mutex_destroy(&peer->io_lock);
    g_free(peer);
}

static gboolean scroll_messages_to_bottom_idle(gpointer data) {
    GtkWidget *scrolled = GTK_WIDGET(data);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    if (adj != NULL) {
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    }
    return G_SOURCE_REMOVE;
}

static GtkWidget *build_bubble_label(const char *text, const char *css_class, gboolean wrap) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), wrap);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);
    return label;
}

static void append_message_row(ChatMessage *msg) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *bubble = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *time_label = build_bubble_label(msg->time_hhmm, "msg-time", FALSE);

    GtkWidget *left_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *right_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(left_spacer, TRUE);
    gtk_widget_set_hexpand(right_spacer, TRUE);

    if (msg->system) {
        GtkWidget *sys_text = build_bubble_label(msg->text, "msg-system", TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(bubble), "bubble-system");
        gtk_box_pack_start(GTK_BOX(bubble), sys_text, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bubble), time_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), left_spacer, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), bubble, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), right_spacer, TRUE, TRUE, 0);
    } else if (msg->outgoing) {
        GtkWidget *text = build_bubble_label(msg->text, "msg-out", TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(bubble), "bubble-out");
        gtk_box_pack_start(GTK_BOX(bubble), text, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bubble), time_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), left_spacer, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), bubble, FALSE, FALSE, 0);
    } else {
        GtkWidget *text = build_bubble_label(msg->text, "msg-in", TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(bubble), "bubble-in");
        gtk_box_pack_start(GTK_BOX(bubble), text, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bubble), time_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), bubble, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(wrap), right_spacer, TRUE, TRUE, 0);
    }

    gtk_widget_set_margin_top(wrap, 8);
    gtk_widget_set_margin_bottom(wrap, 2);
    gtk_widget_set_margin_start(wrap, 14);
    gtk_widget_set_margin_end(wrap, 14);

    gtk_container_add(GTK_CONTAINER(row), wrap);
    gtk_container_add(GTK_CONTAINER(app.messages_list), row);
}

static void clear_listbox_children(GtkWidget *listbox) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
}

static void rebuild_chat_view(void) {
    clear_listbox_children(app.messages_list);

    if (app.active_peer == NULL) {
        return;
    }

    for (guint i = 0; i < app.active_peer->messages->len; i++) {
        ChatMessage *msg = g_ptr_array_index(app.active_peer->messages, i);
        append_message_row(msg);
    }

    gtk_widget_show_all(app.messages_list);
    g_idle_add(scroll_messages_to_bottom_idle, app.messages_scrolled);
}

static void refresh_peer_row(Peer *peer) {
    if (peer->name_label == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(peer->name_label), peer_display_name(peer));
}

static gboolean peer_in_search(Peer *peer, const char *needle_lower) {
    if (needle_lower == NULL || needle_lower[0] == '\0') {
        return TRUE;
    }

    char haystack[400];
    snprintf(haystack, sizeof(haystack), "%s %s %s", peer_display_name(peer), peer->ip, peer->profile.bio);
    gchar *h = g_utf8_strdown(haystack, -1);
    gboolean ok = strstr(h, needle_lower) != NULL;
    g_free(h);
    return ok;
}

static void apply_contacts_filter(void) {
    const char *query = gtk_entry_get_text(GTK_ENTRY(app.search_entry));
    gchar *q_lower = g_utf8_strdown(query, -1);

    g_mutex_lock(&app.peers_mutex);
    for (guint i = 0; i < app.peers->len; i++) {
        Peer *peer = g_ptr_array_index(app.peers, i);
        gboolean visible = (!peer->hidden) && peer_in_search(peer, q_lower);
        if (peer->row) {
            if (visible) {
                gtk_widget_show(peer->row);
            } else {
                gtk_widget_hide(peer->row);
            }
        }
    }
    g_mutex_unlock(&app.peers_mutex);

    g_free(q_lower);
}

static Peer *find_peer_by_id(int id) {
    Peer *res = NULL;

    g_mutex_lock(&app.peers_mutex);
    for (guint i = 0; i < app.peers->len; i++) {
        Peer *peer = g_ptr_array_index(app.peers, i);
        if (peer->id == id) {
            res = peer;
            break;
        }
    }
    g_mutex_unlock(&app.peers_mutex);

    return res;
}

static GtkWidget *build_contact_row(Peer *peer) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name = gtk_label_new(peer_display_name(peer));
    GtkWidget *preview = gtk_label_new("Новый чат");
    GtkWidget *meta = gtk_label_new(" ");

    gtk_style_context_add_class(gtk_widget_get_style_context(h), "contact-row");
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "contact-name");
    gtk_style_context_add_class(gtk_widget_get_style_context(preview), "contact-preview");
    gtk_style_context_add_class(gtk_widget_get_style_context(meta), "contact-meta");

    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_set_halign(preview, GTK_ALIGN_START);
    gtk_widget_set_halign(meta, GTK_ALIGN_END);
    gtk_widget_set_valign(meta, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(center), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), preview, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(h), center, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(h), meta, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(row), h);

    peer->row = row;
    peer->name_label = name;
    peer->preview_label = preview;
    peer->meta_label = meta;

    g_object_set_data(G_OBJECT(row), "peer-id", GINT_TO_POINTER(peer->id));
    return row;
}

typedef struct {
    Peer *peer;
    gboolean select_now;
} AddPeerRowEvent;

static gboolean ui_add_peer_row_idle(gpointer data) {
    AddPeerRowEvent *ev = data;
    Peer *peer = ev->peer;
    GtkWidget *row = build_contact_row(peer);

    gtk_container_add(GTK_CONTAINER(app.contacts_list), row);
    gtk_widget_show_all(row);
    apply_contacts_filter();

    if (ev->select_now || app.active_peer == NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(app.contacts_list), GTK_LIST_BOX_ROW(row));
        set_active_peer(peer);
    }

    g_free(ev);
    return G_SOURCE_REMOVE;
}

static gboolean ui_peer_refresh_idle(gpointer data) {
    Peer *peer = data;
    refresh_peer_row(peer);

    if (app.active_peer == peer) {
        set_active_peer(peer);
    }

    return G_SOURCE_REMOVE;
}

typedef struct {
    Peer *peer;
    gboolean outgoing;
    gboolean system;
    char text[MAX_MSG];
} AddMessageEvent;

static gboolean ui_add_message_idle(gpointer data) {
    AddMessageEvent *ev = data;
    peer_add_message(ev->peer, ev->outgoing, ev->system, ev->text);
    g_free(ev);
    return G_SOURCE_REMOVE;
}

static void queue_message(Peer *peer, gboolean outgoing, gboolean system, const char *text) {
    AddMessageEvent *ev = g_new0(AddMessageEvent, 1);
    ev->peer = peer;
    ev->outgoing = outgoing;
    ev->system = system;
    sanitize_field(text, ev->text, sizeof(ev->text));
    g_idle_add(ui_add_message_idle, ev);
}

static gboolean ui_peer_disconnected_idle(gpointer data) {
    Peer *peer = data;
    set_peer_preview(peer, "Отключен");
    queue_message(peer, FALSE, TRUE, "Соединение закрыто");

    if (app.active_peer == peer) {
        set_active_peer(peer);
    }

    return G_SOURCE_REMOVE;
}

static void parse_hello(Peer *peer, const char *line) {
    gchar **parts = g_strsplit(line, "\t", 4);
    if (parts[1] == NULL || parts[2] == NULL || parts[3] == NULL) {
        g_strfreev(parts);
        return;
    }

    sanitize_field(parts[1], peer->profile.nickname, sizeof(peer->profile.nickname));
    peer->profile.age = atoi(parts[2]);
    if (peer->profile.age <= 0) {
        peer->profile.age = 18;
    }
    sanitize_field(parts[3], peer->profile.bio, sizeof(peer->profile.bio));

    g_idle_add(ui_peer_refresh_idle, peer);

    g_strfreev(parts);
}

static void parse_msg(Peer *peer, const char *line) {
    if (g_str_has_prefix(line, "MSG\t")) {
        queue_message(peer, FALSE, FALSE, line + 4);
    } else {
        queue_message(peer, FALSE, FALSE, line);
    }
}

static void process_protocol_line(Peer *peer, const char *line) {
    if (g_str_has_prefix(line, "HELLO\t")) {
        parse_hello(peer, line);
    } else if (g_str_has_prefix(line, "MSG\t")) {
        parse_msg(peer, line);
    } else if (strcmp(line, "BYE") == 0) {
        peer_disconnect_socket(peer);
    } else {
        parse_msg(peer, line);
    }
}

static void *peer_reader_thread(void *arg) {
    Peer *peer = arg;
    char read_buf[2048];
    char line_buf[4096];
    size_t line_len = 0;

    while (peer->connected) {
        ssize_t n = recv(peer->fd, read_buf, sizeof(read_buf), 0);
        if (n <= 0) {
            break;
        }

        for (ssize_t i = 0; i < n; i++) {
            char c = read_buf[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0) {
                    process_protocol_line(peer, line_buf);
                }
                line_len = 0;
            } else if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = c;
            }
        }
    }

    peer_disconnect_socket(peer);
    g_idle_add(ui_peer_disconnected_idle, peer);
    return NULL;
}

static void start_peer_thread(Peer *peer) {
    pthread_create(&peer->thread, NULL, peer_reader_thread, peer);
    pthread_detach(peer->thread);
}

static Peer *create_peer(const char *alias, const char *ip, int port, int fd, gboolean inbound) {
    Peer *peer = g_new0(Peer, 1);

    peer->id = app.next_peer_id++;
    peer->fd = fd;
    peer->connected = TRUE;
    peer->inbound = inbound;
    peer->hidden = FALSE;

    sanitize_field(alias, peer->alias, sizeof(peer->alias));
    sanitize_field(ip, peer->ip, sizeof(peer->ip));
    peer->port = port;

    g_strlcpy(peer->profile.nickname, "user", sizeof(peer->profile.nickname));
    peer->profile.age = 18;
    g_strlcpy(peer->profile.bio, "новый контакт", sizeof(peer->profile.bio));

    peer->messages = g_ptr_array_new_with_free_func(free_chat_message);
    pthread_mutex_init(&peer->io_lock, NULL);

    return peer;
}

static void add_peer(Peer *peer, gboolean select_now) {
    g_mutex_lock(&app.peers_mutex);
    g_ptr_array_add(app.peers, peer);
    g_mutex_unlock(&app.peers_mutex);

    AddPeerRowEvent *ev = g_new0(AddPeerRowEvent, 1);
    ev->peer = peer;
    ev->select_now = select_now;
    g_idle_add(ui_add_peer_row_idle, ev);
}

static gboolean connect_outbound(const char *alias, const char *ip, int port, gboolean select_now) {
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        set_status("Ошибка: не удалось создать сокет");
        return FALSE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        set_status("Ошибка: неверный IP");
        close(fd);
        return FALSE;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        set_status("Ошибка: не удалось подключиться");
        close(fd);
        return FALSE;
    }

    Peer *peer = create_peer(alias, ip, port, fd, FALSE);
    add_peer(peer, select_now);

    queue_message(peer, FALSE, TRUE, "Чат создан");
    start_peer_thread(peer);
    send_hello(peer);

    char status[256];
    snprintf(status, sizeof(status), "Подключено к %s:%d", ip, port);
    set_status(status);

    return TRUE;
}

static gboolean accept_incoming_idle(gpointer data) {
    Peer *peer = data;
    add_peer(peer, FALSE);
    queue_message(peer, FALSE, TRUE, "Входящее подключение");
    send_hello(peer);
    start_peer_thread(peer);
    return G_SOURCE_REMOVE;
}

static void *accept_thread_fn(void *arg) {
    (void)arg;

    while (app.running) {
        struct sockaddr_in cliaddr;
        socklen_t clen = sizeof(cliaddr);
        int cfd = accept(app.listen_fd, (struct sockaddr *)&cliaddr, &clen);
        if (cfd < 0) {
            if (!app.running) {
                break;
            }
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &cliaddr.sin_addr, ip, sizeof(ip));
        int port = ntohs(cliaddr.sin_port);

        Peer *peer = create_peer("", ip, port, cfd, TRUE);
        g_idle_add(accept_incoming_idle, peer);
    }

    return NULL;
}

static gboolean start_listener(int port) {
    int fd;
    int opt = 1;
    struct sockaddr_in serv;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return FALSE;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        close(fd);
        return FALSE;
    }

    if (listen(fd, 32) < 0) {
        close(fd);
        return FALSE;
    }

    app.listen_fd = fd;
    app.running = TRUE;
    pthread_create(&app.accept_thread, NULL, accept_thread_fn, NULL);
    pthread_detach(app.accept_thread);
    return TRUE;
}

static void style_dialog_buttons(GtkWidget *dialog) {
    GtkWidget *cancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    GtkWidget *ok = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget *close = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);

    if (cancel != NULL) {
        gtk_style_context_add_class(gtk_widget_get_style_context(cancel), "dialog-secondary");
    }
    if (ok != NULL) {
        gtk_style_context_add_class(gtk_widget_get_style_context(ok), "dialog-primary");
    }
    if (close != NULL) {
        gtk_style_context_add_class(gtk_widget_get_style_context(close), "dialog-primary");
    }
}

static void open_profile_dialog(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Мой профиль",
        GTK_WINDOW(app.window),
        GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL,
        "Сохранить", GTK_RESPONSE_OK,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 420);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    style_dialog_buttons(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "dialog-card");
    gtk_container_set_border_width(GTK_CONTAINER(card), 14);
    gtk_container_add(GTK_CONTAINER(content), card);

    GtkWidget *title = gtk_label_new("Профиль");
    GtkWidget *hint = gtk_label_new("Данные сохраняются локально в файле проекта и подгружаются при новом запуске.");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "dialog-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dialog-hint");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_halign(hint, GTK_ALIGN_START);

    GtkWidget *first_name_label = gtk_label_new("Имя");
    gtk_style_context_add_class(gtk_widget_get_style_context(first_name_label), "dialog-field");
    gtk_widget_set_halign(first_name_label, GTK_ALIGN_START);
    GtkWidget *first_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(first_name_entry), app.me.first_name);
    gtk_style_context_add_class(gtk_widget_get_style_context(first_name_entry), "dialog-entry");
    gtk_widget_set_size_request(first_name_entry, 0, 42);

    GtkWidget *last_name_label = gtk_label_new("Фамилия");
    gtk_style_context_add_class(gtk_widget_get_style_context(last_name_label), "dialog-field");
    gtk_widget_set_halign(last_name_label, GTK_ALIGN_START);
    GtkWidget *last_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(last_name_entry), app.me.last_name);
    gtk_style_context_add_class(gtk_widget_get_style_context(last_name_entry), "dialog-entry");
    gtk_widget_set_size_request(last_name_entry, 0, 42);

    GtkWidget *username_label = gtk_label_new("Username");
    gtk_style_context_add_class(gtk_widget_get_style_context(username_label), "dialog-field");
    gtk_widget_set_halign(username_label, GTK_ALIGN_START);
    GtkWidget *username_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(username_entry), app.me.username);
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "@username");
    gtk_style_context_add_class(gtk_widget_get_style_context(username_entry), "dialog-entry");
    gtk_widget_set_size_request(username_entry, 0, 42);

    GtkWidget *bio_label = gtk_label_new("Описание");
    gtk_style_context_add_class(gtk_widget_get_style_context(bio_label), "dialog-field");
    gtk_widget_set_halign(bio_label, GTK_ALIGN_START);
    GtkWidget *bio_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(bio_entry), app.me.bio);
    gtk_style_context_add_class(gtk_widget_get_style_context(bio_entry), "dialog-entry");
    gtk_widget_set_size_request(bio_entry, 0, 42);

    GtkWidget *age_label = gtk_label_new("Возраст");
    gtk_style_context_add_class(gtk_widget_get_style_context(age_label), "dialog-field");
    gtk_widget_set_halign(age_label, GTK_ALIGN_START);
    GtkWidget *age_spin = gtk_spin_button_new_with_range(1, 120, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(age_spin), app.me.age);
    gtk_style_context_add_class(gtk_widget_get_style_context(age_spin), "dialog-entry");
    gtk_widget_set_size_request(age_spin, 0, 42);

    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), first_name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), first_name_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), last_name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), last_name_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), username_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), username_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), bio_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), bio_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), age_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), age_spin, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *first_name = gtk_entry_get_text(GTK_ENTRY(first_name_entry));
        const char *last_name = gtk_entry_get_text(GTK_ENTRY(last_name_entry));
        const char *username = gtk_entry_get_text(GTK_ENTRY(username_entry));
        const char *bio = gtk_entry_get_text(GTK_ENTRY(bio_entry));
        int age = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(age_spin));

        apply_my_profile_fields(first_name, last_name, username, bio, age);
        save_profile_to_disk();
        broadcast_hello();
        set_status("Профиль обновлен");
    }

    gtk_widget_destroy(dialog);
}

static void open_remote_profile(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    if (app.active_peer == NULL) {
        return;
    }

    Peer *peer = app.active_peer;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Профиль контакта",
        GTK_WINDOW(app.window),
        GTK_DIALOG_MODAL,
        "Закрыть", GTK_RESPONSE_CLOSE,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "dialog-card");
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_container_add(GTK_CONTAINER(content), box);

    style_dialog_buttons(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 280);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *name = gtk_label_new(peer_display_name(peer));
    GtkWidget *handle = gtk_label_new(peer->profile.nickname);

    char info[512];
    snprintf(info, sizeof(info), "Возраст: %d\nОписание: %s\nАдрес: %s:%d",
             peer->profile.age,
             peer->profile.bio[0] ? peer->profile.bio : "без описания",
             peer->ip,
             peer->port);
    GtkWidget *info_label = gtk_label_new(info);

    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(handle), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(info_label), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "dialog-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(handle), "dialog-hint");

    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), handle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), info_label, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_new_chat(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Новый чат",
        GTK_WINDOW(app.window),
        GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL,
        "Подключить", GTK_RESPONSE_OK,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 420);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    style_dialog_buttons(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "dialog-card");
    gtk_container_set_border_width(GTK_CONTAINER(card), 16);
    gtk_container_add(GTK_CONTAINER(content), card);

    GtkWidget *title = gtk_label_new("Подключение к новому чату");
    GtkWidget *hint = gtk_label_new("Введите параметры подключения. Контакт сразу появится в списке слева.");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "dialog-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dialog-hint");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_halign(hint, GTK_ALIGN_START);

    GtkWidget *alias_label = gtk_label_new("Имя контакта");
    gtk_style_context_add_class(gtk_widget_get_style_context(alias_label), "dialog-field");
    GtkWidget *alias_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(alias_entry), "Например: Алекс");
    gtk_style_context_add_class(gtk_widget_get_style_context(alias_entry), "dialog-entry");
    gtk_widget_set_size_request(alias_entry, 0, 44);

    GtkWidget *ip_label = gtk_label_new("IP адрес хоста");
    gtk_style_context_add_class(gtk_widget_get_style_context(ip_label), "dialog-field");
    GtkWidget *ip_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ip_entry), "127.0.0.1");
    gtk_style_context_add_class(gtk_widget_get_style_context(ip_entry), "dialog-entry");
    gtk_widget_set_size_request(ip_entry, 0, 44);

    GtkWidget *port_label = gtk_label_new("Порт");
    gtk_style_context_add_class(gtk_widget_get_style_context(port_label), "dialog-field");
    GtkWidget *port_entry = gtk_entry_new();
    char port_hint[16];
    snprintf(port_hint, sizeof(port_hint), "%d", APP_DEFAULT_PORT);
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), port_hint);
    gtk_style_context_add_class(gtk_widget_get_style_context(port_entry), "dialog-entry");
    gtk_widget_set_size_request(port_entry, 0, 44);

    gtk_widget_set_halign(alias_label, GTK_ALIGN_START);
    gtk_widget_set_halign(ip_label, GTK_ALIGN_START);
    gtk_widget_set_halign(port_label, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), alias_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), alias_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), ip_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), ip_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), port_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), port_entry, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *alias = gtk_entry_get_text(GTK_ENTRY(alias_entry));
        const char *ip = gtk_entry_get_text(GTK_ENTRY(ip_entry));
        const char *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));

        if (ip[0] == '\0') {
            ip = "127.0.0.1";
        }

        int port = APP_DEFAULT_PORT;
        if (port_str[0] != '\0') {
            port = atoi(port_str);
        }

        if (port <= 0 || port > 65535) {
            set_status("Ошибка: неверный порт");
        } else if (connect_outbound(alias, ip, port, TRUE)) {
            set_status("Чат создан");
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_hide_chat(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    if (app.active_peer == NULL) {
        return;
    }

    Peer *peer = app.active_peer;

    peer_send_line(peer, "BYE");
    peer_disconnect_socket(peer);
    peer->hidden = TRUE;

    if (peer->row) {
        gtk_widget_hide(peer->row);
    }

    set_active_peer(NULL);
    set_status("Чат скрыт");
}

static void on_send(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    if (app.active_peer == NULL) {
        set_status("Выберите чат");
        return;
    }

    Peer *peer = app.active_peer;
    if (!peer->connected) {
        set_status("Контакт отключен");
        return;
    }

    const char *text = gtk_entry_get_text(GTK_ENTRY(app.message_entry));
    if (text == NULL || text[0] == '\0') {
        return;
    }

    char safe[MAX_MSG];
    sanitize_field(text, safe, sizeof(safe));

    char line[MAX_MSG + 10];
    snprintf(line, sizeof(line), "MSG\t%s", safe);

    if (!peer_send_line(peer, line)) {
        set_status("Ошибка отправки");
        return;
    }

    queue_message(peer, TRUE, FALSE, safe);
    gtk_entry_set_text(GTK_ENTRY(app.message_entry), "");
}

static void on_contacts_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    (void)data;

    if (row == NULL) {
        set_active_peer(NULL);
        return;
    }

    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "peer-id"));
    Peer *peer = find_peer_by_id(id);
    if (peer == NULL || peer->hidden) {
        set_active_peer(NULL);
        return;
    }

    set_active_peer(peer);
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
    (void)editable;
    (void)data;
    apply_contacts_filter();
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;

    app.running = FALSE;

    if (app.listen_fd >= 0) {
        shutdown(app.listen_fd, SHUT_RDWR);
        close(app.listen_fd);
        app.listen_fd = -1;
    }

    g_mutex_lock(&app.peers_mutex);
    for (guint i = 0; i < app.peers->len; i++) {
        Peer *peer = g_ptr_array_index(app.peers, i);
        peer_send_line(peer, "BYE");
        peer_disconnect_socket(peer);
    }
    g_mutex_unlock(&app.peers_mutex);

    return FALSE;
}

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    GError *err = NULL;

    gtk_css_provider_load_from_data(provider, XCHAT_CSS, -1, &err);
    if (err != NULL) {
        g_warning("CSS error: %s", err->message);
        g_clear_error(&err);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

static GtkWidget *build_empty_page(void) {
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *icon_wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *icon = gtk_label_new("✉");
    GtkWidget *title = gtk_label_new("Начать разговор");
    GtkWidget *subtitle = gtk_label_new("Выберите один из существующих чатов или создайте новый.");
    GtkWidget *button = gtk_button_new_with_label("Новый чат");

    gtk_style_context_add_class(gtk_widget_get_style_context(wrap), "empty-wrap");
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_wrap), "empty-icon-wrap");
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "empty-icon");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "empty-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "empty-subtitle");
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "new-chat-btn");

    gtk_widget_set_hexpand(wrap, TRUE);
    gtk_widget_set_vexpand(wrap, TRUE);
    gtk_widget_set_halign(wrap, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(wrap, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(icon_wrap, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(button, 150, 0);
    gtk_label_set_line_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(subtitle), 46);
    gtk_label_set_justify(GTK_LABEL(subtitle), GTK_JUSTIFY_CENTER);

    gtk_container_add(GTK_CONTAINER(icon_wrap), icon);

    gtk_box_pack_start(GTK_BOX(wrap), icon_wrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wrap), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wrap), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wrap), button, FALSE, FALSE, 0);

    g_signal_connect(button, "clicked", G_CALLBACK(on_new_chat), NULL);

    return wrap;
}

static GtkWidget *build_chat_page(void) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title_wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    app.chat_title = gtk_label_new("Чат");
    app.chat_subtitle = gtk_label_new("Выберите контакт");

    gtk_style_context_add_class(gtk_widget_get_style_context(top), "chat-topbar");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.chat_title), "chat-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.chat_subtitle), "chat-subtitle");

    gtk_widget_set_halign(app.chat_title, GTK_ALIGN_START);
    gtk_widget_set_halign(app.chat_subtitle, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(title_wrap), app.chat_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_wrap), app.chat_subtitle, FALSE, FALSE, 0);

    app.remote_profile_button = gtk_button_new_with_label("Профиль");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.remote_profile_button), "soft-btn");
    g_signal_connect(app.remote_profile_button, "clicked", G_CALLBACK(open_remote_profile), NULL);

    gtk_box_pack_start(GTK_BOX(top), title_wrap, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(top), app.remote_profile_button, FALSE, FALSE, 0);

    app.messages_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.messages_list), GTK_SELECTION_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.messages_list), "chat-list");

    app.messages_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(app.messages_scrolled), app.messages_list);

    GtkWidget *composer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(composer), "composer");

    app.hide_button = gtk_button_new_with_label("Скрыть чат");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.hide_button), "soft-btn");
    g_signal_connect(app.hide_button, "clicked", G_CALLBACK(on_hide_chat), NULL);

    app.message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.message_entry), "Сообщение");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.message_entry), "compose-entry");

    app.send_button = gtk_button_new_with_label("Отправить");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.send_button), "send-btn");

    g_signal_connect(app.send_button, "clicked", G_CALLBACK(on_send), NULL);
    g_signal_connect(app.message_entry, "activate", G_CALLBACK(on_send), NULL);

    gtk_box_pack_start(GTK_BOX(composer), app.hide_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(composer), app.message_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(composer), app.send_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app.messages_scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), composer, FALSE, FALSE, 0);

    return root;
}

static GtkWidget *build_left_panel(void) {
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(left, 420, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(left), "left-panel");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "left-header");

    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = gtk_label_new("Чат");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "left-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    app.profile_btn = gtk_button_new_with_label("Профиль");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.profile_btn), "soft-btn");
    g_signal_connect(app.profile_btn, "clicked", G_CALLBACK(open_profile_dialog), NULL);

    gtk_box_pack_start(GTK_BOX(title_row), title, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(title_row), app.profile_btn, FALSE, FALSE, 0);

    GtkWidget *search_wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(search_wrap), "search-wrap");

    app.search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.search_entry), "Поиск");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.search_entry), "search-entry");
    g_signal_connect(app.search_entry, "changed", G_CALLBACK(on_search_changed), NULL);

    gtk_box_pack_start(GTK_BOX(search_wrap), app.search_entry, FALSE, FALSE, 0);

    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(filters), "filters-row");
    GtkWidget *all_btn = gtk_button_new_with_label("Все");
    GtkWidget *req_btn = gtk_button_new_with_label("Запросы");
    gtk_style_context_add_class(gtk_widget_get_style_context(all_btn), "filter-pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(all_btn), "active");
    gtk_style_context_add_class(gtk_widget_get_style_context(req_btn), "filter-pill");
    gtk_box_pack_start(GTK_BOX(filters), all_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters), req_btn, FALSE, FALSE, 0);

    app.contacts_list = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(app.contacts_list), "contacts-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.contacts_list), GTK_SELECTION_SINGLE);
    g_signal_connect(app.contacts_list, "row-selected", G_CALLBACK(on_contacts_row_selected), NULL);

    GtkWidget *contacts_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(contacts_scrolled), app.contacts_list);

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(footer), "left-footer");

    GtkWidget *me_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *me_texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    app.me_name = gtk_label_new("Я");
    app.me_subtitle = gtk_label_new("профиль");

    gtk_style_context_add_class(gtk_widget_get_style_context(app.me_name), "contact-name");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.me_subtitle), "contact-preview");

    gtk_widget_set_halign(app.me_name, GTK_ALIGN_START);
    gtk_widget_set_halign(app.me_subtitle, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(me_texts), app.me_name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(me_texts), app.me_subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(me_box), me_texts, TRUE, TRUE, 0);

    GtkWidget *footer_new_chat = gtk_button_new_with_label("Новый чат");
    gtk_style_context_add_class(gtk_widget_get_style_context(footer_new_chat), "new-chat-btn");
    g_signal_connect(footer_new_chat, "clicked", G_CALLBACK(on_new_chat), NULL);

    gtk_box_pack_start(GTK_BOX(footer), me_box, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(footer), footer_new_chat, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), title_row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(left), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), search_wrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), filters, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), contacts_scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left), footer, FALSE, FALSE, 0);

    return left;
}

static void init_default_profile(void) {
#if APP_ROLE_SERVER
    g_strlcpy(app.me.first_name, "Server", sizeof(app.me.first_name));
    g_strlcpy(app.me.last_name, "Host", sizeof(app.me.last_name));
    g_strlcpy(app.me.username, "@server_host", sizeof(app.me.username));
    g_strlcpy(app.me.bio, "Серверный аккаунт", sizeof(app.me.bio));
#else
    g_strlcpy(app.me.first_name, "New", sizeof(app.me.first_name));
    g_strlcpy(app.me.last_name, "User", sizeof(app.me.last_name));
    g_strlcpy(app.me.username, "@new_user", sizeof(app.me.username));
    g_strlcpy(app.me.bio, "Пользователь xchat", sizeof(app.me.bio));
#endif
    app.me.age = 20;
    profile_refresh_public_fields(&app.me);
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;

    memset(&app, 0, sizeof(app));
    app.listen_fd = -1;
    app.next_peer_id = 1;
    app.peers = g_ptr_array_new_with_free_func(peer_free);
    g_mutex_init(&app.peers_mutex);

    init_default_profile();
    load_profile_from_disk();

    load_css();

    app.window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app.window), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1380, 860);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(on_window_delete), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(root), "root");
    gtk_container_add(GTK_CONTAINER(app.window), root);

    GtkWidget *left = build_left_panel();

    GtkWidget *right_wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(right_wrap), "right-panel");

    app.right_stack = gtk_stack_new();
    app.empty_page = build_empty_page();
    app.chat_page = build_chat_page();

    gtk_stack_add_named(GTK_STACK(app.right_stack), app.empty_page, "empty");
    gtk_stack_add_named(GTK_STACK(app.right_stack), app.chat_page, "chat");
    gtk_stack_set_visible_child(GTK_STACK(app.right_stack), app.empty_page);

    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(status), "status-strip");
    app.status_label = gtk_label_new("Готово");
    gtk_style_context_add_class(gtk_widget_get_style_context(app.status_label), "status-label");
    gtk_widget_set_halign(app.status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(status), app.status_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(right_wrap), app.right_stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(right_wrap), status, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), left, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), right_wrap, TRUE, TRUE, 0);

    update_me_panel();

#if APP_ROLE_SERVER
    if (start_listener(APP_DEFAULT_PORT)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Сервер слушает 0.0.0.0:%d", APP_DEFAULT_PORT);
        set_status(msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Ошибка запуска сервера на порту %d", APP_DEFAULT_PORT);
        set_status(msg);
    }
#else
    set_status("Выберите чат или создайте новый");
#endif

    gtk_widget_show_all(app.window);
}

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    int status;

    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    app.running = FALSE;
    if (app.listen_fd >= 0) {
        close(app.listen_fd);
        app.listen_fd = -1;
    }

    if (app.peers != NULL) {
        g_ptr_array_free(app.peers, TRUE);
        app.peers = NULL;
    }

    g_mutex_clear(&app.peers_mutex);
    g_object_unref(gtk_app);
    return status;
}
