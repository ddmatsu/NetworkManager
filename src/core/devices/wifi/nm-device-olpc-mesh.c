/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Dan Williams <dcbw@redhat.com>
 * Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 * Daniel Drake <dsd@laptop.org>
 * Copyright (C) 2005 - 2014 Red Hat, Inc.
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2009 One Laptop per Child
 */

#include "src/core/nm-default-daemon.h"

#include "nm-device-olpc-mesh.h"

#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "devices/nm-device.h"
#include "nm-device-wifi.h"
#include "devices/nm-device-private.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"
#include "nm-act-request.h"
#include "nm-setting-connection.h"
#include "nm-setting-olpc-mesh.h"
#include "nm-manager.h"
#include "libnm-core-aux-intern/nm-libnm-core-utils.h"
#include "libnm-platform/nm-platform.h"

#define _NMLOG_DEVICE_TYPE NMDeviceOlpcMesh
#include "devices/nm-device-logging.h"

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE(NMDeviceOlpcMesh, PROP_COMPANION, PROP_ACTIVE_CHANNEL, );

typedef struct {
    NMDevice  *companion;
    NMManager *manager;
    bool       stage1_waiting : 1;
} NMDeviceOlpcMeshPrivate;

struct _NMDeviceOlpcMesh {
    NMDevice                parent;
    NMDeviceOlpcMeshPrivate _priv;
};

struct _NMDeviceOlpcMeshClass {
    NMDeviceClass parent;
};

G_DEFINE_TYPE(NMDeviceOlpcMesh, nm_device_olpc_mesh, NM_TYPE_DEVICE)

#define NM_DEVICE_OLPC_MESH_GET_PRIVATE(self) \
    _NM_GET_PRIVATE(self, NMDeviceOlpcMesh, NM_IS_DEVICE_OLPC_MESH, NMDevice)

/*****************************************************************************/

static gboolean
get_autoconnect_allowed(NMDevice *device)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(device);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    /* We can't even connect if we don't have a companion yet. */
    if (!priv->companion)
        return FALSE;

    /* We must not attempt to autoconnect when the companion is connected or
     * connecting, * because we'd tear down its connection. */
    if (nm_device_get_state(priv->companion) > NM_DEVICE_STATE_DISCONNECTED)
        return FALSE;

    return TRUE;
}

#define DEFAULT_SSID "olpc-mesh"

static gboolean
complete_connection(NMDevice            *device,
                    NMConnection        *connection,
                    const char          *specific_object,
                    NMConnection *const *existing_connections,
                    GError             **error)
{
    NMSettingOlpcMesh *s_mesh;

    s_mesh = _nm_connection_ensure_setting(connection, NM_TYPE_SETTING_OLPC_MESH);

    if (!nm_setting_olpc_mesh_get_ssid(s_mesh)) {
        gs_unref_bytes GBytes *ssid = NULL;

        ssid = g_bytes_new_static(DEFAULT_SSID, NM_STRLEN(DEFAULT_SSID));
        g_object_set(G_OBJECT(s_mesh), NM_SETTING_OLPC_MESH_SSID, ssid, NULL);
    }

    if (!nm_setting_olpc_mesh_get_dhcp_anycast_address(s_mesh)) {
        const char *anycast = "c0:27:c0:27:c0:27";

        g_object_set(G_OBJECT(s_mesh), NM_SETTING_OLPC_MESH_DHCP_ANYCAST_ADDRESS, anycast, NULL);
    }

    nm_utils_complete_generic(nm_device_get_platform(device),
                              connection,
                              NM_SETTING_OLPC_MESH_SETTING_NAME,
                              existing_connections,
                              NULL,
                              _("Mesh"),
                              NULL,
                              NULL);

    return TRUE;
}

/*****************************************************************************/

static const char *
get_dhcp_anycast_address(NMDevice *device)
{
    NMSettingOlpcMesh *s_mesh;

    s_mesh = nm_device_get_applied_setting(device, NM_TYPE_SETTING_OLPC_MESH);
    return s_mesh ? nm_setting_olpc_mesh_get_dhcp_anycast_address(s_mesh) : NULL;
}

