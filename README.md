# ZCL Cluster Generator for nRF Connect SDK (ZBOSS)

This repository contains a utility for automatic generation of ZCL clusters for projects based on **nRF Connect SDK** and **ZBOSS**.

## Why this project exists

In the Nordic SDK, adding ZCL clusters is typically done using the provided template and the `zb_range_extender.h` file. This approach works, but in practice it has several drawbacks.

First, adding new clusters:

- requires a lot of attention to small details
- takes noticeable time even for a relatively simple device

Second, the SDK template project structure is more suitable for a demo example than for real development.

Another issue is that cluster descriptions, attribute structures, and their initialization often end up directly inside `main.c`. As a result, `main` becomes cluttered with Zigbee-specific code and is harder to read.

This repository proposes a different approach:

- use a cleaner `main.c` template without ZCL-related components
- generate ZCL-related files automatically
- keep cluster descriptions separate from the main application logic

## What is included in the repository

The repository contains a template project with a standard structure.

The project root contains:

- `gen_zcl.py` — the generator
- `prj.yaml` — the project configuration file

The generated files:

- `/include/zb_range_extender.h`
- `/include/zb_zcl_struct.h`
- `/src/zb_zcl_struct.c`

are created in the corresponding project directories.

## How it works

The generator reads project settings from `prj.yaml` and automatically generates the required ZCL files.

Run it like this:

```bash
python3 gen_zcl.py prj.yaml
```

## `prj.yaml` file format

The generator uses a YAML file with the following structure:

```yaml
project:
  name: name_your_project

sdk:
  zcl_path: "path to the ZCL directory in the SDK \\ncs-zigbee\\lib\\zboss\\include\\zcl"
  # example:
  # E:\\ncs_workspaces\\zigbee-r23\\ncs-zigbee\\lib\\zboss\\include\\zcl

device:
  id: 0x000C
  version: 0
  endpoint: 1
  report_attr_count: 2

clusters:
  - name: zcl_cluster_id
    role: server
```

## Field description

### `project.name`

Project name.  
It is used as part of generated macros and identifiers.

### `sdk.zcl_path`

Path to the ZCL headers in your SDK.

For example:

```text
E:\ncs_workspaces\zigbee-r23\ncs-zigbee\lib\zboss\include\zcl
```

### `device.id`

Zigbee Device ID.

### `device.version`

Device version.

### `device.endpoint`

Endpoint number.

### `device.report_attr_count`

Number of attributes used for reporting.

### `clusters`

List of clusters to include in the project.

Each cluster is defined by:

- `name`
- `role` — `server` or `client`

## What is generated

After running the generator, the following files are created:

- `zb_range_extender.h`
- `zb_zcl_struct.h`
- `zb_zcl_struct.c`

These files contain:

- cluster list description
- attribute structures
- attribute list declarations
- basic attribute initialization

## Main idea

The purpose of this tool is to remove repetitive manual work when adding ZCL clusters to a project.

Instead of constantly editing service/template code by hand, it is enough to:

- describe the project in `prj.yaml`
- run the generator
- get a ready-made set of ZCL files

This gives you:

- less manual routine
- a cleaner `main.c`
- faster cluster setup
- a project structure that is better suited for real development

## Important note

The generator works only as well as the selected cluster is actually supported by the SDK version and the ZBOSS library being used.

In practice, a situation is possible where:

- a cluster is already present in the SDK headers
- but is not yet fully supported in the library

In such cases, manual adjustment may still be required.
