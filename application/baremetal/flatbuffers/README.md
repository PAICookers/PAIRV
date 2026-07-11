# FlatBuffers Reader

`flatbuffers` is the smallest bare-metal example of reading a PAIBox
runtime artifact from flash. It does not configure PAICORE, encode input,
decode output, or execute CPU tasks.

## 1. Install the artifact

Copy the matching target-package artifact and schema from a PAIBox export:

```sh
cp <export>/runtime/targets/rv_cpu0/compile_artifacts.bin fixtures/
cp <export>/runtime/targets/rv_cpu0/compile_artifacts.fbs fixtures/
```

`compile_artifacts.bin` is embedded in the firmware by `objcopy` and linked
into the flash-resident `.large_const_data` section. The `.fbs` file is kept
beside it as the format reference; the firmware reads only the binary file.

## 2. Build and download

From the repository root, with the Nuclei toolchain on `PATH`:

```sh
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/flatbuffers clean all
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/flatbuffers upload
```

`flashxip` is also supported. The artifact must stay in flash, so plain `ilm`
is intentionally rejected.

## 3. What the program reads

The program passes the embedded bytes to `rvrt_artifact_read()`, which checks
alignment, FlatBuffers structure, schema version, and required runtime tables.
It then calls `rvrt_artifact_get_info()` and reads the first configuration
frame with `rvrt_artifact_config_frame_words()` when one exists.

Successful serial output ends with:

```text
FLATBUFFER_READ_PASS
```
