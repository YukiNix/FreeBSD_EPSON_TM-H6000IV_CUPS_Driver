#!/bin/sh

echo "EPSON TM series CUPS driver installer"
echo "---------------------------------------"
echo ""
echo ""

ROOT_UID=0

if [ 0 -ne `id -u` ]
then
    echo "This script requires root user access."
    echo "Re-run as root user."
    exit 1
fi

SERVERROOT=$(grep '^ServerRoot' /usr/local/etc/cups/cups-files.conf | awk '{print $2}')

if [ -z $FILTERDIR ] || [ -z $PPDDIR ]
then
    echo "Searching for ServerRoot, ServerBin, and DataDir tags in /usr/local/etc/cups/cups-files.conf"
    echo ""

    if [ -z $FILTERDIR ]
    then
        SERVERBIN=$(grep '^ServerBin' /usr/local/etc/cups/cups-files.conf | awk '{print $2}')

        if [ -z $SERVERBIN ]
        then
            echo "ServerBin tag not present in cupsd.conf - using default"
            FILTERDIR=/usr/local/libexec/cups/filter
        elif [ ${SERVERBIN:0:1} = "/" ]
        then
            echo "ServerBin tag is present as an absolute path"
            FILTERDIR=$SERVERBIN/filter
        else
            echo "ServerBin tag is present as a relative path - appending to ServerRoot"
            FILTERDIR=$SERVERROOT/$SERVERBIN/filter
        fi
    fi

    echo ""

    if [ -z $PPDDIR ]
    then
        DATADIR=$(grep '^DataDir' /usr/local/etc/cups/cups-files.conf | awk '{print $2}')

        if [ -z $DATADIR ]
        then
            echo "DataDir tag not present in cupsd.conf - using default"
            PPDDIR=/usr/local/share/cups/model/EPSON
        elif [ ${DATADIR:0:1} = "/" ]
        then
            echo "DataDir tag is present as an absolute path"
            PPDDIR=$DATADIR/model/EPSON
        else
            echo "DataDir tag is present as a relative path - appending to ServerRoot"
            PPDDIR=$SERVERROOT/$DATADIR/model/EPSON
        fi
    fi

    echo "SERVERBIN = $SERVERBIN"
    echo "FILTERDIR = $FILTERDIR"
    echo "PPDDIR    = $PPDDIR"
    echo ""
fi

INSTALL=/usr/bin/install

echo "Installing filter driver ..."
$INSTALL -s ./build/rastertotmtr $FILTERDIR
echo ""

echo "Installing ppd files ..."
$INSTALL -m 755 -d $PPDDIR 
$INSTALL -m 755 ./ppd/*.ppd $PPDDIR 
echo ""

echo "Restarting CUPS"
service cupsd restart

echo "Installation Completed"
echo "Add a printer queue using OS tool, http://localhost:631, or http://127.0.0.1:631"
echo ""

