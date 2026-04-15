#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import yaml  # type: ignore
except Exception:
    yaml = None

APP_DEFAULTS = {
    "erase_persistent_config": "ZB_FALSE",
    "read_data_initial_delay": "K_SECONDS(30)",
    "read_data_timer_period": "K_SECONDS(60)",
    "long_poll_interval": "600000",
}

FULLY_SUPPORTED_CLUSTERS = {
    "BASIC",
    "IDENTIFY",
    "POWER_CONFIG",
    "TEMP_MEASUREMENT",
    "REL_HUMIDITY_MEASUREMENT",
    "PRESSURE_MEASUREMENT",
    "POLL_CONTROL",
    "ON_OFF",
}

BACKSLASH = "\\"


@dataclass(frozen=True)
class SDKClusterInfo:
    short_name: str
    id_macro: str
    declare_macro: str | None = None
    declare_params: tuple[str, ...] = ()
    header: str | None = None


@dataclass(frozen=True)
class Cluster:
    name: str
    role: str
    sdk: SDKClusterInfo | None = None

    @property
    def role_macro(self) -> str:
        return "ZB_ZCL_CLUSTER_SERVER_ROLE" if self.role == "server" else "ZB_ZCL_CLUSTER_CLIENT_ROLE"

    @property
    def short_name(self) -> str:
        return self.name[18:] if self.name.startswith("ZB_ZCL_CLUSTER_ID_") else self.name

    @property
    def id_macro(self) -> str:
        return self.name if self.name.startswith("ZB_ZCL_CLUSTER_ID_") else f"ZB_ZCL_CLUSTER_ID_{self.name}"

    @property
    def attr_list_var(self) -> str:
        mapping = {
            ("BASIC", "server"): "basic_attr_list",
            ("IDENTIFY", "server"): "identify_server_attr_list",
            ("IDENTIFY", "client"): "identify_client_attr_list",
            ("POWER_CONFIG", "server"): "power_config_server_attr_list",
            ("TEMP_MEASUREMENT", "server"): "temp_measurement_attr_list",
            ("REL_HUMIDITY_MEASUREMENT", "server"): "humidity_measurement_attr_list",
            ("PRESSURE_MEASUREMENT", "server"): "pressure_measurement_attr_list",
            ("POLL_CONTROL", "server"): "poll_control_attrib_list",
            ("ON_OFF", "server"): "on_off_attr_list",
        }
        return mapping.get((self.short_name, self.role), f"{self.short_name.lower()}_attr_list")

    @property
    def macro_param_name(self) -> str:
        mapping = {
            ("BASIC", "server"): "basic_server_attr_list",
            ("IDENTIFY", "server"): "identify_server_attr_list",
            ("IDENTIFY", "client"): "identify_client_attr_list",
            ("POWER_CONFIG", "server"): "power_config_server_attr_list",
            ("TEMP_MEASUREMENT", "server"): "temp_measurement_attr_list",
            ("REL_HUMIDITY_MEASUREMENT", "server"): "humidity_measurement_attr_list",
            ("PRESSURE_MEASUREMENT", "server"): "pressure_measurement_attr_list",
            ("POLL_CONTROL", "server"): "poll_control_attr_list",
            ("ON_OFF", "server"): "on_off_attr_list",
        }
        return mapping.get((self.short_name, self.role), self.attr_list_var)

    @property
    def is_fully_supported(self) -> bool:
        return self.short_name in FULLY_SUPPORTED_CLUSTERS

    @property
    def has_sdk_declare_macro(self) -> bool:
        return bool(self.sdk and self.sdk.declare_macro)


@dataclass
class ProjectConfig:
    project_name: str
    project_upper: str
    sdk_zcl_path: str
    device_id: str
    device_version: str
    endpoint: int
    report_attr_count: int
    clusters: list[Cluster]
    init: dict[str, Any]
    sdk_clusters: dict[str, SDKClusterInfo]


def require_pyyaml() -> None:
    if yaml is None:
        raise SystemExit("PyYAML is not installed. Install it first: pip install pyyaml")


def sanitize_project_name(name: str) -> str:
    result = []
    for ch in name:
        result.append(ch if ch.isalnum() else "_")
    out = "".join(result).strip("_")
    if not out:
        raise ValueError("project.name is empty after normalization")
    return out


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def as_c_value(value: Any) -> str:
    if isinstance(value, str):
        stripped = value.strip()
        if len(stripped) >= 2 and stripped[0] == '"' and stripped[-1] == '"':
            return stripped
        if stripped.startswith(("ZB_", "K_")):
            return stripped
        return c_string(value)
    if isinstance(value, bool):
        return "ZB_TRUE" if value else "ZB_FALSE"
    return str(value)


def as_c_int_literal(value: Any) -> str:
    if isinstance(value, int):
        return f"0x{value:04X}"
    return str(value)


def normalize_cluster_name(name: str) -> str:
    raw = str(name).strip().upper()
    if raw.startswith("ZB_ZCL_CLUSTER_ID_"):
        return raw[len("ZB_ZCL_CLUSTER_ID_"):]
    return raw