/*****************************************************************************/

static NMActStageReturn
act_stage1_prepare(NMDevice *device, NMDeviceStateReason *out_failure_reason)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(device);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    /* disconnect companion device, if it is connected */
    if (nm_device_get_act_request(NM_DEVICE(priv->companion))) {
        _LOGI(LOGD_OLPC, "disconnecting companion device %s", nm_device_get_iface(priv->companion));
        /* FIXME: VPN stuff here is a bug; but we can't really change API now... */
        nm_device_state_changed(NM_DEVICE(priv->companion),
                                NM_DEVICE_STATE_DISCONNECTED,
                                NM_DEVICE_STATE_REASON_USER_REQUESTED);
        _LOGI(LOGD_OLPC, "companion %s disconnected", nm_device_get_iface(priv->companion));
    }

    /* wait with continuing configuration until the companion device is done scanning */
    if (nm_device_wifi_get_scanning(NM_DEVICE_WIFI(priv->companion))) {
        priv->stage1_waiting = TRUE;
        return NM_ACT_STAGE_RETURN_POSTPONE;
    }

    priv->stage1_waiting = FALSE;
    return NM_ACT_STAGE_RETURN_SUCCESS;
}

static gboolean
_mesh_set_channel(NMDeviceOlpcMesh *self, guint32 channel)
{
    NMPlatform *platform;
    int         ifindex = nm_device_get_ifindex(NM_DEVICE(self));
    guint32     old_channel;

    platform    = nm_device_get_platform(NM_DEVICE(self));
    old_channel = nm_platform_mesh_get_channel(platform, ifindex);

    if (channel == 0)
        channel = old_channel;

    /* We want to call this even if the channel number is the same,
     * because that actually starts the mesh with the configured mesh ID. */
    if (!nm_platform_mesh_set_channel(platform, ifindex, channel))
        return FALSE;

    if (old_channel != channel)
        _notify(self, PROP_ACTIVE_CHANNEL);

    return TRUE;
}

static NMActStageReturn
act_stage2_config(NMDevice *device, NMDeviceStateReason *out_failure_reason)
{
    NMDeviceOlpcMesh  *self = NM_DEVICE_OLPC_MESH(device);
    NMSettingOlpcMesh *s_mesh;
    GBytes            *ssid;
    gboolean           success;

    s_mesh = nm_device_get_applied_setting(device, NM_TYPE_SETTING_OLPC_MESH);
    g_return_val_if_fail(s_mesh, NM_ACT_STAGE_RETURN_FAILURE);

    ssid = nm_setting_olpc_mesh_get_ssid(s_mesh);

    nm_device_take_down(NM_DEVICE(self), TRUE);
    success = nm_platform_mesh_set_ssid(nm_device_get_platform(device),
                                        nm_device_get_ifindex(device),
                                        g_bytes_get_data(ssid, NULL),
                                        g_bytes_get_size(ssid));
    nm_device_bring_up(NM_DEVICE(self));
    if (!success) {
        _LOGW(LOGD_WIFI, "Unable to set the mesh ID");
        return NM_ACT_STAGE_RETURN_FAILURE;
    }

    if (!_mesh_set_channel(self, nm_setting_olpc_mesh_get_channel(s_mesh))) {
        _LOGW(LOGD_WIFI, "Unable to set the mesh channel");
        return NM_ACT_STAGE_RETURN_FAILURE;
    }

    return NM_ACT_STAGE_RETURN_SUCCESS;
}

