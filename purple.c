/*
 * pidgin
 *
 * Pidgin is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 */

#include "purple.h"

#include <glib.h>

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CUSTOM_USER_DIRECTORY  "/dev/null"
#define CUSTOM_PLUGIN_PATH     ""
#define PLUGIN_SAVE_PREF       "/purple/nullclient/plugins/saved"
#define UI_ID                  "nullclient"

#define NAME			"alice"
#define CHAT			"room"

PurpleConversation *chat_g = NULL;

char *alice(const char *source, const char *msgin, PurpleConversation *conv) {
	int s, t, len, chk = 0;
	char *ptr;
	struct sockaddr_un remote;
	char str[1024];
	static char msg[1024];
	gchar *error = NULL;

TOP:
	if (!chk)
		snprintf(str,sizeof(str)-1,"%s\007%s",source,msgin);
	else
		snprintf(str,sizeof(str)-1,"%s",source);

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		return "error in socket()";
	}

	remote.sun_family = AF_UNIX;
	strcpy(remote.sun_path, "/tmp/rs");
	len = strlen(remote.sun_path) + sizeof(remote.sun_family);
	if (connect(s, (struct sockaddr *)&remote, len) == -1) {
		return "error in connect()";
	}	

	if (send(s, str, strlen(str), 0) == -1) {
		return "error in send()";
	}

	if ((t=recv(s, str, 1024, 0)) > 0) {
		str[t] = '\0';
	} else {
		return "error in recv()";
	}

	close(s);

	if (chk) {
		if (!strcasecmp(str, "abusive")) {
			snprintf(str,sizeof(str)-1,"kick %s",source);
			purple_cmd_do_command(conv, str, str, &error);

			if (error) {
				snprintf(msg,sizeof(msg)-1,"%s",error);
				free(error);
			} else
				snprintf(msg,sizeof(msg)-1,"<s>%s</s>",source);
		}
	} else {
		strncpy(msg, str, 1024);
		if (conv == chat_g) {
			chk = 1;
			goto TOP;
		}
	}

	// pause for typing reality-ness
/*	len = strlen(msg);
	if (len > 20)
		len = 20;
	for (t = 0; t < len; t++)
		usleep(99000);*/

	return msg;
}

typedef struct {
	PurpleConversation *conv;
	char *name;
	char *msg;
} queue_item_t;

typedef struct {
	queue_item_t **q;
	size_t len;
} queue_t;

static gboolean
pop(gpointer p) {
	queue_t *q = (queue_t*)p;
	queue_item_t *item;

	if (q->len > 0) {
		item = q->q[0];

		q->len--;
		if (q->len > 0) {
			memmove(q->q, &(q->q[1]), q->len * sizeof(queue_item_t*));
	//		q->q = realloc(q->q, q->len * sizeof(queue_item_t*));
		}

		if (purple_conversation_get_type(item->conv) == PURPLE_CONV_TYPE_IM)
			purple_conv_im_send(PURPLE_CONV_IM(item->conv), alice(item->name, item->msg, item->conv));
		else if (purple_conversation_get_type(item->conv) == PURPLE_CONV_TYPE_CHAT)
			purple_conv_chat_send(PURPLE_CONV_CHAT(item->conv),alice(item->name, item->msg, item->conv));

		free(item->name);
		free(item->msg);
		free(item);
	}

	return 1;
}

static void
push(queue_t *q, PurpleConversation *conv, const char *name, const char *msg) {
	queue_item_t *new = g_new0(queue_item_t, 1);
	new->conv = conv;
	new->name = strdup(name);
	new->msg = strdup(msg);

	q->q = realloc(q->q, (q->len+1) * sizeof(queue_item_t*));
	q->q[q->len] = new;
	q->len++;
}

queue_t *queue;

/**
 * The following eventloop functions are used in both pidgin and purple-text. If your
 * application uses glib mainloop, you can safely use this verbatim.
 */
#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

typedef struct _PurpleGLibIOClosure {
	PurpleInputFunction function;
	guint result;
	gpointer data;
} PurpleGLibIOClosure;

static void purple_glib_io_destroy(gpointer data)
{
	g_free(data);
}

static gboolean purple_glib_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
	PurpleGLibIOClosure *closure = data;
	PurpleInputCondition purple_cond = 0;

	if (condition & PURPLE_GLIB_READ_COND)
		purple_cond |= PURPLE_INPUT_READ;
	if (condition & PURPLE_GLIB_WRITE_COND)
		purple_cond |= PURPLE_INPUT_WRITE;

	closure->function(closure->data, g_io_channel_unix_get_fd(source),
			  purple_cond);

	return TRUE;
}

