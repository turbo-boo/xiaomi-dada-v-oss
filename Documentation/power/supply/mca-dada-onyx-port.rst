Dada MCA port from Onyx
======================

The Dada tree now contains the Onyx wired buck/PD/PPS/QC state machines,
wireless basic/quick/reverse controllers, full fuel-gauge strategy with SOC
smoothing and shutdown policy, smart-charge scene/night/SOC/bypass policy,
thermal policy and battery/USB/wireless power-supply implementations.

The hardware port covers the ICs declared by Dada's DT: BQ27Z561, SC8585
(implemented by sc8581.c), NU1652 and HL7603. The temporary compatibility
voters and BAA-only/read-only/no-charging strategy replacements are removed.
Stock module filenames and the Dada Kleaf path translations remain intact.

References
----------

* Onyx: xiaomi-onyx-playground/android_kernel_xiaomi_sm8735,
  lineage-23.2, commit 737c220c601ec2202ada953041770aa4c6df7e66.
* Dada DT: MiCode/kernel_devicetree, dada-v-oss,
  commit 233bd52eddce9f081f8e87802796f5994babafa5,
  qcom/mca.dtsi and qcom/dada-charger-common.dtsi.
* Stock dump: https://dumps.tadiphone.dev/dumps/xiaomi/dada.
  The Android 15 CN OS2.0.101.0 branch resolves to
  1125d12771fff547cad324aa7cd3961cd9156489. Project/tree metadata was readable
  during this port; clone, raw module and deeper tree requests failed with
  HTTP 502 or timeouts. This port does not claim a new binary ABI comparison
  against that dump.

Dada adaptations
----------------

The existing Dada GLINK owner/opcode/property mapping, hardware ID handling,
path-control/connector/OVP/OCP/BMD/LPD monitors, shared-memory/partition access
and DT-driven single-pack JEITA policy are retained. The folding-device
``flip_charge_curr`` voter is optional when base/flip support is disabled.
Termination current uses the restrictive MIN election required by Dada JEITA.

BASP retains Dada's 36-byte header and inline 24-byte curve header. Each open
file owns its own page; closing takes an immutable snapshot before validation.
The parser validates checksum, complete size, all counts/indices/ranges,
duplicate rows, FFC section identity and voltage/current step semantics before
dispatch. Wired/wireless curve updates stage allocations and reject updates
while that quick-charge controller is online. Temperature bands remain DT-owned.
A consumer error is returned; application across independent controllers is
not a rollback transaction.

Callback registries use SRCU, which permits sleeping/nested class callbacks.
Providers detach before their state is released. Stop flags, notifier/thread
shutdown, work draining and diagnostic removal replace empty Onyx remove paths.
Voter addresses are retained in the core registry because consumers cache them;
removal detaches the callback and data, and a later bind can reuse the entry.
Force-stop paths do not synchronously cancel their currently executing monitor.

Firmware and external dependencies
----------------------------------

NU1652 firmware download/erase/bin controls and vendor/index selection are
ported, including index bounds checks. Dada-specific equality of the included
Onyx firmware image with stock has not been established. Destructive firmware
operations therefore return ``-EOPNOTSUPP`` unless the board explicitly supplies
``xiaomi,allow-firmware-update``. The official Dada DT does not opt in.
No device firmware is written by the validation workflow.

The MIEV reporting implementation follows Onyx and requires its Xiaomi MIEV
transport when CONFIG_MIEV is enabled. A build with that transport disabled
does not establish telemetry delivery. ADSP firmware and the Android charging
HAL are external runtime dependencies.

Off-device validation
---------------------

The MCA workflow builds the selected arm64 kernel Image and modules without
relaxing modpost errors, in addition to compiling the MCA/wireless subtrees.
The verification configuration enables MHI_BUS_MISC, IPC_LOGGING, the vendor UFS
PHY, QCOM_IOMMU_UTIL and Android vendor hooks/OEM data. It excludes the obsolete Q6V5_MSS remoteproc driver
(which has duplicate reg_info definitions in this public tree) and the
legacy Qualcomm interconnect drivers (their upstream node layout conflicts
with the vendor-modified interconnect headers), and CoreSight USB (its QDSS
header is outside this release). This generic verification configuration is not a device boot image or
a substitute for the vendor Kleaf/GKI ABI build.

JEITA tests cover 68 checks against the official CN/Global tables. BASP tests
cover 20,207 checks with address/undefined-behavior sanitizers, including
unaligned input, truncation, duplicate/invalid rows and deterministic malformed
packets. verify_dada_port.py checks the 52 packaged MCA module paths and the
recorded official Dada DT bindings, then checks the compiled OF aliases and
presence of the full kernel Module.symvers.

Device probing, charging/thermal/SOC accuracy, reconnect behavior, suspend and
shutdown, wireless power/FOD/reverse behavior and firmware compatibility still
require hardware validation. These are not inferred from successful builds.
