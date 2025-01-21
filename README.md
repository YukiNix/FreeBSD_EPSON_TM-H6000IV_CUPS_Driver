# FreeBSD CUPS Driver for EPSON TM-H6000IV Multifunction Printer

This code is ported from EPSON ImpactReceipt-3.0.0.0 (EPSON impact and receipt printers' CUPS driver for Linux host). 

It could run with other EPSON impact and receipt model too, but I can not test since I do not have those printers. **And it does not work on endorsement mode (ECR-43B) at all.**

Dependencies: clang, cups, make

Build method (Compile and install the two drivers **separately**, privileges may be required): `./build.sh & ./install.sh`

Have fun.