def _parse_declare_macros(header_text: str) -> list[tuple[str, tuple[str, ...]]]:
    lines = header_text.splitlines()
    out: list[tuple[str, tuple[str, ...]]] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("#define ZB_ZCL_DECLARE_") and "_ATTRIB_LIST" in line:
            macro_lines = [line]
            while macro_lines[-1].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                macro_lines.append(lines[i])
            full = "\n".join(macro_lines)
            m = re.search(r'#define\s+(ZB_ZCL_DECLARE_[A-Z0-9_]+_ATTRIB_LIST(?:_[A-Z0-9_]+)?)\s*\((.*?)\)', full, re.S)
            if m:
                params = tuple(
                    p.strip()
                    for p in m.group(2).replace("\\", " ").replace("\n", " ").split(",")
                    if p.strip()
                )
                out.append((m.group(1), params))
        i += 1
    return out


def scan_sdk_clusters(zcl_path: str) -> dict[str, SDKClusterInfo]:
    zcl_dir = Path(zcl_path)
    common_h = zcl_dir / "zb_zcl_common.h"
    if not common_h.exists():
        return {}
    infos: dict[str, SDKClusterInfo] = {}
    common_text = common_h.read_text(encoding="utf-8", errors="ignore")
    for m in re.finditer(r'#define\s+(ZB_ZCL_CLUSTER_ID_[A-Z0-9_]+)\s+', common_text):
        macro = m.group(1)
        short = macro[len("ZB_ZCL_CLUSTER_ID_"):]
        if short.endswith("_SERVER_ROLE_INIT") or short.endswith("_CLIENT_ROLE_INIT"):
            continue
        infos[short] = SDKClusterInfo(short_name=short, id_macro=macro)

    for header in sorted(zcl_dir.glob("zb_zcl_*.h")):
        text = header.read_text(encoding="utf-8", errors="ignore")
        for macro, params in _parse_declare_macros(text):
            core = macro[len("ZB_ZCL_DECLARE_"):]
            candidates = []
            if core.endswith("_ATTRIB_LIST_EXT"):
                candidates.append(core[:-len("_ATTRIB_LIST_EXT")])
            if core.endswith("_ATTRIB_LIST"):
                candidates.append(core[:-len("_ATTRIB_LIST")])
            for candidate in candidates:
                if candidate in infos and infos[candidate].declare_macro is None:
                    infos[candidate] = SDKClusterInfo(
                        short_name=candidate,
                        id_macro=infos[candidate].id_macro,
                        declare_macro=macro,
                        declare_params=params,
                        header=header.name,
                    )
                    break
    return infos


def load_config(path: Path) -> ProjectConfig:
    require_pyyaml()
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    project_name = data["project"]["name"]
    project_norm = sanitize_project_name(project_name)
    project_upper = project_norm.upper()
    device = data["device"]
    sdk_zcl_path = str(data["sdk"]["zcl_path"])
    sdk_clusters = scan_sdk_clusters(sdk_zcl_path)
    clusters: list[Cluster] = []
    seen: set[tuple[str, str]] = set()
    for item in data["clusters"]:
        short_name = normalize_cluster_name(item["name"])
        role = str(item["role"]).strip().lower()
        if sdk_clusters and short_name not in sdk_clusters:
            raise ValueError(f"Unsupported cluster: {short_name}")
        if role not in {"server", "client"}:
            raise ValueError(f"Unsupported role for {short_name}: {role}")
        key = (short_name, role)
        if key in seen:
            raise ValueError(f"Duplicate cluster/role entry: {short_name}/{role}")
        seen.add(key)
        sdk = sdk_clusters.get(short_name)
        id_macro = sdk.id_macro if sdk else (short_name if short_name.startswith("ZB_ZCL_CLUSTER_ID_") else f"ZB_ZCL_CLUSTER_ID_{short_name}")
        clusters.append(Cluster(id_macro, role, sdk))
    return ProjectConfig(
        project_name=project_norm,
        project_upper=project_upper,
        sdk_zcl_path=sdk_zcl_path,
        device_id=as_c_int_literal(device["id"]),
        device_version=str(device.get("version", 0)),
        endpoint=int(device.get("endpoint", 1)),
        report_attr_count=int(device["report_attr_count"]),
        clusters=clusters,
        init=data.get("init", {}),
        sdk_clusters=sdk_clusters,
    )


def validate_sdk_path(cfg: ProjectConfig) -> str | None:
    common_h = Path(cfg.sdk_zcl_path) / "zb_zcl_common.h"
    if common_h.exists():
        return None
    return f"Warning: SDK file not found for validation: {common_h}"



def infer_placeholder_field_type(cluster_short_name: str, param: str) -> str:
    p = param.lower()
    cluster = cluster_short_name.upper()
    if "description" in p or p.endswith("_name") or p.endswith("_desc"):
        return "zb_char_t[33]"
    if p == "on_off":
        return "zb_bool_t"
    if "out_of_service" in p:
        return "zb_bool_t"
    if "status_flags" in p or "reliability" in p:
        return "zb_uint8_t"
    if "engineering_units" in p:
        return "zb_uint16_t"
    if "app_type" in p:
        return "zb_uint32_t"
    if "checkin_interval" in p or "long_poll_interval" in p:
        return "zb_uint32_t"
    if "short_poll_interval" in p or "fast_poll_timeout" in p:
        return "zb_uint16_t"
    if "present_value" in p or "resolution" in p:
        if cluster in {"ANALOG_INPUT", "ANALOG_OUTPUT", "ANALOG_VALUE"}:
            return "zb_single_t"
        return "zb_int16_t"
    if "measured_value" in p or p.startswith("min_") or p.startswith("max_") or "tolerance" in p:
        if cluster in {"ANALOG_INPUT", "ANALOG_OUTPUT", "ANALOG_VALUE"}:
            return "zb_single_t"
        return "zb_int16_t"
    return "zb_uint8_t"


