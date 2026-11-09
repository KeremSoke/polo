/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A libSM client for test-xsmp.py: xsmp-test-client LOG MODE
 *
 * Connects through SESSION_MANAGER, sets its properties, appends a line
 * per message to LOG and exits on Die. MODE "normal" answers every
 * SaveYourself with SaveYourselfDone; "interact-cancel" asks to interact
 * on a shutdown it may interact in, then cancels the shutdown.
 */

#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/ICE/ICElib.h>
#include <X11/SM/SMlib.h>

static const char *log_path;
static int interact_cancel;
static int running = 1;

static void
record(const char *fmt, ...)
{
	FILE *log = fopen(log_path, "a");
	va_list args;

	if (!log)
		return;
	va_start(args, fmt);
	vfprintf(log, fmt, args);
	va_end(args);
	fputc('\n', log);
	fclose(log);
}

static void
interact(SmcConn conn, SmPointer data)
{
	record("Interact");
	SmcInteractDone(conn, True);
}

static void
save_yourself(SmcConn conn, SmPointer data, int save_type, Bool shutdown,
	      int interact_style, Bool fast)
{
	record("SaveYourself %d %d %d %d", save_type, shutdown, interact_style,
	       fast);
	if (interact_cancel && shutdown &&
	    interact_style == SmInteractStyleAny) {
		SmcInteractRequest(conn, SmDialogNormal, interact, NULL);
		return;
	}
	SmcSaveYourselfDone(conn, True);
}

static void
die(SmcConn conn, SmPointer data)
{
	record("Die");
	running = 0;
}

static void
save_complete(SmcConn conn, SmPointer data)
{
	record("SaveComplete");
}

static void
shutdown_cancelled(SmcConn conn, SmPointer data)
{
	record("ShutdownCancelled");
}

static void
set_props(SmcConn conn, char *self)
{
	char pid[32];
	char style = SmRestartIfRunning;
	SmPropValue program = { (int)strlen("xsmp-test-client"),
				"xsmp-test-client" };
	SmPropValue user = { 4, "test" };
	SmPropValue restart[3] = { { (int)strlen(self), self },
				   { (int)strlen(log_path), (char *)log_path },
				   { 6, "normal" } };
	SmPropValue discard = { 4, "true" };
	SmPropValue pid_value;
	SmPropValue style_value = { 1, &style };
	SmProp props[] = {
		{ SmProgram, SmARRAY8, 1, &program },
		{ SmUserID, SmARRAY8, 1, &user },
		{ SmRestartCommand, SmLISTofARRAY8, 3, restart },
		{ SmCloneCommand, SmLISTofARRAY8, 3, restart },
		{ SmDiscardCommand, SmLISTofARRAY8, 1, &discard },
		{ SmProcessID, SmARRAY8, 1, &pid_value },
		{ SmRestartStyleHint, SmCARD8, 1, &style_value },
	};
	SmProp *list[sizeof props / sizeof props[0]];

	snprintf(pid, sizeof pid, "%d", (int)getpid());
	pid_value.length = (int)strlen(pid);
	pid_value.value = pid;
	for (size_t i = 0; i < sizeof props / sizeof props[0]; i++)
		list[i] = &props[i];
	SmcSetProperties(conn, sizeof props / sizeof props[0], list);
}

int
main(int argc, char **argv)
{
	SmcCallbacks callbacks = { 0 };
	char error[256];
	char *client_id = NULL;
	SmcConn conn;
	struct pollfd fd;

	if (argc < 3)
		return 2;
	log_path = argv[1];
	interact_cancel = strcmp(argv[2], "interact-cancel") == 0;
	callbacks.save_yourself.callback = save_yourself;
	callbacks.die.callback = die;
	callbacks.save_complete.callback = save_complete;
	callbacks.shutdown_cancelled.callback = shutdown_cancelled;
	conn = SmcOpenConnection(
		NULL, NULL, SmProtoMajor, SmProtoMinor,
		SmcSaveYourselfProcMask | SmcDieProcMask |
			SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask,
		&callbacks, NULL, &client_id, sizeof error, error);
	if (!conn) {
		record("failed %s", error);
		return 1;
	}
	set_props(conn, argv[0]);
	record("registered %s %d", client_id, (int)getpid());
	free(client_id);

	fd.fd = IceConnectionNumber(SmcGetIceConnection(conn));
	if (strncmp(argv[2], "stall-", 6) == 0) {
		unsigned char header[8] = { 1, 0, 0, 0 };
		uint32_t units =
			strcmp(argv[2], "stall-huge") == 0 ? 0xc0001 : 4;

		memcpy(header + 4, &units, sizeof units);
		if (write(fd.fd, header, sizeof header) != sizeof header)
			return 1;
		record("stalled");
	}
	fd.events = POLLIN;
	while (running && poll(&fd, 1, -1) > 0) {
		if (IceProcessMessages(SmcGetIceConnection(conn), NULL, NULL) !=
		    IceProcessMessagesSuccess)
			break;
	}
	SmcCloseConnection(conn, 0, NULL);
	record("closed");
	return 0;
}
