
#include "nm-device-interface.h"
#include "nm-ip4-config.h"

static gboolean impl_device_activate (NMDeviceInterface *device,
									  GHashTable *connection_hash,
									  GError **err);

static gboolean impl_device_deactivate (NMDeviceInterface *device, GError **err);

#include "nm-device-interface-glue.h"

static void
nm_device_interface_init (gpointer g_iface)
{
	GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
	static gboolean initialized = FALSE;

	if (initialized)
		return;

	/* Properties */
	g_object_interface_install_property
		(g_iface,
		 g_param_spec_string (NM_DEVICE_INTERFACE_UDI,
							  "Udi",
							  "HAL Udi",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_string (NM_DEVICE_INTERFACE_IFACE,
							  "Interface",
							  "Interface",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_string (NM_DEVICE_INTERFACE_DRIVER,
							  "Driver",
							  "Driver",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	g_object_interface_install_property
		(g_iface,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_CAPABILITIES,
							"Capabilities",
							"Capabilities",
							0, G_MAXUINT32, NM_DEVICE_CAP_NONE,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_pointer (NM_DEVICE_INTERFACE_APP_DATA,
							   "AppData",
							   "AppData",
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_IP4_ADDRESS,
							"IP4 address",
							"IP4 address",
							0, G_MAXUINT32, 0, /* FIXME */
							G_PARAM_READWRITE));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_object (NM_DEVICE_INTERFACE_IP4_CONFIG,
							  "IP4 Config",
							  "IP4 Config",
							  G_TYPE_OBJECT,
							  G_PARAM_READWRITE));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_STATE,
							"State",
							"State",
							0, G_MAXUINT32, NM_DEVICE_STATE_UNKNOWN,
							G_PARAM_READABLE));

	g_object_interface_install_property
		(g_iface,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_DEVICE_TYPE,
							"DeviceType",
							"DeviceType",
							0, G_MAXUINT32, DEVICE_TYPE_UNKNOWN,
							G_PARAM_READABLE));

	/* Signals */
	g_signal_new ("state-changed",
				  iface_type,
				  G_SIGNAL_RUN_FIRST,
				  G_STRUCT_OFFSET (NMDeviceInterface, state_changed),
				  NULL, NULL,
				  g_cclosure_marshal_VOID__UINT,
				  G_TYPE_NONE, 1,
				  G_TYPE_UINT);

	g_signal_new ("carrier-changed",
				  iface_type,
				  G_SIGNAL_RUN_FIRST,
				  G_STRUCT_OFFSET (NMDeviceInterface, carrier_changed),
				  NULL, NULL,
				  g_cclosure_marshal_VOID__BOOLEAN,
				  G_TYPE_NONE, 1,
				  G_TYPE_BOOLEAN);

	dbus_g_object_type_install_info (iface_type,
									 &dbus_glib_nm_device_interface_object_info);

	initialized = TRUE;
}


GType
nm_device_interface_get_type (void)
{
	static GType device_interface_type = 0;

	if (!device_interface_type) {
		const GTypeInfo device_interface_info = {
			sizeof (NMDeviceInterface), /* class_size */
			nm_device_interface_init,   /* base_init */
			NULL,		/* base_finalize */
			NULL,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			0,
			0,              /* n_preallocs */
			NULL
		};

		device_interface_type = g_type_register_static (G_TYPE_INTERFACE,
														"NMDeviceInterface",
														&device_interface_info, 0);

		g_type_interface_add_prerequisite (device_interface_type, G_TYPE_OBJECT);
	}

	return device_interface_type;
}

void
nm_device_interface_activate (NMDeviceInterface *device,
							  NMConnection *connection,
							  gboolean user_requested)
{
	g_return_if_fail (NM_IS_DEVICE_INTERFACE (device));
	g_return_if_fail (connection != NULL);

	NM_DEVICE_INTERFACE_GET_INTERFACE (device)->activate (device, connection, user_requested);
}

static gboolean
impl_device_activate (NMDeviceInterface *device,
					  GHashTable *connection_hash,
					  GError **err)
{
	NMConnection *connection;

	connection = nm_connection_new_from_hash (connection_hash);
	nm_connection_dump (connection);

	nm_device_interface_activate (device, connection, TRUE);

	return TRUE;
}

void
nm_device_interface_deactivate (NMDeviceInterface *device)
{
	g_return_if_fail (NM_IS_DEVICE_INTERFACE (device));

	NM_DEVICE_INTERFACE_GET_INTERFACE (device)->deactivate (device);
}

static gboolean
impl_device_deactivate (NMDeviceInterface *device, GError **err)
{
	g_return_val_if_fail (NM_IS_DEVICE_INTERFACE (device), FALSE);

	nm_device_interface_deactivate (device);

	return TRUE;
}
