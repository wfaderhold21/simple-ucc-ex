# UCC Memory Map API Usage Examples

This repository contains a set of examples demonstrating the usage of memory map API in the Unified Collective Communication (UCC) library.
The API allows users to register memory with the UCC library and use this registered memory in collective operations including two-sided
and one-sided operations. In addition, the user can unmap the memory when it is no longer needed.

The repository is broken into two examples:
* __host-to-host collective__: an example performing an all-to-all collective operation between a set of hosts. This example depends on an implementation that supports OpenSHMEM and MPI (e.g., Open-MPI).
* __offloaded collectives using a DPU__: an example performing an all-to-all collective operation offloaded to a DPU. This example requires a DPU and requires OpenSHMEM and MPI.