def field_decl_from_type(field_type: str, field_name: str) -> str:
    if field_type.endswith("[33]"):
        base = field_type[:-4]
        return f"    {base} {field_name}[33];"
    return f"    {field_type} {field_name};"


def field_expr(field_name: str, field_type: str, cluster_short_name: str) -> str:
    base = f"dev_ctx.{cluster_short_name.lower()}_attr.{field_name}"
    if field_type.endswith("[33]"):
        return base
    return f"&{base}"


def emit_dynamic_cluster_struct(cluster: Cluster) -> str:
    type_name = f"zb_zcl_{cluster.short_name.lower()}_attr_t"
    params = tuple(cluster.sdk.declare_params[1:]) if cluster.sdk and cluster.sdk.declare_params else ()
    lines = ["typedef struct", "{"]
    if not params:
        lines.append("    zb_uint8_t placeholder;")
    else:
        for p in params:
            field_type = infer_placeholder_field_type(cluster.short_name, p)
            lines.append(field_decl_from_type(field_type, p))
    lines.append(f"}} {type_name};")
    return "\n".join(lines)


def emit_dynamic_cluster_decl(cluster: Cluster) -> str:
    if not cluster.sdk or not cluster.sdk.declare_macro:
        return (
            f"// SDK declare macro not found for {cluster.id_macro}. Using empty fallback attr list.\n"
            f"ZB_ZCL_DECLARE_EMPTY_ATTRIB_LIST_FOR_CLUSTER(\n"
            f"    {cluster.attr_list_var},\n"
            f"    {cluster.id_macro});"
        )
    params = tuple(cluster.sdk.declare_params[1:])
    lines = [
        f"// SDK macro from {cluster.sdk.header}",
        f"{cluster.sdk.declare_macro}(",
        f"    {cluster.attr_list_var},",
    ]
    for i, p in enumerate(params):
        field_type = infer_placeholder_field_type(cluster.short_name, p)
        expr = field_expr(p, field_type, cluster.short_name)
        comma = "," if i != len(params) - 1 else ""
        lines.append(f"    {expr}{comma}")
    lines.append(");")
    return "\n".join(lines)


def has_cluster(cfg: ProjectConfig, name: str, role: str | None = None) -> bool:
    return any(c.short_name == name and (role is None or c.role == role) for c in cfg.clusters)


def unsupported_clusters(cfg: ProjectConfig) -> list[Cluster]:
    return [c for c in cfg.clusters if not c.is_fully_supported]


def unsupported_server_clusters(cfg: ProjectConfig) -> list[Cluster]:
    return [c for c in cfg.clusters if (not c.is_fully_supported) and c.role == "server"]


def sdk_header_includes(cfg: ProjectConfig) -> list[str]:
    headers: list[str] = []
    seen: set[str] = set()
    for c in cfg.clusters:
        if c.sdk and c.sdk.header and c.sdk.header not in seen:
            seen.add(c.sdk.header)
            headers.append(c.sdk.header)
    return headers


def server_clusters(cfg: ProjectConfig) -> list[Cluster]:
    return [c for c in cfg.clusters if c.role == "server"]


def client_clusters(cfg: ProjectConfig) -> list[Cluster]:
    return [c for c in cfg.clusters if c.role == "client"]


def ordered_clusters(cfg: ProjectConfig) -> list[Cluster]:
    return server_clusters(cfg) + client_clusters(cfg)


def macro_block(lines: list[str]) -> str:
    out = []
    for i, line in enumerate(lines):
        suffix = f" {BACKSLASH}" if i != len(lines) - 1 else ""
        out.append(f"{line}{suffix}")
    return "\n".join(out)


