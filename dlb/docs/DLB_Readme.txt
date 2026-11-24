*******************************************************************************

	    Intel Dynamic Load Balancer 2.x Driver Release 8.14.0

********************************************************************************
Disclaimer: This code is being provided to potential customers of Intel DLB to
enable the use of Intel DLB well ahead of the kernel.org and DPDK.org
upstreaming process. Please be aware that based on the open source community
feedback, the design of the code module can change, including API
interface definitions. If the open source implementation differs from what is
presented in this release, Intel may reserve the right to update the
implementation to align with the open source version at a later time and stop
supporting this early enablement version.

===================
Table of Contents
===================

* Intel Dynamic Load Balancer
* Intel DLB Software Installation Notes
* Kernel and DPDK Versions
* Build and Installation
* Examples / Test Cases
* Alerts & Notices
* Known Issues

=========================================
Intel Dynamic Load Balancer (Intel DLB)
=========================================

Intel DLB 2.x is a PCIe device that provides load-balanced, prioritized
scheduling of events across CPU cores enabling efficient core-to-core
communication. It can also serve as an accelerator for the event-driven programming 
model of DPDK's Event Device Library. This DPDK library is used in packet processing 
pipelines for multi-core scalability, dynamic load-balancing, and variety of packet
distribution and synchronization schemes. For non-DPDK applications, a separate library
named libdlb is provided with the release. 

=======================================
Intel DLB Software Installation Notes
=======================================

The Intel DLB software comes in the following package:

    dlb_linux_src_release_<rel-id>.txz

The package contains the Intel DLB kernel driver, DPDK Patch for Intel DLB 2.x
PMD (Poll Mode Driver) with other required changes to standard DPDK, and libdlb
client library for non-dpdk applications. The package can be extracted using
the following command:

    $ tar xfJ dlb_linux_src_release_<rel-id>.txz

DLB Software feature details can be found in DLB Programmer Guides.

============================
Kernel and DPDK Versions
============================

Linux Kernel Version: Tested with Linux Kernel versions 6.19 and 7.2.

DPDK Base Version: v25.11.2

===========================
Build and Installation
===========================

Refer to document "DLB_SW_User_Guide.pdf" for detailed instructions on build
and installation.

==========================
Examples / Test Cases
==========================

This Intel DLB release contains libdlb, the client library for building Intel
DLB applications which do not require complete DPDK framework. It also includes
a set of sample tests to demonstrate various features. Accompanying User Guide
has details on the included samples.

====================================================
Resolved issues and new features in release 8.14.0
====================================================

- Security fixes in the DLB kernel driver and libdlb: input validation for
  ioctl, sysfs, VF/PF mailbox, live-migration and perf PMU paths; fixes for
  kernel information leaks, reference leaks and port file lifetime across FLR.
- Fix cross-VF denial of service when a VF domain reset times out on the
  VF->PF mailbox.
- Fix migration state buffer sizing in the VFIO and VDCM live-migration paths.
- dlb_monitor: fix AQED storage counter and output formatting.
- DPDK PMD rebased from DPDK v24.11.1 to DPDK v25.11.2.
- DPDK PMD: fix a directory handle leak in bifurcated mode device detection,
  and reject negative queue/port IDs in xstats reset.
- DPDK PMD devargs: port_dequeue_wait no longer loops forever on a malformed
  port list (e.g. "1+:polling"), and cos_bw rejects values outside 0-100
  instead of overflowing the sum check.
- libdlb: reject negative domain/queue IDs, out-of-range queue depth levels
  and credit pool ID 64, and handle dlb_release() with num == 0.
- DPDK eventdev: rte_event_port_attr_get() now supports
  RTE_EVENT_PORT_ATTR_INDEPENDENT_ENQ (previously returned -EINVAL).
- Add support for Linux kernels 6.19 and 7.2.

====================================================
Resolved issues and new features in release 8.13.0
====================================================

- HQM-1185: Add support for enabling/disabling CQs from application
- BZ 1616: Fix compilation on CPUs without AVX512 support
- 16027749838.27: [GNRD PRQ] Failed to compile DLB driver in RHEL-9.6.0-GA OS
- Add support for Linux kernels 6.14.

=====================
Alerts & Notices
=====================

- The software distributed in this release is not intended for secure use
  and should not be used to transmit or store security sensitive or privacy
  protected data.
- DLB2.x application only supports simultaneous use of either Bi-furcated mode
  devices  (bound to dlb2 driver) or PF PMD mode devices (bound to vfio-pci).
  Application should use all devices of the same type when some devices are
  bound to dlb2 driver and some to vfio-pci module. Different application can
  use different types and run at the same time. To avoid picking devices bound
  to vfio-pci while using devices bound to dlb2 driver, use the --no-pci
  option.
- SIOV is not supported with SUSE Linux Enterprise Server 15 because functions
  needed are not back ported.
- HQM-417: For older kernels (pre 5.15) with some specific application patterns
  and kernel configuration, DLB device binding to vfio-pci driver results in a
  crash. In such cases, "uio_pci_generic" or "igb_uio" module can be used in
  place of "vfio-pci" (with IOMMU disabled or in passthrough mode) or Bifurcated
  mode can be used by linking device to DLB driver. This issue is not observed
  with kernels 5.15 and later.
- HQM-899: A new port probing value (2) is introduced for port probing which improves
  driver load times. Port_probe = 0; No probe, Port_probe = 1; enables port probing
  with longer load times but higher success rate compared to improved probe, 
  Port_probe = 2 ; (improved probe), less driver load times, less probe success rate
  compared to port_probe = 1. Improved probe is the default driver behavior.
- SIOV feature is not supported with standard Linux kernels. To disable SIOV at
  compile time use "make SIOV=n".
- To build dlb2 driver for standard Linux Kernel 6.2.0, please use the command
  "make SRIOV_LM=n"
- DLB2_IOC_ENABLE_CQ_EPOLL: the process_id argument is now ignored and
  event_fd is always resolved in the calling process. Applications must pass
  an eventfd that they own.
- DLB2_IOC_BLOCK_ON_CQ_INTERRUPT: cq_va must be a user-space address; kernel
  addresses are rejected with -EFAULT.
- Port file descriptors are bound to the scheduling domain that created them.
  After a device reset (FLR), mmap() on a port fd from the old domain fails;
  applications must recreate their domain and ports.
- DPDK v25.11 removed the "enable_kmods" meson option; do not pass
  -Denable_kmods when configuring DPDK.
- The rte_pmd_dlb2.h API in this release keeps the pre-DPDK 25.07 token pop
  mode names (enum dlb2_token_pop_mode: AUTO_POP, DELAYED_POP, DEFERRED_POP);
  upstream DPDK 25.07 and later prefix them with RTE_PMD_DLB2_. Applications
  written for upstream DPDK must use the un-prefixed names with this PMD.
  
=====================
Known Issues
=====================
- The movdir64b instruction does not work properly in pf passthrough mode with libdlb.
  User can disable movdir64b feature when running qemu for the pf passthrough
  by using "-cpu host,movdir64b=off" option.
