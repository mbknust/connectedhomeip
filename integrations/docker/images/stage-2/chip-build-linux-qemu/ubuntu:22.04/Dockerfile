ARG VERSION=latest
FROM ubuntu:22.04
LABEL org.opencontainers.image.source https://github.com/project-chip/connectedhomeip

ARG KERNEL_VERSION=6.7.3

# USER root

RUN set -x \
	&& apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -fy \
	qemu \
	git \
	fakeroot \
	build-essential \
	ncurses-dev \
	xz-utils \
	libssl-dev \
	bc flex \
	libelf-dev \
	bison \
	make \
	cpio dwarves zstd \
	curl \
	python3 \
	&& apt-get clean \
	&& rm -rf /var/lib/apt/lists/ \
	&& : # last line

# Download Linux kernel source
RUN mkdir -p /tmp/workdir
WORKDIR /tmp/workdir
RUN curl https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz -o linux-${KERNEL_VERSION}.tar.xz
RUN tar -xJf linux-${KERNEL_VERSION}.tar.xz && rm linux-${KERNEL_VERSION}.tar.xz

# Set configuration for btvirt
WORKDIR /tmp/workdir/linux-${KERNEL_VERSION}
COPY files/0001-Bluetooth-MGMT-Synchronize-scan-start-and-LE-Meta-ev.patch .
RUN patch -p1 < 0001-Bluetooth-MGMT-Synchronize-scan-start-and-LE-Meta-ev.patch
RUN make x86_64_defconfig
RUN ./scripts/config -e MAC80211 \
	&& ./scripts/config -e MAC80211_HWSIM \
	&& ./scripts/config -e BT \
	&& ./scripts/config -e BT_BREDR \
	&& ./scripts/config -e BT_HCIVHCI \
	&& ./scripts/config -e CONFIG_CRYPTO_USER \
	&& ./scripts/config -e CONFIG_CRYPTO_USER_API_SKCIPHER \
	&& ./scripts/config -e CONFIG_CRYPTO_AES \
	&& ./scripts/config -e CONFIG_CRYPTO_ECB \
	&& ./scripts/config -e CONFIG_CRYPTO_CMAC \
	&& ./scripts/config -e CONFIG_CRYPTO_USER_API_HASH
# Compile
RUN export MAKEFLAGS=-j$(nproc) \
	&& make olddefconfig  \
	&& make

# REguired for modules install
RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -fy \
	kmod
RUN mkdir -p /opt/ubuntu-qemu/rootfs
RUN make modules_install INSTALL_MOD_PATH=/opt/ubuntu-qemu/rootfs \
	&& cp arch/x86/boot/bzImage /opt/ubuntu-qemu/bzImage

# Download Ubuntu image for QEMU
RUN curl https://cloud-images.ubuntu.com/minimal/releases/jammy/release/ubuntu-22.04-minimal-cloudimg-amd64.img \
	-o /opt/ubuntu-qemu/ubuntu-22.04-minimal-cloudimg-amd64.img
RUN curl https://cloud-images.ubuntu.com/minimal/releases/jammy/release/SHA256SUMS \
	-o /opt/ubuntu-qemu/SHA256SUMS
WORKDIR /opt/ubuntu-qemu
# RUN echo ""	&& sha256sum -c SHA256SUMS 2>&1 | grep OK
# RUN echo ""	&& : # last line


RUN apt update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -fy --no-install-recommends \
	libguestfs-tools linux-image-generic

RUN apt update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -fy --no-install-recommends \
	git bc libusb-dev libdbus-1-dev libglib2.0-dev unzip

# RUN https://github.com/bryanperris/ell.git


RUN mkdir -p /tmp/workdir/bluez
WORKDIR /tmp/workdir/bluez
RUN curl https://github.com/bluez/bluez/archive/refs/tags/5.72.zip -L -o /tmp/workdir/bluez/bluez-5.72.zip
RUN unzip bluez-5.72.zip
WORKDIR /tmp/workdir/bluez/bluez-5.72
RUN mkdir -p build

RUN apt update
RUN apt install -fy --no-install-recommends apt-utils
RUN DEBIAN_FRONTEND=noninteractive apt-get install -fy --fix-missing \
	git \
	bc \
	libusb-dev \
	libdbus-1-dev libglib2.0-dev libudev-dev libical-dev libreadline-dev autoconf elfutils \
	libelf-dev libdw-dev libell-dev libell0 build-essential libtool python3-docutils \
	udev

RUN dpkg -l | grep -i Embedded

