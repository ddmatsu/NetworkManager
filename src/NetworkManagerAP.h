/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifndef NM_ACCESS_POINT_H
#define NM_ACCESS_POINT_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>
#include <time.h>
#include "NetworkManager.h"

#define NM_TYPE_AP            (nm_ap_get_type ())
#define NM_AP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_AP, NMAccessPoint))
#define NM_AP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_AP, NMAccessPointClass))
#define NM_IS_AP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_AP))
#define NM_IS_AP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_AP))
#define NM_AP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_AP, NMAccessPointClass))

#define NM_AP_FLAGS "flags"
#define NM_AP_WPA_FLAGS "wpa-flags"
#define NM_AP_RSN_FLAGS "rsn-flags"
#define NM_AP_SSID "ssid"
#define NM_AP_FREQUENCY "frequency"
#define NM_AP_HW_ADDRESS "hw-address"
#define NM_AP_MODE "mode"
#define NM_AP_RATE "rate"
#define NM_AP_STRENGTH "strength"

typedef struct {
	GObject parent;
} NMAccessPoint;

typedef struct {
	GObjectClass parent;

	/* Signals */
	void (*strength_changed) (NMAccessPoint *ap, gint8 strength);
} NMAccessPointClass;

GType nm_ap_get_type (void);

NMAccessPoint *	nm_ap_new				(void);
NMAccessPoint *	nm_ap_new_from_ap		(NMAccessPoint *ap);
NMAccessPoint * nm_ap_new_from_properties (GHashTable *properties);

const char *		nm_ap_get_dbus_path (NMAccessPoint *ap);
const GTimeVal *	nm_ap_get_timestamp				(const NMAccessPoint *ap);
void				nm_ap_set_timestamp				(NMAccessPoint *ap, glong sec, glong usec);
void				nm_ap_set_timestamp_via_timestamp	(NMAccessPoint *ap, const GTimeVal *timestamp);

const GByteArray *	nm_ap_get_ssid (const NMAccessPoint * ap);
void				nm_ap_set_ssid (NMAccessPoint * ap, const GByteArray * ssid);

guint32			nm_ap_get_flags	(NMAccessPoint *ap);
void				nm_ap_set_flags	(NMAccessPoint *ap, guint32 flags);

guint32			nm_ap_get_wpa_flags	(NMAccessPoint *ap);
void				nm_ap_set_wpa_flags	(NMAccessPoint *ap, guint32 flags);

guint32			nm_ap_get_rsn_flags	(NMAccessPoint *ap);
void				nm_ap_set_rsn_flags	(NMAccessPoint *ap, guint32 flags);

const struct ether_addr * nm_ap_get_address	(const NMAccessPoint *ap);
void				nm_ap_set_address		(NMAccessPoint *ap, const struct ether_addr *addr);

int				nm_ap_get_mode			(NMAccessPoint *ap);
void				nm_ap_set_mode			(NMAccessPoint *ap, const int mode);

gint8			nm_ap_get_strength		(NMAccessPoint *ap);
void				nm_ap_set_strength		(NMAccessPoint *ap, gint8 strength);

double			nm_ap_get_freq			(NMAccessPoint *ap);
void				nm_ap_set_freq			(NMAccessPoint *ap, double freq);

guint16			nm_ap_get_rate			(NMAccessPoint *ap);
void				nm_ap_set_rate			(NMAccessPoint *ap, guint16 rate);

gboolean			nm_ap_get_invalid		(const NMAccessPoint *ap);
void				nm_ap_set_invalid		(NMAccessPoint *ap, gboolean invalid);

gboolean			nm_ap_get_artificial	(const NMAccessPoint *ap);
void				nm_ap_set_artificial	(NMAccessPoint *ap, gboolean artificial);

gboolean			nm_ap_get_broadcast		(NMAccessPoint *ap);
void				nm_ap_set_broadcast		(NMAccessPoint *ap, gboolean broadcast);

glong			nm_ap_get_last_seen		(const NMAccessPoint *ap);
void				nm_ap_set_last_seen		(NMAccessPoint *ap, const glong last_seen);

gboolean			nm_ap_get_user_created	(const NMAccessPoint *ap);
void				nm_ap_set_user_created	(NMAccessPoint *ap, gboolean user_created);

GSList *			nm_ap_get_user_addresses	(const NMAccessPoint *ap);
void				nm_ap_set_user_addresses (NMAccessPoint *ap, GSList *list);

guint32				nm_ap_add_security_from_ie (guint32 flags,
                                                const guint8 *wpa_ie,
                                                guint32 length);

void				nm_ap_print_self (NMAccessPoint *ap, const char * prefix);

/* 
 * NOTE:
 * This is not intended to return true for all APs with manufacturer defaults.  It is intended to return true for
 * only the MOST COMMON manufacturing defaults.
 */
gboolean			nm_ap_has_manufacturer_default_ssid	(NMAccessPoint *ap);

#endif /* NM_ACCESS_POINT_H */
