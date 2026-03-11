# What's this

A brige connect Qemu + external CModel or other library, without write Qemu component

# How it Works

just a vfio-user daemon hook bar's read/write from guest inside Qemu

# Setup

* run this daemon
* run qemu with ` -device '{"driver": "vfio-user-pci","socket": {"path": "/tmp/vfio_user.sock", "type": "unix"}}' `
* in guest , run `lspci -n` ,check vendor id/device id 