def emit_range_extender_h(cfg: ProjectConfig) -> str:
    in_count = len(server_clusters(cfg))
    out_count = len(client_clusters(cfg))
    macro_name = f"ZB_DECLARE_{cfg.project_upper}_CLUSTER_LIST"
    simple_desc_macro = f"ZB_ZCL_DECLARE_{cfg.project_upper}_SIMPLE_DESC"
    ep_macro = f"ZB_DECLARE_{cfg.project_upper}_EP"

    param_lines = [f"#define {macro_name}(", "\tcluster_list_name,"]
    for i, c in enumerate(ordered_clusters(cfg)):
        comma = "," if i != len(cfg.clusters) - 1 else ")"
        param_lines.append(f"\t{c.macro_param_name}{comma}")
    param_lines += ["\tzb_zcl_cluster_desc_t cluster_list_name[] =", "\t\t{"]
    for i, c in enumerate(ordered_clusters(cfg)):
        comma = "," if i != len(cfg.clusters) - 1 else ""
        param_lines += [
            "\t\t\tZB_ZCL_CLUSTER_DESC(",
            f"\t\t\t\t{c.id_macro},",
            f"\t\t\t\tZB_ZCL_ARRAY_SIZE({c.macro_param_name}, zb_zcl_attr_t),",
            f"\t\t\t\t({c.macro_param_name}),",
            f"\t\t\t\t{c.role_macro},",
            f"\t\t\t\tZB_ZCL_MANUF_CODE_INVALID){comma}",
        ]
    param_lines.append("\t\t}")

    sd_lines = [
        f"#define {simple_desc_macro}(",
        "\tep_name, ep_id, in_clust_num, out_clust_num)",
        "\tZB_DECLARE_SIMPLE_DESC(in_clust_num, out_clust_num);",
        "\tZB_AF_SIMPLE_DESC_TYPE(in_clust_num, out_clust_num)",
        "\tsimple_desc_##ep_name =",
        "\t\t{",
        "\t\t\tep_id,",
        "\t\t\tZB_AF_HA_PROFILE_ID,",
        "\t\t\tZB_ENV_SENSOR_DEVICE_ID,",
        "\t\t\tZB_DEVICE_VER_ENV_SENSOR,",
        "\t\t\t0,",
        "\t\t\tin_clust_num,",
        "\t\t\tout_clust_num,",
        "\t\t\t{" + ", ".join(c.id_macro for c in ordered_clusters(cfg)) + "}}",
    ]

    ep_lines = [
        f"#define {ep_macro}(ep_name, ep_id, cluster_list)",
        f"\t{simple_desc_macro}(ep_name, ep_id,",
        f"\t\t\t\t\t\t  ZB_{cfg.project_upper}_IN_CLUSTER_NUM, ZB_{cfg.project_upper}_OUT_CLUSTER_NUM);",
        "\tZBOSS_DEVICE_DECLARE_REPORTING_CTX(reporting_info##ep_name,",
        f"\t\t\t\t\t\t   ZB_{cfg.project_upper}_REPORT_ATTR_COUNT);",
        "\tZB_AF_DECLARE_ENDPOINT_DESC(ep_name, ep_id, ZB_AF_HA_PROFILE_ID, 0, NULL,",
        "\t\t\t\t\t\t\t\tZB_ZCL_ARRAY_SIZE(cluster_list, zb_zcl_cluster_desc_t), cluster_list,",
        "\t\t\t\t\t\t\t\t(zb_af_simple_desc_1_1_t *)&simple_desc_##ep_name,",
        f"\t\t\t\t\t\t\t\tZB_{cfg.project_upper}_REPORT_ATTR_COUNT, reporting_info##ep_name,",
        "\t\t\t\t\t\t\t\t0, NULL) // No CVC ctx",
    ]

    body = [
        "#ifndef __ZB_RANGE_EXTENDER_H__",
        "#define __ZB_RANGE_EXTENDER_H__",
        "",
        f"#define ZB_ENV_SENSOR_DEVICE_ID {cfg.device_id}",
        "",
        f"#define ZB_DEVICE_VER_ENV_SENSOR {cfg.device_version}",
        "",
        "// number of IN (server) clusters",
        f"#define ZB_{cfg.project_upper}_IN_CLUSTER_NUM {in_count}",
        "",
        "// number of OUT (client) clusters",
        f"#define ZB_{cfg.project_upper}_OUT_CLUSTER_NUM {out_count}",
        "",
        "// total number of (IN+OUT) clusters",
        f"#define ZB_{cfg.project_upper}_CLUSTER_NUM {BACKSLASH}",
        f"\t(ZB_{cfg.project_upper}_IN_CLUSTER_NUM + ZB_{cfg.project_upper}_OUT_CLUSTER_NUM)",
        "",
        "// Number of attributes for reporting",
        f"#define ZB_{cfg.project_upper}_REPORT_ATTR_COUNT {cfg.report_attr_count}",
        "",
        macro_block(param_lines),
        "",
        macro_block(sd_lines),
        "",
        macro_block(ep_lines),
        "",
        "#endif // __ZB_RANGE_EXTENDER_H__",
        "",
    ]
    return "\n".join(body)


def emit_basic_defines(cfg: ProjectConfig) -> list[str]:
    basic = cfg.init.get("basic", {})
    out: list[str] = []
    if has_cluster(cfg, "BASIC", "server"):
        model_id = basic.get("model_id", cfg.project_name)
        out.extend([
            f"#define {cfg.project_upper}_INIT_BASIC_APP_VERSION {basic.get('app_version', 1)}",
            f"#define {cfg.project_upper}_INIT_BASIC_STACK_VERSION {basic.get('stack_version', 10)}",
            f"#define {cfg.project_upper}_INIT_BASIC_HW_VERSION {basic.get('hw_version', 11)}",
            f"#define {cfg.project_upper}_INIT_BASIC_SW_VERSION {as_c_value(basic.get('sw_version', 'build-1'))}",
            f"#define {cfg.project_upper}_INIT_BASIC_MANUF_NAME {as_c_value(basic.get('manufacturer_name', 'Nordic'))}",
            f"#define {cfg.project_upper}_INIT_BASIC_MODEL_ID {as_c_value(model_id)}",
            f"#define {cfg.project_upper}_INIT_BASIC_DATE_CODE {as_c_value(basic.get('date_code', '033026'))}",
            f"#define {cfg.project_upper}_INIT_BASIC_POWER_SOURCE {basic.get('power_source', 'ZB_ZCL_BASIC_POWER_SOURCE_BATTERY')}",
            f"#define {cfg.project_upper}_INIT_BASIC_LOCATION_DESC {as_c_value(basic.get('location_desc', ''))}",
            f"#define {cfg.project_upper}_INIT_BASIC_PH_ENV {basic.get('physical_env', 'ZB_ZCL_BASIC_ENV_UNSPECIFIED')}",
        ])
    return out


