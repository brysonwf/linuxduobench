// SPDX-License-Identifier: GPL-2.0-or-later
// duo-gps: one long-lived QMI LOC client over QRTR (docs/research/workbench/modem.md "GPS").
// qmicli cannot hold a LOC session: every invocation gets a new client and the session ends with it ("Unknown client
// 1 for service loc"), so --loc-start + --loc-follow-* never sees a report. This keeps ONE client: register events
// (position, satellites, NMEA), set the NMEA types, start a periodic session, print what arrives, stop at the end.
// Build ON the workbench (dev overlay: gcc + libqmi-dev + glib-dev):
//   gcc -O2 -o duo-gps duo-gps.c $(pkg-config --cflags --libs qmi-glib)
// usage: duo-gps [seconds=120] [qrtr-node=0]   (modem in low-power mode is enough to try - never 'online' unattended)
#include <libqmi-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GMainLoop *loop;
static QmiDevice *dev;
static QmiClientLoc *loc;
static int secs = 120, node_id = 0, n_nmea, n_sv, n_pos;

static void die(const char *what, GError *e)
{
	fprintf(stderr, "duo-gps: %s: %s\n", what, e ? e->message : "?");
	exit(1);
}

static void on_nmea(QmiClientLoc *c, QmiIndicationLocNmeaOutput *o, gpointer d)
{
	const gchar *s = NULL;
	if (qmi_indication_loc_nmea_output_get_nmea_string(o, &s, NULL) && s) {
		n_nmea++;
		// GGA/RMC carry the user's coordinates once there is a fix: print the sentence type only, unless
		// DUO_GPS_NMEA=1 (local debugging; never commit such a log)
		if (getenv("DUO_GPS_NMEA")) {
			printf("NMEA %s", s);
			if (s[0] && s[strlen(s) - 1] != '\n')
				printf("\n");
		} else if (strncmp(s + 3, "GSV", 3) == 0)
			printf("NMEA %.40s\n", s);	/* satellites in view: no position in it */
		else if (n_nmea % 50 == 1)
			printf("NMEA %.6s ... (%d sentences so far)\n", s, n_nmea);
	}
}

static void on_sv(QmiClientLoc *c, QmiIndicationLocGnssSvInfoOutput *o, gpointer d)
{
	GArray *a = NULL;
	n_sv++;
	if (qmi_indication_loc_gnss_sv_info_output_get_list(o, &a, NULL) && a) {
		guint used = 0;
		printf("SV report: %u satellites:", a->len);
		for (guint i = 0; i < a->len; i++) {
			QmiIndicationLocGnssSvInfoOutputListElement *e =
				&g_array_index(a, QmiIndicationLocGnssSvInfoOutputListElement, i);
			printf(" %s/%u snr %.0f", qmi_loc_system_get_string(e->system), e->gnss_satellite_id, e->signal_to_noise_ratio_bhz);
			if (e->signal_to_noise_ratio_bhz > 0)
				used++;
		}
		printf(" (%u with signal)\n", used);
	} else
		printf("SV report: no list\n");
}

static void on_pos(QmiClientLoc *c, QmiIndicationLocPositionReportOutput *o, gpointer d)
{
	QmiLocSessionStatus st;
	gdouble lat = 0, lon = 0;
	n_pos++;
	qmi_indication_loc_position_report_output_get_session_status(o, &st, NULL);
	qmi_indication_loc_position_report_output_get_latitude(o, &lat, NULL);
	qmi_indication_loc_position_report_output_get_longitude(o, &lon, NULL);
	gfloat unc = -1; guint8 tech_n = 0;
	QmiLocTechnologyUsed tech = 0;
	qmi_indication_loc_position_report_output_get_horizontal_uncertainty_circular(o, &unc, NULL);
	if (qmi_indication_loc_position_report_output_get_technology_used(o, &tech, NULL)) tech_n = 1;
	// coordinates are the user's location: print only whether a fix exists, never the numbers. The uncertainty + the
	// technology mask tell a coarse (cell / last-known) position from a real satellite fix.
	printf("POSITION status=%s fix=%s unc=%.0f m tech=%s0x%x\n", qmi_loc_session_status_get_string(st),
	       (lat != 0 || lon != 0) ? "yes (coordinates withheld)" : "no", unc, tech_n ? "" : "n/a ", (unsigned)tech);
}