static gboolean
is_available(NMDevice *device, NMDeviceCheckDevAvailableFlags flags)
{
    NMDeviceOlpcMesh *self = NM_DEVICE_OLPC_MESH(device);

    if (!NM_DEVICE_OLPC_MESH_GET_PRIVATE(self)->companion) {
        _LOGD(LOGD_WIFI, "not available because companion not found");
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

static void
companion_cleanup(NMDeviceOlpcMesh *self)
{
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    if (priv->companion) {
        nm_device_wifi_scanning_prohibited_track(NM_DEVICE_WIFI(priv->companion), self, FALSE);
        g_signal_handlers_disconnect_by_data(priv->companion, self);
        g_clear_object(&priv->companion);
    }
    _notify(self, PROP_COMPANION);
}

static void
companion_notify_cb(NMDeviceWifi *companion, GParamSpec *pspec, gpointer user_data)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(user_data);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    nm_assert(NM_IS_DEVICE_WIFI(companion));
    nm_assert(priv->companion == (gpointer) companion);

    if (!priv->stage1_waiting)
        return;

    if (!nm_device_wifi_get_scanning(NM_DEVICE_WIFI(companion))) {
        priv->stage1_waiting = FALSE;
        nm_device_activate_schedule_stage1_device_prepare(NM_DEVICE(self), FALSE);
    }
}

/* disconnect from mesh if someone starts using the companion */
static void
companion_state_changed_cb(NMDeviceWifi       *companion,
                           NMDeviceState       state,
                           NMDeviceState       old_state,
                           NMDeviceStateReason reason,
                           gpointer            user_data)
{
    NMDeviceOlpcMesh *self       = NM_DEVICE_OLPC_MESH(user_data);
    NMDeviceState     self_state = nm_device_get_state(NM_DEVICE(self));

    if (old_state > NM_DEVICE_STATE_DISCONNECTED && state <= NM_DEVICE_STATE_DISCONNECTED) {
        nm_device_recheck_auto_activate_schedule(NM_DEVICE(self));
    }

    if (self_state < NM_DEVICE_STATE_PREPARE || self_state > NM_DEVICE_STATE_ACTIVATED
        || state < NM_DEVICE_STATE_PREPARE || state > NM_DEVICE_STATE_ACTIVATED)
        return;

    _LOGD(LOGD_OLPC, "disconnecting mesh due to companion connectivity");
    /* FIXME: VPN stuff here is a bug; but we can't really change API now... */
    nm_device_state_changed(NM_DEVICE(self),
                            NM_DEVICE_STATE_DISCONNECTED,
                            NM_DEVICE_STATE_REASON_USER_REQUESTED);
}

static gboolean
companion_autoconnect_allowed_cb(NMDeviceWifi *companion, gpointer user_data)
{
    NMDeviceOlpcMesh *self  = NM_DEVICE_OLPC_MESH(user_data);
    NMDeviceState     state = nm_device_get_state(NM_DEVICE(self));

    /* Don't allow the companion to autoconnect while a mesh connection is
     * active */
    return (state < NM_DEVICE_STATE_PREPARE) || (state > NM_DEVICE_STATE_ACTIVATED);
}

static gboolean
check_companion(NMDeviceOlpcMesh *self, NMDevice *other)
{
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);
    const char              *my_addr, *their_addr;

    if (!NM_IS_DEVICE_WIFI(other))
        return FALSE;

    my_addr    = nm_device_get_hw_address(NM_DEVICE(self));
    their_addr = nm_device_get_hw_address(other);
    if (!nm_utils_hwaddr_matches(my_addr, -1, their_addr, -1))
        return FALSE;

    nm_assert(priv->companion == NULL);
    priv->companion = g_object_ref(other);

    _LOGI(LOGD_OLPC, "found companion Wi-Fi device %s", nm_device_get_iface(other));

    g_signal_connect(G_OBJECT(other),
                     NM_DEVICE_STATE_CHANGED,
                     G_CALLBACK(companion_state_changed_cb),
                     self);

    g_signal_connect(G_OBJECT(other),
                     "notify::" NM_DEVICE_WIFI_SCANNING,
                     G_CALLBACK(companion_notify_cb),
                     self);

    g_signal_connect(G_OBJECT(other),
                     NM_DEVICE_AUTOCONNECT_ALLOWED,
                     G_CALLBACK(companion_autoconnect_allowed_cb),
                     self);

    _notify(self, PROP_COMPANION);

    return TRUE;
}