def emit_measurement_defines(cfg: ProjectConfig) -> list[str]:
    out: list[str] = []
    if has_cluster(cfg, "TEMP_MEASUREMENT", "server"):
        init = cfg.init.get("temp_measurement", {})
        out.extend([
            f"#define ATTR_TEMP_MIN ({init.get('min_value', -4000)})",
            f"#define ATTR_TEMP_MAX ({init.get('max_value', 8500)})",
            f"#define ATTR_TEMP_TOLERANCE ({init.get('tolerance', 100)})",
        ])
    if has_cluster(cfg, "REL_HUMIDITY_MEASUREMENT", "server"):
        init = cfg.init.get("rel_humidity_measurement", {})
        out.extend([
            f"#define ATTR_HUM_MIN ({init.get('min_value', 0)})",
            f"#define ATTR_HUM_MAX ({init.get('max_value', 10000)})",
        ])
    if has_cluster(cfg, "PRESSURE_MEASUREMENT", "server"):
        init = cfg.init.get("pressure_measurement", {})
        out.extend([
            f"#define ATTR_PRESSURE_MIN ({init.get('min_value', 3000)})",
            f"#define ATTR_PRESSURE_MAX ({init.get('max_value', 11000)})",
            f"#define ATTR_PRESSURE_TOLERANCE ({init.get('tolerance', 1)})",
        ])
    return out


def emit_struct_typedefs(cfg: ProjectConfig) -> str:
    blocks: list[str] = []
    if has_cluster(cfg, "POWER_CONFIG", "server"):
        blocks.append("""typedef struct zb_zcl_power_attrs zb_zcl_power_attrs_t;\n\nstruct zb_zcl_power_attrs\n{\n    zb_uint8_t voltage;\n    zb_uint8_t percent_remaining;\n};""")
    if has_cluster(cfg, "REL_HUMIDITY_MEASUREMENT", "server"):
        blocks.append("""typedef struct\n{\n    zb_int16_t measure_value;\n    zb_int16_t min_measure_value;\n    zb_int16_t max_measure_value;\n} zb_zcl_humidity_measurement_attrs_t;""")
    if has_cluster(cfg, "PRESSURE_MEASUREMENT", "server"):
        blocks.append("""typedef struct\n{\n    zb_int16_t measure_value;\n    zb_int16_t min_measure_value;\n    zb_int16_t max_measure_value;\n    zb_int16_t tolerance_value;\n} zb_zcl_pressure_measurement_attrs_t;""")
    if has_cluster(cfg, "POLL_CONTROL", "server"):
        blocks.append("""typedef struct\n{\n    zb_uint32_t checkin_interval;\n    zb_uint32_t long_poll_interval;\n    zb_uint16_t short_poll_interval;\n    zb_uint16_t fast_poll_timeout;\n    zb_uint32_t checkin_interval_min;\n    zb_uint32_t long_poll_interval_min;\n    zb_uint16_t fast_poll_timeout_max;\n} zb_zcl_poll_control_attrs_t;""")
    for cluster in unsupported_server_clusters(cfg):
        blocks.append(emit_dynamic_cluster_struct(cluster))
    return "\n\n".join(blocks)


def emit_device_ctx_struct(cfg: ProjectConfig) -> str:
    members = []
    if has_cluster(cfg, "BASIC", "server"):
        members.append("    zb_zcl_basic_attrs_ext_t basic_attr;")
    if has_cluster(cfg, "IDENTIFY", "server"):
        members.append("    zb_zcl_identify_attrs_t identify_attr;")
    if has_cluster(cfg, "TEMP_MEASUREMENT", "server"):
        members.append("    zb_zcl_temp_measurement_attrs_t temp_measure_attrs;")
    if has_cluster(cfg, "REL_HUMIDITY_MEASUREMENT", "server"):
        members.append("    zb_zcl_humidity_measurement_attrs_t humidity_measure_attrs;")
    if has_cluster(cfg, "PRESSURE_MEASUREMENT", "server"):
        members.append("    zb_zcl_pressure_measurement_attrs_t pressure_measure_attrs;")
    if has_cluster(cfg, "POWER_CONFIG", "server"):
        members.append("    zb_zcl_power_attrs_t power_attr;")
    if has_cluster(cfg, "POLL_CONTROL", "server"):
        members.append("    zb_zcl_poll_control_attrs_t poll_control_attrs;")
    if has_cluster(cfg, "ON_OFF", "server"):
        members.append("    zb_zcl_on_off_attrs_t on_off_attr;")
    for cluster in unsupported_server_clusters(cfg):
        members.append(f"    zb_zcl_{cluster.short_name.lower()}_attr_t {cluster.short_name.lower()}_attr;")
    return "\n".join(members)