RUN ./bootstrap && \
	cd build && \
	../configure \
	--disable-silent-rules \
	--enable-pie \
	--enable-tools \
	--enable-test \
	--enable-library \
	--enable-testing \
	--enable-udev \
	--enable-experimental \
	--enable-external-ell \
	--enable-deprecated \
	--enable-backtrace \
	--enable-debug \
	--enable-tools \
	--enable-monitor \
	--disable-cups \
	--disable-sap \
	--disable-hid2hci \
	--disable-a2dp \
	--disable-avrcp \
	--disable-network \
	--disable-hid \
	--disable-hog \
	--disable-health \
	--disable-bap \
	--disable-bass \
	--disable-mcp  \
	--disable-vcp \
	--disable-micp \
	--disable-csip \
	--disable-cups \
	--disable-mesh \
	--disable-midi \
	--disable-obex \
	--disable-optimization \
	--prefix=/usr \
	--mandir=/usr/share/man \
	--sysconfdir=/etc \
	--localstatedir=/var \
	--with-systemdsystemunitdir=/lib/systemd/system \
	--with-systemduserunitdir=/usr/lib/systemd \
	&& make -j8
WORKDIR /tmp/workdir/bluez/bluez-5.72/build
RUN make install DESTDIR=/opt/ubuntu-qemu/rootfs && mkdir -p /opt/ubuntu-qemu/rootfs/usr/bin && cp /tmp/workdir/bluez/bluez-5.72/build/emulator/btvirt /opt/ubuntu-qemu/rootfs/usr/bin

# RUN truncate -s 5G /opt/ubuntu-qemu/ubuntu-22.04.img
RUN qemu-img create -f qcow2 -o preallocation=metadata /opt/ubuntu-qemu/ubuntu-22.04.img 10G
RUN virt-resize --expand /dev/sda1 /opt/ubuntu-qemu/ubuntu-22.04-minimal-cloudimg-amd64.img /opt/ubuntu-qemu/ubuntu-22.04.img

COPY files /opt/ubuntu-qemu/virt
RUN mv /opt/ubuntu-qemu/virt/run-img.sh /opt/ubuntu-qemu && chmod +x /opt/ubuntu-qemu/run-img.sh
RUN guestfish -a /opt/ubuntu-qemu/ubuntu-22.04.img \
	--mount /dev/sda3:/ \
	copy-in /opt/ubuntu-qemu/rootfs/lib /usr : \
	copy-in /opt/ubuntu-qemu/rootfs/usr / : \
	copy-in /opt/ubuntu-qemu/virt /opt : \
	copy-in /opt/ubuntu-qemu/virt/config/01-netcfg.yaml /etc/netplan : \
	sh 'apt-get purge -y snapd' : \
	sh 'systemctl disable systemd-networkd-wait-online.service cloud-init.service lxd-agent.service ' : \
	sh 'systemctl mask systemd-networkd-wait-online.service cloud-init.service lxd-agent.service ' : \
	sh 'systemctl enable bluetooth' : \
	sh 'passwd -d root' : \
	sh 'ssh-keygen -A' : \
	sh '/bin/echo -e "PermitRootLogin yes\nPasswordAuthentication yes\nPermitEmptyPasswords yes" >> /etc/ssh/sshd_config' : \
	sh 'rm /etc/ssh/sshd_config.d/60-cloudimg-settings.conf'


RUN DEBIAN_FRONTEND=noninteractive apt-get install -fy --fix-missing \
	python3-pip \
	python3-venv \
	python3.10-venv \
	python-is-python3

RUN pip3 install click diskcache coloredlogs ipython

RUN DEBIAN_FRONTEND=noninteractive apt-get install -fy --fix-missing \
	dnsmasq \
	hostapd \
	wpasupplicant \
	iw \
	libdw1 \
	rfkill

RUN guestfish -a /opt/ubuntu-qemu/ubuntu-22.04.img \
	--mount /dev/sda3:/ \
	copy-in /opt/ubuntu-qemu/rootfs/lib /usr : \
	copy-in /opt/ubuntu-qemu/rootfs/usr / : \
	copy-in /opt/ubuntu-qemu/virt /opt : \
	copy-in /opt/ubuntu-qemu/virt/config/01-netcfg.yaml /etc/netplan : \
	sh 'systemctl disable hostapd wpa_supplicant hostapd dnsmasq'

# ip link set enp0s4 up
# dhclient enp0s4

# && systemctl disable snapd.service && systemctl mask snapd' :
# rm /etc/systemd/system/multi-user.target.wants/snapd.service

# USER $USER_NAME
# WORKDIR $USER_HOME

WORKDIR /opt/ubuntu-qemu