static guint glib_input_add(gint fd, PurpleInputCondition condition, PurpleInputFunction function,
							   gpointer data)
{
	PurpleGLibIOClosure *closure = g_new0(PurpleGLibIOClosure, 1);
	GIOChannel *channel;
	GIOCondition cond = 0;

	closure->function = function;
	closure->data = data;

	if (condition & PURPLE_INPUT_READ)
		cond |= PURPLE_GLIB_READ_COND;
	if (condition & PURPLE_INPUT_WRITE)
		cond |= PURPLE_GLIB_WRITE_COND;

#if defined _WIN32 && !defined WINPIDGIN_USE_GLIB_IO_CHANNEL
	channel = wpurple_g_io_channel_win32_new_socket(fd);
#else
	channel = g_io_channel_unix_new(fd);
#endif
	closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
					      purple_glib_io_invoke, closure, purple_glib_io_destroy);

	g_io_channel_unref(channel);
	return closure->result;
}

static PurpleEventLoopUiOps glib_eventloops =
{
	g_timeout_add,
	g_source_remove,
	glib_input_add,
	g_source_remove,
	NULL,
#if GLIB_CHECK_VERSION(2,14,0)
	g_timeout_add_seconds,
#else
	NULL,
#endif

	/* padding */
	NULL,
	NULL,
	NULL
};
/*** End of the eventloop functions. ***/

/*** Conversation uiops ***/
static void
null_write_conv(PurpleConversation *conv, const char *who, const char *alias,
			const char *message, PurpleMessageFlags flags, time_t mtime)
{
	const char *name;
	char *msg;

	//if (alias && *alias)
	//	name = alias;
	if (who && *who)
		name = who;
	else
		name = NULL;

/*	printf("(%s) %s %s: %s\n", purple_conversation_get_name(conv),
			purple_utf8_strftime("(%H:%M:%S)", localtime(&mtime)),
			name, message);*/

	if (!(flags & PURPLE_MESSAGE_RECV))
		return;

	msg = purple_markup_strip_html(message);

	push(queue, conv, name, msg);

	free(msg);
}

