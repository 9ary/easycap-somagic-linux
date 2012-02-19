The EasyCAP Somagic device can be initialized and used in Cygwin, but not with
the factory drivers. Here are the steps required to get started.

Instructions:

 1. Install the factory EasyCAP driver from the CD, if it has not already been.
 2. Download and install a Windows binary version of mplayer with mplayer.exe. 
    It does not need to be a Cygwin version. 
    Link: http://www.mplayerhq.hu/design7/dload.html
 3. Download Cygwin setup.exe and run it. 
    Link: http://www.cygwin.com/install.html
 4. In the installer, choose the default options, until the "Select Packages"
    dialog. In that dialog, expand the "Devel" category and select the following 
    packages for installation: gcc, git, libusb-win32, libgcrypt-devel, make. 
    Accept installation of any dependencies.
 5. Launch the Cygwin Terminal. Create a symbolic link to mplayer in 
    /usr/local/bin. Then, enter the following commands at the terminal:
	cd /usr/src
	git clone https://code.google.com/p/easycap-somagic-linux/
	mkdir /lib/firmware
        cd /usr/src/easycap-somagic-linux/tools/extract-somagic-firmware
	make
	./extract-somagic-firmware /cygdrive/c/Program\ Files/Common\ Files/Somagic/SmiUsbGrabber3C/xp/SmiUsbGrabber3C.sys
	cd /usr/src/easycap-somagic-linux/windows/user
	make
	make install
	exit
 6. Go to "Add or Remove Programs" in the Control Panel and uninstall the 
    "SMI Grabber Device" program. This removes the factory drivers.
 7. Plug in the EasyCAP Somagic device. This should bring up the "Found New 
    Hardware Wizard" dialog. Do not to search online. Choose to install from a 
    list or a specific location. Don't search, choose the driver to install. 
    Choose "Have Disk", then browse to 
    c:\cygwin\usr\src\easycap-somagic-linux\windows\drivers. 
    Select "SM-USB_007" and choose "Open". Complete driver installation.
 8. Launch the Cygwin Terminal. Enter the following command at the terminal:
	somagic-init
 9. The "Found New Hardware Wizard" dialog should pop up. Do not to search 
    online. Choose to install from a list or a specific location. Don't search, 
    choose the driver to install. Choose "Have Disk", then browse to 
    c:\cygwin\usr\src\easycap-somagic-linux\windows\drivers. 
    Select "SMI_Grabber_Device" and choose "Open". Complete driver installation.
10. Installation should now be complete. Return to the Cygwin Terminal and 
    enter a command to capture, for example:
    somagic-capture -n 2>/dev/null | mplayer - -demuxer rawvideo -rawvideo "ntsc:format=uyvy:fps=30000/1001"
11. To use the device in the future, only steps 8 and 10 should be necessary.