def emit_struct_h(cfg: ProjectConfig) -> str:
    basic_defines = emit_basic_defines(cfg)
    measure_defines = emit_measurement_defines(cfg)
    lines = [
        "#ifndef ZB_ZCL_STRUCT_H",
        "#define ZB_ZCL_STRUCT_H",
        "",
        "#include <zboss_api.h>",
        "#include <zboss_api_addons.h>",
    ]
    for header in sdk_header_includes(cfg):
        lines.append(f"#include <zcl/{header}>")
    lines.append("")
    lines.extend(basic_defines)
    if basic_defines:
        lines.append("")
    lines.extend(measure_defines)
    if measure_defines:
        lines.append("")
    lines.extend([
        f"#define ENDPOINT_NUM {cfg.endpoint}",
        f"#define ERASE_PERSISTENT_CONFIG {APP_DEFAULTS['erase_persistent_config']}",
        "",
        f"#define READ_DATA_INITIAL_DELAY {APP_DEFAULTS['read_data_initial_delay']}",
        f"#define READ_DATA_TIMER_PERIOD {APP_DEFAULTS['read_data_timer_period']}",
        f"#define LONG_POLL_INTERVAL {APP_DEFAULTS['long_poll_interval']}",
        "",
    ])
    typedefs = emit_struct_typedefs(cfg)
    if typedefs:
        lines.append(typedefs)
        lines.append("")
    lines.extend([
        "struct zb_device_ctx",
        "{",
        emit_device_ctx_struct(cfg),
        "};",
        "",
        "extern struct zb_device_ctx dev_ctx;",
        "",
        f"extern zb_af_device_ctx_t {cfg.project_upper}_ctx;",
        f"extern zb_af_endpoint_desc_t {cfg.project_upper}_ep;",
        "",
        "void app_clusters_attr_init(void);",
        "",
        "#endif /* ZB_ZCL_STRUCT_H */",
        "",
    ])
    return "\n".join(lines)


def emit_sdk_skeleton_comment(cluster: Cluster) -> str:
    if not cluster.sdk or not cluster.sdk.declare_macro:
        return f"// SDK declare macro not found for {cluster.id_macro}. Using empty fallback attr list."
    params = list(cluster.sdk.declare_params)[1:]
    lines = [
        f"// SDK macro from {cluster.sdk.header}",
        f"// Fill real storage/arguments if you want full cluster support.",
        f"// {cluster.sdk.declare_macro}(",
        f"//     {cluster.attr_list_var},",
    ]
    for i, p in enumerate(params):
        comma = "," if i != len(params) - 1 else ""
        lines.append(f"//     {p}{comma}")
    lines += ["// );"]
    return "\n".join(lines)


def emit_attr_declarations(cfg: ProjectConfig) -> str:
    blocks: list[str] = []
    unsupported = [c for c in cfg.clusters if not c.is_fully_supported]
    if unsupported:
        blocks.append(f"""#define ZB_ZCL_DECLARE_EMPTY_ATTRIB_LIST_FOR_CLUSTER(attr_list, cluster_id) {BACKSLASH}
    ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(attr_list, cluster_id)     {BACKSLASH}
    ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST""")
    if has_cluster(cfg, "BASIC", "server"):
        blocks.append("""ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
    basic_attr_list,
    &dev_ctx.basic_attr.zcl_version,
    &dev_ctx.basic_attr.app_version,
    &dev_ctx.basic_attr.stack_version,
    &dev_ctx.basic_attr.hw_version,
    dev_ctx.basic_attr.mf_name,
    dev_ctx.basic_attr.model_id,
    dev_ctx.basic_attr.date_code,
    &dev_ctx.basic_attr.power_source,
    dev_ctx.basic_attr.location_id,
    &dev_ctx.basic_attr.ph_env,
    dev_ctx.basic_attr.sw_ver);""")
    if has_cluster(cfg, "IDENTIFY", "client"):
        blocks.append("""ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(
    identify_client_attr_list);""")
    if has_cluster(cfg, "IDENTIFY", "server"):
        blocks.append("""ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
    identify_server_attr_list,
    &dev_ctx.identify_attr.identify_time);""")
    if has_cluster(cfg, "POWER_CONFIG", "server"):
        blocks.append(f"""#define ZB_ZCL_DECLARE_POWER_CONFIG_VOLTAGE_PERCENT_ATTRIB_LIST(attr_list, voltage, remaining)        {BACKSLASH}
    ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(attr_list, ZB_ZCL_POWER_CONFIG)                 {BACKSLASH}
    ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID(voltage, ),                    {BACKSLASH}
        ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID(remaining, ), {BACKSLASH}
        ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

ZB_ZCL_DECLARE_POWER_CONFIG_VOLTAGE_PERCENT_ATTRIB_LIST(
    power_config_server_attr_list,
    &dev_ctx.power_attr.voltage,
    &dev_ctx.power_attr.percent_remaining);""")
    if has_cluster(cfg, "ON_OFF", "server"):
        blocks.append("""ZB_ZCL_DECLARE_ON_OFF_ATTRIB_LIST(
    on_off_attr_list,
    &dev_ctx.on_off_attr.on_off);""")
    if has_cluster(cfg, "POLL_CONTROL", "server"):
        blocks.append("""ZB_ZCL_DECLARE_POLL_CONTROL_ATTRIB_LIST(
    poll_control_attrib_list,
    &dev_ctx.poll_control_attrs.checkin_interval,
    &dev_ctx.poll_control_attrs.long_poll_interval,
    &dev_ctx.poll_control_attrs.short_poll_interval,
    &dev_ctx.poll_control_attrs.fast_poll_timeout,
    &dev_ctx.poll_control_attrs.checkin_interval_min,
    &dev_ctx.poll_control_attrs.long_poll_interval_min,
    &dev_ctx.poll_control_attrs.fast_poll_timeout_max);""")
    if has_cluster(cfg, "TEMP_MEASUREMENT", "server"):
        blocks.append("""ZB_ZCL_DECLARE_TEMP_MEASUREMENT_ATTRIB_LIST(
    temp_measurement_attr_list,
    &dev_ctx.temp_measure_attrs.measure_value,
    &dev_ctx.temp_measure_attrs.min_measure_value,
    &dev_ctx.temp_measure_attrs.max_measure_value,
    &dev_ctx.temp_measure_attrs.tolerance);""")
    if has_cluster(cfg, "REL_HUMIDITY_MEASUREMENT", "server"):
        blocks.append("""ZB_ZCL_DECLARE_REL_HUMIDITY_MEASUREMENT_ATTRIB_LIST(
    humidity_measurement_attr_list,
    &dev_ctx.humidity_measure_attrs.measure_value,
    &dev_ctx.humidity_measure_attrs.min_measure_value,
    &dev_ctx.humidity_measure_attrs.max_measure_value);""")
    if has_cluster(cfg, "PRESSURE_MEASUREMENT", "server"):
        blocks.append("""ZB_ZCL_DECLARE_PRESSURE_MEASUREMENT_ATTRIB_LIST(
    pressure_measurement_attr_list,
    &dev_ctx.pressure_measure_attrs.measure_value,
    &dev_ctx.pressure_measure_attrs.min_measure_value,
    &dev_ctx.pressure_measure_attrs.max_measure_value,
    &dev_ctx.pressure_measure_attrs.tolerance_value);""")
    for cluster in unsupported:
        if cluster.role == "server":
            blocks.append(emit_dynamic_cluster_decl(cluster))
        else:
            blocks.append(
                emit_sdk_skeleton_comment(cluster)
                + "\n"
                + f"ZB_ZCL_DECLARE_EMPTY_ATTRIB_LIST_FOR_CLUSTER(\n    {cluster.attr_list_var},\n    {cluster.id_macro});"
            )
    return "\n\n".join(blocks)