static PurpleConversationUiOps null_conv_uiops =
{
	NULL,                      /* create_conversation  */
	NULL,                      /* destroy_conversation */
	NULL,                      /* write_chat           */
	NULL,                      /* write_im             */
	null_write_conv,           /* write_conv           */
	NULL,                      /* chat_add_users       */
	NULL,                      /* chat_rename_user     */
	NULL,                      /* chat_remove_users    */
	NULL,                      /* chat_update_user     */
	NULL,                      /* present              */
	NULL,                      /* has_focus            */
	NULL,                      /* custom_smiley_add    */
	NULL,                      /* custom_smiley_write  */
	NULL,                      /* custom_smiley_close  */
	NULL,                      /* send_confirm         */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
null_ui_init(void)
{
	/**
	 * This should initialize the UI components for all the modules. Here we
	 * just initialize the UI for conversations.
	 */
	purple_conversations_set_ui_ops(&null_conv_uiops);
}

static PurpleCoreUiOps null_core_uiops =
{
	NULL,
	NULL,
	null_ui_init,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static gint
chat_invited(PurpleAccount *account, const char *inviter,
	const char *chat, const char *invite_message,
	const GHashTable *components)
{
	if (chat_g != NULL) {
		printf("Inviting %s\n", inviter);
	
		serv_chat_invite(purple_account_get_connection(account), 
			purple_conv_chat_get_id(PURPLE_CONV_CHAT(chat_g)),
			":)", inviter);
	}

	return 1;
}

static void
init_libpurple(void)
{
	/* Set a custom user directory (optional) */
	purple_util_set_user_dir(CUSTOM_USER_DIRECTORY);

	/* We do not want any debugging for now to keep the noise to a minimum. */
	purple_debug_set_enabled(FALSE);

	/* Set the core-uiops, which is used to
	 * 	- initialize the ui specific preferences.
	 * 	- initialize the debug ui.
	 * 	- initialize the ui components for all the modules.
	 * 	- uninitialize the ui components for all the modules when the core terminates.
	 */
	purple_core_set_ui_ops(&null_core_uiops);

	/* Set the uiops for the eventloop. If your client is glib-based, you can safely
	 * copy this verbatim. */
	purple_eventloop_set_ui_ops(&glib_eventloops);

	/* Set path to search for plugins. The core (libpurple) takes care of loading the
	 * core-plugins, which includes the protocol-plugins. So it is not essential to add
	 * any path here, but it might be desired, especially for ui-specific plugins. */
	purple_plugins_add_search_path(CUSTOM_PLUGIN_PATH);

	/* Now that all the essential stuff has been set, let's try to init the core. It's
	 * necessary to provide a non-NULL name for the current ui to the core. This name
	 * is used by stuff that depends on this ui, for example the ui-specific plugins. */
	if (!purple_core_init(UI_ID)) {
		/* Initializing the core failed. Terminate. */
		fprintf(stderr,
				"libpurple initialization failed. Dumping core.\n"
				"Please report this!\n");
		abort();
	}

	/* Create and load the buddylist. */
	purple_set_blist(purple_blist_new());
	purple_blist_load();

	/* Load the preferences. */
	purple_prefs_load();

	/* Load the desired plugins. The client should save the list of loaded plugins in
	 * the preferences using purple_plugins_save_loaded(PLUGIN_SAVE_PREF) */
	purple_plugins_load_saved(PLUGIN_SAVE_PREF);

	/* Load the pounces. */
	purple_pounces_load();
}

static gboolean
join_chat(gpointer gc)
{
	GHashTable *params;

	if (chat_g != NULL)
		return 0;

	params = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	printf("Joining %s...\n", CHAT);

	g_hash_table_insert(params, g_strdup("exchange"), g_strdup("4"));
	g_hash_table_insert(params, g_strdup("room"), g_strdup(CHAT));

	serv_join_chat(gc, params);
	g_hash_table_destroy(params);

	return 1;
}

static void
signed_on(PurpleConnection *gc, gpointer null)
{
	PurpleAccount *account = purple_connection_get_account((PurpleConnection*)gc);
	printf("Account connected: %s %s\n", account->username, account->protocol_id);

	join_chat((gpointer)gc);
	purple_timeout_add_seconds(60, join_chat, (gpointer)gc);

	queue = g_new0(queue_t, 1);
	purple_timeout_add_seconds(3, pop, queue);
}

static void
chat_joined(PurpleConversation *conv)
{
	const char *room = purple_conversation_get_name(conv);
	printf("Joined chat: %s\n", room);
	if (!strcasecmp(CHAT, room))
		chat_g = conv;
}

static void
connect_to_signals_for_demonstration_purposes_only(void)
{
	static int signed_on_handle, chat_invited_handle, chat_joined_handle;

	purple_signal_connect(purple_connections_get_handle(), "signed-on", &signed_on_handle,
				PURPLE_CALLBACK(signed_on), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "chat-invited", &chat_invited_handle,
				PURPLE_CALLBACK(chat_invited), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "chat-joined", &chat_joined_handle,
				PURPLE_CALLBACK(chat_joined), NULL);
}

int main(int argc, char *argv[])
{
	GList *iter;
	int i, num;
	GList *names = NULL;
	const char *prpl;
	char name[128];
	char *password;
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	PurpleAccount *account;
	PurpleSavedStatus *status;
	char *res;

#ifndef _WIN32
	/* libpurple's built-in DNS resolution forks processes to perform
	 * blocking lookups without blocking the main process.  It does not
	 * handle SIGCHLD itself, so if the UI does not you quickly get an army
	 * of zombie subprocesses marching around.
	 */
	signal(SIGCHLD, SIG_IGN);
#endif

	init_libpurple();

	printf("libpurple initialized.\n");

	iter = purple_plugins_get_protocols();
	for (i = 0; iter; iter = iter->next) {
		PurplePlugin *plugin = iter->data;
		PurplePluginInfo *info = plugin->info;
		if (info && info->name) {
			printf("\t%d: %s\n", i++, info->name);
			names = g_list_append(names, info->id);
		}
	}
	printf("Select the protocol [0-%d]: ", i-1);
	res = fgets(name, sizeof(name), stdin);
	if (!res) {
		fprintf(stderr, "Failed to gets protocol selection.");
		abort();
	}
	sscanf(name, "%d", &num);
	prpl = g_list_nth_data(names, num);

	printf("Username: ");
	res = fgets(name, sizeof(name), stdin);
	if (!res) {
		fprintf(stderr, "Failed to read user name.");
		abort();
	}
	name[strlen(name) - 1] = 0;  /* strip the \n at the end */

	/* Create the account */
	account = purple_account_new(name, prpl);

	/* Get the password for the account */
	password = getpass("Password: ");
	purple_account_set_password(account, password);

	/* It's necessary to enable the account first. */
	purple_account_set_enabled(account, UI_ID, TRUE);

	/* Now, to connect the account(s), create a status and activate it. */
	status = purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE);
	purple_savedstatus_activate(status);

	connect_to_signals_for_demonstration_purposes_only();

	g_main_loop_run(loop);

	return 0;
}