static void
device_added_cb(NMManager *manager, NMDevice *other, gpointer user_data)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(user_data);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    if (!priv->companion && check_companion(self, other)) {
        nm_device_queue_recheck_available(NM_DEVICE(self),
                                          NM_DEVICE_STATE_REASON_NONE,
                                          NM_DEVICE_STATE_REASON_NONE);
        nm_device_remove_pending_action(NM_DEVICE(self),
                                        NM_PENDING_ACTION_WAITING_FOR_COMPANION,
                                        FALSE);
    }
}

static void
device_removed_cb(NMManager *manager, NMDevice *other, gpointer user_data)
{
    NMDeviceOlpcMesh *self = NM_DEVICE_OLPC_MESH(user_data);

    if (other == NM_DEVICE_OLPC_MESH_GET_PRIVATE(self)->companion)
        companion_cleanup(self);
}

static void
find_companion(NMDeviceOlpcMesh *self)
{
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);
    const CList             *tmp_lst;
    NMDevice                *candidate;

    if (priv->companion)
        return;

    nm_device_add_pending_action(NM_DEVICE(self), NM_PENDING_ACTION_WAITING_FOR_COMPANION, TRUE);

    /* Try to find the companion if it's already known to the NMManager */
    nm_manager_for_each_device (priv->manager, candidate, tmp_lst) {
        if (check_companion(self, candidate)) {
            nm_device_queue_recheck_available(NM_DEVICE(self),
                                              NM_DEVICE_STATE_REASON_NONE,
                                              NM_DEVICE_STATE_REASON_NONE);
            nm_device_remove_pending_action(NM_DEVICE(self),
                                            NM_PENDING_ACTION_WAITING_FOR_COMPANION,
                                            TRUE);
            break;
        }
    }
}

static void
state_changed(NMDevice           *device,
              NMDeviceState       new_state,
              NMDeviceState       old_state,
              NMDeviceStateReason reason)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(device);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    if (new_state == NM_DEVICE_STATE_UNAVAILABLE)
        find_companion(self);

    if (priv->companion) {
        gboolean temporarily_prohibited = FALSE;

        if (new_state >= NM_DEVICE_STATE_PREPARE && new_state <= NM_DEVICE_STATE_IP_CONFIG) {
            /* Don't allow the companion to scan while configuring the mesh interface */
            temporarily_prohibited = TRUE;
        }
        nm_device_wifi_scanning_prohibited_track(NM_DEVICE_WIFI(priv->companion),
                                                 self,
                                                 temporarily_prohibited);
    }
}

static guint32
get_dhcp_timeout_for_device(NMDevice *device, int addr_family)
{
    /* shorter timeout for mesh connectivity */
    return 20;
}

/*****************************************************************************/

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    NMDeviceOlpcMesh        *self   = NM_DEVICE_OLPC_MESH(object);
    NMDevice                *device = NM_DEVICE(self);
    NMDeviceOlpcMeshPrivate *priv   = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    switch (prop_id) {
    case PROP_COMPANION:
        nm_dbus_utils_g_value_set_object_path(value, priv->companion);
        break;
    case PROP_ACTIVE_CHANNEL:
        g_value_set_uint(value,
                         nm_platform_mesh_get_channel(nm_device_get_platform(device),
                                                      nm_device_get_ifindex(device)));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/*****************************************************************************/

static void
nm_device_olpc_mesh_init(NMDeviceOlpcMesh *self)
{}

static void
constructed(GObject *object)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(object);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    G_OBJECT_CLASS(nm_device_olpc_mesh_parent_class)->constructed(object);

    priv->manager = g_object_ref(NM_MANAGER_GET);

    g_signal_connect(priv->manager, NM_MANAGER_DEVICE_ADDED, G_CALLBACK(device_added_cb), self);
    g_signal_connect(priv->manager, NM_MANAGER_DEVICE_REMOVED, G_CALLBACK(device_removed_cb), self);
}