def emit_cluster_list_invocation(cfg: ProjectConfig) -> str:
    args = ",\n    ".join(c.attr_list_var for c in ordered_clusters(cfg))
    return f"""ZB_DECLARE_{cfg.project_upper}_CLUSTER_LIST(
    {cfg.project_upper}_clusters,
    {args});"""


def emit_init_body(cfg: ProjectConfig) -> str:
    lines: list[str] = [f"\tZB_AF_REGISTER_DEVICE_CTX(&{cfg.project_upper}_ctx);", ""]
    if has_cluster(cfg, "BASIC", "server"):
        lines += [
            "\t// Basic cluster attributes data.",
            f"\tdev_ctx.basic_attr.zcl_version = {cfg.init.get('basic', {}).get('zcl_version', 'ZB_ZCL_VERSION')};",
            f"\tdev_ctx.basic_attr.power_source = {cfg.project_upper}_INIT_BASIC_POWER_SOURCE;",
            f"\tdev_ctx.basic_attr.stack_version = {cfg.project_upper}_INIT_BASIC_STACK_VERSION;",
            f"\tdev_ctx.basic_attr.hw_version = {cfg.project_upper}_INIT_BASIC_HW_VERSION;",
            "",
            "\tZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.mf_name,",
            f"\t\t\t\t\t  {cfg.project_upper}_INIT_BASIC_MANUF_NAME,",
            f"\t\t\t\t\t  ZB_ZCL_STRING_CONST_SIZE({cfg.project_upper}_INIT_BASIC_MANUF_NAME));",
            "",
            "\tZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.model_id,",
            f"\t\t\t\t\t  {cfg.project_upper}_INIT_BASIC_MODEL_ID,",
            f"\t\t\t\t\t  ZB_ZCL_STRING_CONST_SIZE({cfg.project_upper}_INIT_BASIC_MODEL_ID));",
            "",
            "\tZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.date_code,",
            f"\t\t\t\t\t  {cfg.project_upper}_INIT_BASIC_DATE_CODE,",
            f"\t\t\t\t\t  ZB_ZCL_STRING_CONST_SIZE({cfg.project_upper}_INIT_BASIC_DATE_CODE));",
            "",
            "\tZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.location_id,",
            f"\t\t\t\t\t  {cfg.project_upper}_INIT_BASIC_LOCATION_DESC,",
            f"\t\t\t\t\t  ZB_ZCL_STRING_CONST_SIZE({cfg.project_upper}_INIT_BASIC_LOCATION_DESC));",
            "",
            f"\tdev_ctx.basic_attr.ph_env = {cfg.project_upper}_INIT_BASIC_PH_ENV;",
            f"\tdev_ctx.basic_attr.app_version = {cfg.project_upper}_INIT_BASIC_APP_VERSION;",
            "\tZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.sw_ver,",
            f"\t\t\t\t\t  {cfg.project_upper}_INIT_BASIC_SW_VERSION,",
            f"\t\t\t\t\t  ZB_ZCL_STRING_CONST_SIZE({cfg.project_upper}_INIT_BASIC_SW_VERSION));",
            "",
        ]
    if has_cluster(cfg, "IDENTIFY", "server"):
        value = cfg.init.get("identify", {}).get("identify_time", "ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE")
        lines += [f"\tdev_ctx.identify_attr.identify_time = {value};", ""]
    if has_cluster(cfg, "POWER_CONFIG", "server"):
        power = cfg.init.get("power_config", {})
        lines += [
            f"\tdev_ctx.power_attr.voltage = {power.get('battery_voltage', 'ZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_INVALID')};",
            f"\tdev_ctx.power_attr.percent_remaining = {power.get('battery_percentage_remaining', 'ZB_ZCL_POWER_CONFIG_BATTERY_REMAINING_UNKNOWN')};",
            "",
        ]
    if has_cluster(cfg, "TEMP_MEASUREMENT", "server"):
        temp = cfg.init.get("temp_measurement", {})
        lines += [
            f"\tdev_ctx.temp_measure_attrs.measure_value = {temp.get('measured_value', 'ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN')};",
            "\tdev_ctx.temp_measure_attrs.min_measure_value = ATTR_TEMP_MIN;",
            "\tdev_ctx.temp_measure_attrs.max_measure_value = ATTR_TEMP_MAX;",
            "\tdev_ctx.temp_measure_attrs.tolerance = ATTR_TEMP_TOLERANCE;",
            "",
        ]
    if has_cluster(cfg, "REL_HUMIDITY_MEASUREMENT", "server"):
        hum = cfg.init.get("rel_humidity_measurement", {})
        lines += [
            f"\tdev_ctx.humidity_measure_attrs.measure_value = {hum.get('measured_value', 'ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_UNKNOWN')};",
            "\tdev_ctx.humidity_measure_attrs.min_measure_value = ATTR_HUM_MIN;",
            "\tdev_ctx.humidity_measure_attrs.max_measure_value = ATTR_HUM_MAX;",
            "",
        ]
    if has_cluster(cfg, "PRESSURE_MEASUREMENT", "server"):
        press = cfg.init.get("pressure_measurement", {})
        lines += [
            f"\tdev_ctx.pressure_measure_attrs.measure_value = {press.get('measured_value', 'ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_UNKNOWN')};",
            "\tdev_ctx.pressure_measure_attrs.min_measure_value = ATTR_PRESSURE_MIN;",
            "\tdev_ctx.pressure_measure_attrs.max_measure_value = ATTR_PRESSURE_MAX;",
            "\tdev_ctx.pressure_measure_attrs.tolerance_value = ATTR_PRESSURE_TOLERANCE;",
            "",
        ]
    if has_cluster(cfg, "ON_OFF", "server"):
        lines += ["\tdev_ctx.on_off_attr.on_off = ZB_FALSE;", ""]
    if has_cluster(cfg, "POLL_CONTROL", "server"):
        poll = cfg.init.get("poll_control", {})
        lines += [
            "\tdev_ctx.poll_control_attrs.checkin_interval =",
            f"\t\t{poll.get('checkin_interval', 'ZB_ZCL_POLL_CONTROL_CHECKIN_INTERVAL_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.long_poll_interval =",
            f"\t\t{poll.get('long_poll_interval', 'ZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.short_poll_interval =",
            f"\t\t{poll.get('short_poll_interval', 'ZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.fast_poll_timeout =",
            f"\t\t{poll.get('fast_poll_timeout', 'ZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.checkin_interval_min =",
            f"\t\t{poll.get('checkin_interval_min', 'ZB_ZCL_POLL_CONTROL_CHECKIN_MIN_INTERVAL_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.long_poll_interval_min =",
            f"\t\t{poll.get('long_poll_interval_min', 'ZB_ZCL_POLL_CONTROL_LONG_POLL_MIN_INTERVAL_DEFAULT_VALUE')};",
            "\tdev_ctx.poll_control_attrs.fast_poll_timeout_max =",
            f"\t\t{poll.get('fast_poll_timeout_max', 'ZB_ZCL_POLL_CONTROL_FAST_POLL_MAX_TIMEOUT_DEFAULT_VALUE')};",
            "",
        ]
    for cluster in cfg.clusters:
        if not cluster.is_fully_supported:
            if cluster.has_sdk_declare_macro:
                lines += [f"\t// TODO: provide storage/init for {cluster.id_macro} and replace empty fallback attr list using {cluster.sdk.declare_macro}."]
            else:
                lines += [f"\t// TODO: no SDK declare macro found for {cluster.id_macro}; manual cluster support required."]
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