static gboolean finish(gpointer d)
{
	QmiMessageLocStopInput *in = qmi_message_loc_stop_input_new();
	qmi_message_loc_stop_input_set_session_id(in, 1, NULL);
	qmi_client_loc_stop(loc, in, 5, NULL, NULL, NULL);
	qmi_message_loc_stop_input_unref(in);
	printf("duo-gps: %d s done: %d NMEA, %d SV reports, %d position reports\n", secs, n_nmea, n_sv, n_pos);
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

static void started(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QmiMessageLocStartOutput *out = qmi_client_loc_start_finish(QMI_CLIENT_LOC(o), r, &e);
	if (!out || !qmi_message_loc_start_output_get_result(out, &e))
		die("start", e);
	printf("duo-gps: session started, listening %d s\n", secs);
	g_timeout_add_seconds(secs, finish, NULL);
}

static void xtra_or_session(void);
static void nmea_set(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QmiMessageLocSetNmeaTypesOutput *out = qmi_client_loc_set_nmea_types_finish(QMI_CLIENT_LOC(o), r, &e);
	if (!out || !qmi_message_loc_set_nmea_types_output_get_result(out, &e))
		fprintf(stderr, "duo-gps: set NMEA types: %s (continuing)\n", e ? e->message : "?");
	QmiMessageLocStartInput *in = qmi_message_loc_start_input_new();
	qmi_message_loc_start_input_set_session_id(in, 1, NULL);
	qmi_message_loc_start_input_set_fix_recurrence_type(in, QMI_LOC_FIX_RECURRENCE_TYPE_REQUEST_PERIODIC_FIXES, NULL);
	qmi_message_loc_start_input_set_intermediate_report_state(in, QMI_LOC_INTERMEDIATE_REPORT_STATE_ENABLE, NULL);
	qmi_message_loc_start_input_set_minimum_interval_between_position_reports(in, 1000, NULL);
	qmi_client_loc_start(loc, in, 10, NULL, started, NULL);
	qmi_message_loc_start_input_unref(in);
}

/* ---- engine lock + XTRA version check (2026-11-07, CAF location_service_v02.h IDL minor 0x23; no "master client"
 * message exists there). DUO_GPS_LOCK=get prints the lock (GET_ENGINE_LOCK 0x003B -> indication TLV 0x01 status,
 * TLV 0x10 lockType: 1 none, 2 MI, 3 MT, 4 all); DUO_GPS_LOCK=none first sets it (SET_ENGINE_LOCK 0x003A, TLV 0x01).
 * A lock on mobile-initiated sessions could explain refused injections. DUO_GPS_VERCHECK=0 sends
 * SET_XTRA_VERSION_CHECK 0x0085 TLV 0x01 = 0 (disable; status 10 would be an XTRA version failure). */
static void raw_u32(guint16 id, int with_tlv, guint32 v, const char *what)
{
	GError *e = NULL; gsize off;
	QmiMessage *m = qmi_message_new(QMI_SERVICE_LOC, qmi_client_get_cid(QMI_CLIENT(loc)),
					qmi_client_get_next_transaction_id(QMI_CLIENT(loc)), id);
	if (with_tlv && (!(off = qmi_message_tlv_write_init(m, 0x01, &e)) ||
	    !qmi_message_tlv_write_guint32(m, QMI_ENDIAN_LITTLE, v, &e) || !qmi_message_tlv_write_complete(m, off, &e))) {
		printf("%s: build failed\n", what); qmi_message_unref(m); return;
	}
	qmi_device_command_full(dev, m, NULL, 10, NULL, NULL, NULL);
	qmi_message_unref(m);
	printf("%s sent (0x%04x)\n", what, id);
}
static void on_ctl_ind(QmiDevice *d, QmiMessage *m, gpointer u)
{
	guint16 id = qmi_message_get_message_id(m);
	if (qmi_message_get_service(m) != QMI_SERVICE_LOC || (id != 0x003A && id != 0x003B && id != 0x0085)) return;
	gsize off = 0; guint32 st = 99, lock = 0; GError *e = NULL;
	gsize len = qmi_message_tlv_read_init(m, 0x01, NULL, &e);
	if (len) qmi_message_tlv_read_guint32(m, len, &off, QMI_ENDIAN_LITTLE, &st, &e);
	if (id == 0x003B) {
		off = 0; g_clear_error(&e);
		if ((len = qmi_message_tlv_read_init(m, 0x10, NULL, &e))) qmi_message_tlv_read_guint32(m, len, &off, QMI_ENDIAN_LITTLE, &lock, &e);
		printf("engine lock: status %u, lockType %u (%s)\n", st, lock,
		       lock == 1 ? "none" : lock == 2 ? "MOBILE-INITIATED LOCKED" : lock == 3 ? "MT locked" : lock == 4 ? "ALL LOCKED" : "?");
	} else
		printf("%s indication: status %u (%s)\n", id == 0x003A ? "set engine lock" : "xtra version check", st, st == 0 ? "success" : "FAILED");
}
static void engine_controls(void)
{
	static gboolean hooked;
	const char *l = getenv("DUO_GPS_LOCK"), *v = getenv("DUO_GPS_VERCHECK");
	if (!l && !v) return;
	if (!hooked) { g_signal_connect(dev, "indication", G_CALLBACK(on_ctl_ind), NULL); hooked = TRUE; }
	if (l && !strcmp(l, "none")) raw_u32(0x003A, 1, 1, "set engine lock = none");
	if (l) raw_u32(0x003B, 0, 0, "get engine lock");
	if (v) raw_u32(0x0085, 1, (guint32)strtoul(v, NULL, 0), "xtra version check");
}

static void registered(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QmiMessageLocRegisterEventsOutput *out = qmi_client_loc_register_events_finish(QMI_CLIENT_LOC(o), r, &e);
	if (!out || !qmi_message_loc_register_events_output_get_result(out, &e))
		die("register events", e);
	engine_controls();
	if (getenv("DUO_GPS_STANDALONE")) {            /* -> on_opmode -> XTRA (if any) -> begin_session */
		QmiMessageLocSetOperationModeInput *in = qmi_message_loc_set_operation_mode_input_new();
		qmi_message_loc_set_operation_mode_input_set_operation_mode(in, QMI_LOC_OPERATION_MODE_STANDALONE, NULL);
		qmi_client_loc_set_operation_mode(loc, in, 10, NULL, NULL, NULL);
		qmi_message_loc_set_operation_mode_input_unref(in);
		return;
	}
	xtra_or_session();
}

/* ---- assistance (2026-11-06): Android gets a fix at the desk with XTRA orbit data; we start cold and indoors see
 * at most one weak satellite in 10 min. Env options: DUO_GPS_SOURCES=1 (print the modem's XTRA server URLs, exit),
 * DUO_GPS_STANDALONE=1 (operation mode standalone), DUO_GPS_XTRA=<file> (inject it before the session). LOC answers
 * arrive as INDICATIONS, so each step waits for its indication. */
static void begin_session(void);
static GBytes *xtra; static guint xtra_parts, xtra_part;
#define XTRA_PART 1024

static void on_sources(QmiClientLoc *c, QmiIndicationLocGetPredictedOrbitsDataSourceOutput *o, gpointer d)
{
	GArray *servers = NULL; guint32 maxpart = 0, maxsize = 0;
	qmi_indication_loc_get_predicted_orbits_data_source_output_get_allowed_sizes(o, &maxsize, &maxpart, NULL);
	printf("xtra: max file %u bytes, max part %u bytes\n", maxsize, maxpart);
	if (qmi_indication_loc_get_predicted_orbits_data_source_output_get_server_list(o, &servers, NULL) && servers)
		for (guint i = 0; i < servers->len; i++)
			printf("xtra server: %s\n", g_array_index(servers, gchar *, i));
	g_main_loop_quit(loop);
}

static void inject_next(void);
static void injected_resp(GObject *o, GAsyncResult *r, gpointer d);
/* Inject XTRA Data (the newer LOC message): this modem answers the older Inject Predicted Orbits Data with
 * 'NotSupported' (2026-11-06) */
static void on_injected(QmiClientLoc *c, QmiIndicationLocInjectXtraDataOutput *o, gpointer d)
{
	QmiLocIndicationStatus st = 0;
	qmi_indication_loc_inject_xtra_data_output_get_indication_status(o, &st, NULL);
	if (st != QMI_LOC_INDICATION_STATUS_SUCCESS) {
		printf("xtra: part %u rejected (status %d)\n", xtra_part, st); begin_session(); return;
	}
	if (++xtra_part >= xtra_parts) { printf("xtra: %u parts injected\n", xtra_parts); begin_session(); return; }
	inject_next();
}

static void inject_next(void)
{
	gsize n; const guint8 *p = g_bytes_get_data(xtra, &n);
	gsize off = (gsize)xtra_part * XTRA_PART, len = MIN(XTRA_PART, n - off);
	GArray *a = g_array_sized_new(FALSE, FALSE, 1, len); g_array_append_vals(a, p + off, len);
	QmiMessageLocInjectXtraDataInput *in = qmi_message_loc_inject_xtra_data_input_new();
	qmi_message_loc_inject_xtra_data_input_set_total_size(in, n, NULL);
	qmi_message_loc_inject_xtra_data_input_set_total_parts(in, xtra_parts, NULL);
	qmi_message_loc_inject_xtra_data_input_set_part_number(in, xtra_part + 1, NULL);
	qmi_message_loc_inject_xtra_data_input_set_part_data(in, a, NULL);
	qmi_client_loc_inject_xtra_data(loc, in, 10, NULL, injected_resp, NULL);
	qmi_message_loc_inject_xtra_data_input_unref(in); g_array_unref(a);
}

/* the immediate response (the indication follows separately); a rejected request has no indication -> report it
 * and go on with the session instead of hanging (2026-11-06: the first run waited forever after part 1) */
static void injected_resp(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QmiMessageLocInjectXtraDataOutput *out = qmi_client_loc_inject_xtra_data_finish(QMI_CLIENT_LOC(o), r, &e);
	if (!out || !qmi_message_loc_inject_xtra_data_output_get_result(out, &e)) {
		printf("xtra: part %u request failed: %s\n", xtra_part + 1, e ? e->message : "?");
		begin_session();
	}
	if (out) qmi_message_loc_inject_xtra_data_output_unref(out);
}

static void on_opmode(QmiClientLoc *c, QmiIndicationLocSetOperationModeOutput *o, gpointer d)
{
	QmiLocIndicationStatus st = 0;
	qmi_indication_loc_set_operation_mode_output_get_indication_status(o, &st, NULL);
	printf("operation mode standalone: %s\n", st == QMI_LOC_INDICATION_STATUS_SUCCESS ? "ok" : "failed");
	xtra_or_session();
}

/* LOC Inject UTC Time (0x0038; TLV 0x01 = guint64 ms since 1970, TLV 0x02 = guint32 uncertainty ms). libqmi has no
 * wrapper -> a raw message. The XTRA file's last part was rejected (general failure) without a time reference
 * (2026-11-06): the engine cannot check the data's validity window. System clock = chrony over Wi-Fi. */
static gboolean xtra_later(gpointer d);
static void time_sent(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL; QmiMessage *resp = qmi_device_command_full_finish(dev, r, &e);
	printf("utc time injection: %s\n", resp ? "sent" : (e ? e->message : "failed"));
	if (resp) qmi_message_unref(resp);
	unsetenv("DUO_GPS_TIME");           /* time first, THEN the XTRA file (the chain continues here) */
	g_timeout_add(1000, xtra_later, NULL);
}
static gboolean xtra_later(gpointer d) { xtra_or_session(); return G_SOURCE_REMOVE; }
/* raw LOC indications we have no libqmi signal for (the UTC time result): print id + status TLV 0x01 */
static void on_raw_ind(QmiDevice *d, QmiMessage *m, gpointer u)
{
	if (qmi_message_get_service(m) != QMI_SERVICE_LOC || qmi_message_get_message_id(m) != 0x0038)
		return;
	gsize off = 0; guint32 st = 99; GError *e = NULL;
	gsize len = qmi_message_tlv_read_init(m, 0x01, NULL, &e);
	if (len) qmi_message_tlv_read_guint32(m, len, &off, QMI_ENDIAN_LITTLE, &st, &e);
	printf("utc time indication: status %u (%s)\n", st, st == 0 ? "success" : "FAILED");
}

/* LOC Inform Client Revision = QMI_LOC_INFORM_CLIENT_REVISION_REQ_V02 0x0020 (TLV 0x01 guint32 = the client's IDL
 * minor version; the Android HAL sends LOC_V02_IDL_MINOR_VERS). Some firmwares refuse injections from a client that
 * never announced its revision. FIX 2026-11-06: this used to send 0x0021 = QMI_LOC_REG_EVENTS_REQ_V02 (AOSP
 * location_service_v02.h), i.e. it re-registered the event mask as 0x2 instead - the earlier REV test proved nothing.
 * DUO_GPS_REV=<n> sets the revision (DUO_GPS_REV=1 -> 0x50, a recent IDL minor). */
static void inform_revision(void)
{
	GError *e = NULL; gsize off;
	guint32 rev = (guint32)strtoul(getenv("DUO_GPS_REV"), NULL, 0);
	if (rev <= 1) rev = 0x50;
	QmiMessage *m = qmi_message_new(QMI_SERVICE_LOC, qmi_client_get_cid(QMI_CLIENT(loc)),
					qmi_client_get_next_transaction_id(QMI_CLIENT(loc)), 0x0020);
	if ((off = qmi_message_tlv_write_init(m, 0x01, &e)) && qmi_message_tlv_write_guint32(m, QMI_ENDIAN_LITTLE, rev, &e) &&
	    qmi_message_tlv_write_complete(m, off, &e))
		qmi_device_command_full(dev, m, NULL, 10, NULL, NULL, NULL);
	qmi_message_unref(m);
	printf("client revision 0x%x announced (0x0020)\n", rev);
}

static void inject_time(void)
{
	static gboolean hooked;
	if (!hooked) { g_signal_connect(dev, "indication", G_CALLBACK(on_raw_ind), NULL); hooked = TRUE; }
	if (getenv("DUO_GPS_REV")) inform_revision();
	GError *e = NULL; gsize off;
	guint64 ms = (guint64)(g_get_real_time() / 1000);
	QmiMessage *m = qmi_message_new(QMI_SERVICE_LOC, qmi_client_get_cid(QMI_CLIENT(loc)),
					qmi_client_get_next_transaction_id(QMI_CLIENT(loc)), 0x0038);
	if (!(off = qmi_message_tlv_write_init(m, 0x01, &e)) || !qmi_message_tlv_write_guint64(m, QMI_ENDIAN_LITTLE, ms, &e) ||
	    !qmi_message_tlv_write_complete(m, off, &e) ||
	    !(off = qmi_message_tlv_write_init(m, 0x02, &e)) || !qmi_message_tlv_write_guint32(m, QMI_ENDIAN_LITTLE, 1000, &e) ||
	    !qmi_message_tlv_write_complete(m, off, &e)) {
		printf("utc time: build failed: %s\n", e ? e->message : "?"); qmi_message_unref(m); return;
	}
	qmi_device_command_full(dev, m, NULL, 10, NULL, time_sent, NULL);
	qmi_message_unref(m);
}

static void xtra_or_session(void)
{
	const char *x = getenv("DUO_GPS_XTRA"); GError *e = NULL; gchar *buf; gsize n;
	if (getenv("DUO_GPS_TIME")) { inject_time(); return; }   /* time_sent() calls back here */
	if (x && g_file_get_contents(x, &buf, &n, &e)) {
		xtra = g_bytes_new_take(buf, n); xtra_parts = (n + XTRA_PART - 1) / XTRA_PART; xtra_part = 0;
		printf("xtra: injecting %s (%zu bytes, %u parts)\n", x, n, xtra_parts);
		inject_next(); return;
	}
	if (x) printf("xtra: cannot read %s: %s\n", x, e ? e->message : "?");
	begin_session();
}

static void begin_session(void)
{
	QmiMessageLocSetNmeaTypesInput *in = qmi_message_loc_set_nmea_types_input_new();
	qmi_message_loc_set_nmea_types_input_set_nmea_types(in, QMI_LOC_NMEA_TYPE_GGA | QMI_LOC_NMEA_TYPE_RMC |
		QMI_LOC_NMEA_TYPE_GSV | QMI_LOC_NMEA_TYPE_GSA | QMI_LOC_NMEA_TYPE_VTG, NULL);
	qmi_client_loc_set_nmea_types(loc, in, 10, NULL, nmea_set, NULL);
	qmi_message_loc_set_nmea_types_input_unref(in);
}

static void got_client(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QmiClient *c = qmi_device_allocate_client_finish(dev, r, &e);
	if (!c)
		die("allocate LOC client", e);
	loc = QMI_CLIENT_LOC(c);
	g_signal_connect(loc, "get-predicted-orbits-data-source", G_CALLBACK(on_sources), NULL);
	g_signal_connect(loc, "inject-xtra-data", G_CALLBACK(on_injected), NULL);
	g_signal_connect(loc, "set-operation-mode", G_CALLBACK(on_opmode), NULL);
	if (getenv("DUO_GPS_SOURCES")) {
		qmi_client_loc_get_predicted_orbits_data_source(loc, NULL, 10, NULL, NULL, NULL);
		return;
	}
	g_signal_connect(loc, "nmea", G_CALLBACK(on_nmea), NULL);
	g_signal_connect(loc, "gnss-sv-info", G_CALLBACK(on_sv), NULL);
	g_signal_connect(loc, "position-report", G_CALLBACK(on_pos), NULL);
	QmiMessageLocRegisterEventsInput *in = qmi_message_loc_register_events_input_new();
	qmi_message_loc_register_events_input_set_event_registration_mask(in,
		QMI_LOC_EVENT_REGISTRATION_FLAG_POSITION_REPORT | QMI_LOC_EVENT_REGISTRATION_FLAG_GNSS_SATELLITE_INFO |
		QMI_LOC_EVENT_REGISTRATION_FLAG_NMEA, NULL);
	qmi_client_loc_register_events(loc, in, 10, NULL, registered, NULL);
	qmi_message_loc_register_events_input_unref(in);
}

static void opened(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	if (!qmi_device_open_finish(dev, r, &e))
		die("open", e);
	qmi_device_allocate_client(dev, QMI_SERVICE_LOC, QMI_CID_NONE, 10, NULL, got_client, NULL);
}

static void got_dev(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	dev = qmi_device_new_from_node_finish(r, &e);
	if (!dev)
		die("device from node", e);
	qmi_device_open(dev, QMI_DEVICE_OPEN_FLAGS_NONE, 10, NULL, opened, NULL);
}

static void got_node(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QrtrNode *n = qrtr_bus_wait_for_node_finish(QRTR_BUS(o), r, &e);
	if (!n)
		die("qrtr node", e);
	qmi_device_new_from_node(n, NULL, got_dev, NULL);
}

static void got_bus(GObject *o, GAsyncResult *r, gpointer d)
{
	GError *e = NULL;
	QrtrBus *b = qrtr_bus_new_finish(r, &e);
	if (!b)
		die("qrtr bus", e);
	qrtr_bus_wait_for_node(b, node_id, 5000, NULL, got_node, NULL);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		secs = atoi(argv[1]);
	if (argc > 2)
		node_id = atoi(argv[2]);
	setvbuf(stdout, NULL, _IOLBF, 0);
	loop = g_main_loop_new(NULL, FALSE);
	qrtr_bus_new(1000, NULL, got_bus, NULL);
	g_main_loop_run(loop);
	return 0;
}
