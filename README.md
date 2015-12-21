This is the x11r7 version of the imx xorg driver as included in buildroot 2015.05. It has already been patched with the patches included in buildroot:

* 0001-Update-to-newer-swap-macros.patch
* 0002-Fix-error-unknown-type-name-uint.patch
* 0003-support-glibc-2.20.patch
* 0004-Make-video-API-forward-and-backward-compatible.patch
* 0005-xf86-video-imxfb-fix-m4-hardcodded-paths.patch
* 0006-xserver-1.14-compat.patch

Be aware that this driver requires the closed-source z160 library.

For a version that does not require the z160 library, but which does not have 2D hardware acceleration, see the open-source-no-accel branch.

# Compile and install

```
# install dependencies
apt-get install build-essential checkinstall
apt-get build-dep xserver-xorg-video-fbdev

# ensure that you have the fread kernel include files at /usr/src/linux/include

# install the proprietary binary-only libz160
wget http://repository.timesys.com/buildsources/l/libz160-bin/libz160-bin-11.09.01/libz160-bin-11.09.01.tar.gz
tar xvzf libz160-bin-11.09.01.tar.gz
sudo cp -a libz160-bin-11.09.01/* /
rm -rf libz160-bin-11.09.01*

# configure and compile
./autogen.sh --prefix=/usr
make
sudo checkinstall -y --pkgname xserver-xorg-video-imx --pkgversion 0.0.1 -D make install # or just "make install"
```