def emit_struct_c(cfg: ProjectConfig) -> str:
    return "\n".join([
        '#include "zb_zcl_struct.h"',
        '#include "zb_range_extender.h"',
        "",
        "struct zb_device_ctx dev_ctx;",
        "",
        emit_attr_declarations(cfg),
        "",
        emit_cluster_list_invocation(cfg),
        "",
        f"ZB_DECLARE_{cfg.project_upper}_EP(",
        f"    {cfg.project_upper}_ep,",
        "    ENDPOINT_NUM,",
        f"    {cfg.project_upper}_clusters);",
        "",
        "ZBOSS_DECLARE_DEVICE_CTX_1_EP(",
        f"    {cfg.project_upper}_ctx,",
        f"    {cfg.project_upper}_ep);",
        "",
        "void app_clusters_attr_init(void)",
        "{",
        emit_init_body(cfg),
        "}",
        "",
    ])


def write_file(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8", newline="\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate ZBOSS Zigbee boilerplate from YAML")
    parser.add_argument("yaml_path", help="Path to project YAML")
    args = parser.parse_args(argv)
    cfg = load_config(Path(args.yaml_path))
    warning = validate_sdk_path(cfg)
    if warning:
        print(warning, file=sys.stderr)
    project_root = Path.cwd()
    include_dir = project_root / "include"
    src_dir = project_root / "src"
    include_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)
    write_file(include_dir / "zb_range_extender.h", emit_range_extender_h(cfg))
    write_file(include_dir / "zb_zcl_struct.h", emit_struct_h(cfg))
    write_file(src_dir / "zb_zcl_struct.c", emit_struct_c(cfg))
    print(f"Generated in {project_root}:")
    print("  include/zb_range_extender.h")
    print("  include/zb_zcl_struct.h")
    print("  src/zb_zcl_struct.c")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
