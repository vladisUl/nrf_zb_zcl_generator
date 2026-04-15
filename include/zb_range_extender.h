#ifndef __ZB_RANGE_EXTENDER_H__
#define __ZB_RANGE_EXTENDER_H__

#define ZB_ENV_SENSOR_DEVICE_ID 0x000C

#define ZB_DEVICE_VER_ENV_SENSOR 0

// number of IN (server) clusters
#define ZB_NORDIC_DIY_TEMPLATE_IN_CLUSTER_NUM 4

// number of OUT (client) clusters
#define ZB_NORDIC_DIY_TEMPLATE_OUT_CLUSTER_NUM 1

// total number of (IN+OUT) clusters
#define ZB_NORDIC_DIY_TEMPLATE_CLUSTER_NUM \
	(ZB_NORDIC_DIY_TEMPLATE_IN_CLUSTER_NUM + ZB_NORDIC_DIY_TEMPLATE_OUT_CLUSTER_NUM)

// Number of attributes for reporting
#define ZB_NORDIC_DIY_TEMPLATE_REPORT_ATTR_COUNT 2

#define ZB_DECLARE_NORDIC_DIY_TEMPLATE_CLUSTER_LIST( \
	cluster_list_name, \
	basic_server_attr_list, \
	identify_server_attr_list, \
	power_config_server_attr_list, \
	humidity_measurement_attr_list, \
	identify_client_attr_list) \
	zb_zcl_cluster_desc_t cluster_list_name[] = \
		{ \
			ZB_ZCL_CLUSTER_DESC( \
				ZB_ZCL_CLUSTER_ID_BASIC, \
				ZB_ZCL_ARRAY_SIZE(basic_server_attr_list, zb_zcl_attr_t), \
				(basic_server_attr_list), \
				ZB_ZCL_CLUSTER_SERVER_ROLE, \
				ZB_ZCL_MANUF_CODE_INVALID), \
			ZB_ZCL_CLUSTER_DESC( \
				ZB_ZCL_CLUSTER_ID_IDENTIFY, \
				ZB_ZCL_ARRAY_SIZE(identify_server_attr_list, zb_zcl_attr_t), \
				(identify_server_attr_list), \
				ZB_ZCL_CLUSTER_SERVER_ROLE, \
				ZB_ZCL_MANUF_CODE_INVALID), \
			ZB_ZCL_CLUSTER_DESC( \
				ZB_ZCL_CLUSTER_ID_POWER_CONFIG, \
				ZB_ZCL_ARRAY_SIZE(power_config_server_attr_list, zb_zcl_attr_t), \
				(power_config_server_attr_list), \
				ZB_ZCL_CLUSTER_SERVER_ROLE, \
				ZB_ZCL_MANUF_CODE_INVALID), \
			ZB_ZCL_CLUSTER_DESC( \
				ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, \
				ZB_ZCL_ARRAY_SIZE(humidity_measurement_attr_list, zb_zcl_attr_t), \
				(humidity_measurement_attr_list), \
				ZB_ZCL_CLUSTER_SERVER_ROLE, \
				ZB_ZCL_MANUF_CODE_INVALID), \
			ZB_ZCL_CLUSTER_DESC( \
				ZB_ZCL_CLUSTER_ID_IDENTIFY, \
				ZB_ZCL_ARRAY_SIZE(identify_client_attr_list, zb_zcl_attr_t), \
				(identify_client_attr_list), \
				ZB_ZCL_CLUSTER_CLIENT_ROLE, \
				ZB_ZCL_MANUF_CODE_INVALID) \
		}

#define ZB_ZCL_DECLARE_NORDIC_DIY_TEMPLATE_SIMPLE_DESC( \
	ep_name, ep_id, in_clust_num, out_clust_num) \
	ZB_DECLARE_SIMPLE_DESC(in_clust_num, out_clust_num); \
	ZB_AF_SIMPLE_DESC_TYPE(in_clust_num, out_clust_num) \
	simple_desc_##ep_name = \
		{ \
			ep_id, \
			ZB_AF_HA_PROFILE_ID, \
			ZB_ENV_SENSOR_DEVICE_ID, \
			ZB_DEVICE_VER_ENV_SENSOR, \
			0, \
			in_clust_num, \
			out_clust_num, \
			{ZB_ZCL_CLUSTER_ID_BASIC, ZB_ZCL_CLUSTER_ID_IDENTIFY, ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ZB_ZCL_CLUSTER_ID_IDENTIFY}}

#define ZB_DECLARE_NORDIC_DIY_TEMPLATE_EP(ep_name, ep_id, cluster_list) \
	ZB_ZCL_DECLARE_NORDIC_DIY_TEMPLATE_SIMPLE_DESC(ep_name, ep_id, \
						  ZB_NORDIC_DIY_TEMPLATE_IN_CLUSTER_NUM, ZB_NORDIC_DIY_TEMPLATE_OUT_CLUSTER_NUM); \
	ZBOSS_DEVICE_DECLARE_REPORTING_CTX(reporting_info##ep_name, \
						   ZB_NORDIC_DIY_TEMPLATE_REPORT_ATTR_COUNT); \
	ZB_AF_DECLARE_ENDPOINT_DESC(ep_name, ep_id, ZB_AF_HA_PROFILE_ID, 0, NULL, \
								ZB_ZCL_ARRAY_SIZE(cluster_list, zb_zcl_cluster_desc_t), cluster_list, \
								(zb_af_simple_desc_1_1_t *)&simple_desc_##ep_name, \
								ZB_NORDIC_DIY_TEMPLATE_REPORT_ATTR_COUNT, reporting_info##ep_name, \
								0, NULL) // No CVC ctx

#endif // __ZB_RANGE_EXTENDER_H__
