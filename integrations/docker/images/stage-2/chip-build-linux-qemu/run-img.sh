#!/bin/bash

KERNEL="/opt/ubuntu-qemu/bzImage"
IMG="/opt/ubuntu-qemu/ubuntu-20.04.img"
ADDITIONAL_ARGS=""

if kvm-ok; then
	ADDITIONAL_ARGS="-enable-kvm -cpu host"
else
	echo "KVM is not available, running without it."
fi

/usr/bin/qemu-system-x86_64 \
	-machine ubuntu \
	-smp 6 \
	-m 1024 \
	-nographic \
	-device virtio-blk-pci,drive=virtio-blk1 \
	-drive file=${IMG},id=virtio-blk1,if=none,format=qcow2,readonly=off \
	-kernel ${KERNEL} \
	-append 'console=ttyS0 mac80211_hwsim.radios=2 root=/dev/vda3' \
	-netdev user,id=network0,hostfwd=tcp::2222-:22 \
	-device e1000,netdev=network0,mac=52:54:00:12:34:56 \
	-virtfs local,path=/chip,mount_tag=host0,security_model=passthrough,id=host0 $ADDITIONAL_ARGS