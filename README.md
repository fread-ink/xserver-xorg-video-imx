This is the x11r7 version of the imx xorg driver as included in buildroot 2015.05. It has already been patched with the patches included in buildroot:

* 0001-Update-to-newer-swap-macros.patch
* 0002-Fix-error-unknown-type-name-uint.patch
* 0003-support-glibc-2.20.patch
* 0004-Make-video-API-forward-and-backward-compatible.patch
* 0005-xf86-video-imxfb-fix-m4-hardcodded-paths.patch
* 0006-xserver-1.14-compat.patch

This is an experimental and untested version of the driver which does not depend on any non-open binary blobs, but also does not have 2D hardware acceleration support.

# Compile and install

```
# install dependencies
sudo apt-get install build-essential checkinstall
sudo apt-get build-dep xserver-xorg-video-fbdev

# ensure that you have the fread kernel include files at /usr/src/linux/include

# configure and compile
./autogen.sh --prefix=/usr
make
sudo checkinstall -y --pkgname xserver-xorg-video-imx --pkgversion 0.0.1 -D make install # or just "make install"
```