NMDevice *
nm_device_olpc_mesh_new(const char *iface)
{
    return g_object_new(NM_TYPE_DEVICE_OLPC_MESH,
                        NM_DEVICE_IFACE,
                        iface,
                        NM_DEVICE_TYPE_DESC,
                        "802.11 OLPC Mesh",
                        NM_DEVICE_DEVICE_TYPE,
                        NM_DEVICE_TYPE_OLPC_MESH,
                        NM_DEVICE_LINK_TYPE,
                        NM_LINK_TYPE_OLPC_MESH,
                        NULL);
}

static void
dispose(GObject *object)
{
    NMDeviceOlpcMesh        *self = NM_DEVICE_OLPC_MESH(object);
    NMDeviceOlpcMeshPrivate *priv = NM_DEVICE_OLPC_MESH_GET_PRIVATE(self);

    companion_cleanup(self);

    if (priv->manager) {
        g_signal_handlers_disconnect_by_func(priv->manager, G_CALLBACK(device_added_cb), self);
        g_signal_handlers_disconnect_by_func(priv->manager, G_CALLBACK(device_removed_cb), self);
        g_clear_object(&priv->manager);
    }

    G_OBJECT_CLASS(nm_device_olpc_mesh_parent_class)->dispose(object);
}

static const NMDBusInterfaceInfoExtended interface_info_device_olpc_mesh = {
    .parent = NM_DEFINE_GDBUS_INTERFACE_INFO_INIT(
        NM_DBUS_INTERFACE_DEVICE_OLPC_MESH,
        .properties = NM_DEFINE_GDBUS_PROPERTY_INFOS(
            NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE(
                "HwAddress",
                "s",
                NM_DEVICE_HW_ADDRESS,
                .annotations = NM_GDBUS_ANNOTATION_INFO_LIST_DEPRECATED(), ),
            NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE("Companion",
                                                           "o",
                                                           NM_DEVICE_OLPC_MESH_COMPANION),
            NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE(
                "ActiveChannel",
                "u",
                NM_DEVICE_OLPC_MESH_ACTIVE_CHANNEL), ), ),
};

static void
nm_device_olpc_mesh_class_init(NMDeviceOlpcMeshClass *klass)
{
    GObjectClass      *object_class      = G_OBJECT_CLASS(klass);
    NMDBusObjectClass *dbus_object_class = NM_DBUS_OBJECT_CLASS(klass);
    NMDeviceClass     *device_class      = NM_DEVICE_CLASS(klass);

    object_class->constructed  = constructed;
    object_class->get_property = get_property;
    object_class->dispose      = dispose;

    dbus_object_class->interface_infos = NM_DBUS_INTERFACE_INFOS(&interface_info_device_olpc_mesh);

    device_class->connection_type_supported        = NM_SETTING_OLPC_MESH_SETTING_NAME;
    device_class->connection_type_check_compatible = NM_SETTING_OLPC_MESH_SETTING_NAME;
    device_class->link_types = NM_DEVICE_DEFINE_LINK_TYPES(NM_LINK_TYPE_OLPC_MESH);

    device_class->get_autoconnect_allowed     = get_autoconnect_allowed;
    device_class->complete_connection         = complete_connection;
    device_class->is_available                = is_available;
    device_class->act_stage1_prepare          = act_stage1_prepare;
    device_class->act_stage2_config           = act_stage2_config;
    device_class->state_changed               = state_changed;
    device_class->get_dhcp_timeout_for_device = get_dhcp_timeout_for_device;
    device_class->get_dhcp_anycast_address    = get_dhcp_anycast_address;

    obj_properties[PROP_COMPANION] = g_param_spec_string(NM_DEVICE_OLPC_MESH_COMPANION,
                                                         "",
                                                         "",
                                                         NULL,
                                                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ACTIVE_CHANNEL] =
        g_param_spec_uint(NM_DEVICE_OLPC_MESH_ACTIVE_CHANNEL,
                          "",
                          "",
                          0,
                          G_MAXUINT32,
                          0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);
}
